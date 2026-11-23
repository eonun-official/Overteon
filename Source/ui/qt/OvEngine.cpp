#include "OvEnv.h"
#include "OvEngine.h"
#include "OvQmlTypes.h"
#include "OvSpectrum.h"
#include "OvTick.h"

#include <cstdio>
#include <cstdlib>

#include <QCursor>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <cmath>

OvEngine::OvEngine (std::unique_ptr<OvSource> source, QObject* parent)
    : QObject (parent), m_source (std::move (source))
{
    const int n = m_source->count();
    for (int i = 0; i < n; ++i)
    {
        auto* p = new OvParam (*m_source, i, this);   // parented: QML must not delete it
        m_params.insert (p->id(), QVariant::fromValue<QObject*> (p));
        m_paramList.push_back (p);

        // A UI edit must bump the parameter revision HERE.  OvParam::setNorm()
        // writes its own cache before the next poll, so tick()'s
        // "before vs after poll()" comparison can never see that change — the
        // revision only moved for HOST edits, and a UI-only change (the band
        // type menu is the clearest one) left the plots stale until something
        // else repainted them.  Dragging looked fine only because the drag
        // handlers call update() themselves.
        connect (p, &OvParam::edited, this, [this, p] {
            const QString id = p->id();
            bool sc = false, pq = false;
            if (id.startsWith (QLatin1String ("sc")))      sc = true;
            else if (id.startsWith (QLatin1String ("pq"))) pq = true;
            else if (id.startsWith (QLatin1String ("drive"))
                     || id == QLatin1String ("protectMode")) sc = pq = true;
            ++m_paramRevision;
            emit paramRevisionChanged (sc, pq);
        });
    }

    // A/B starts with BOTH slots holding the state the plug-in came up with.
    //
    // B used to stay invalid until the first switch, which then seeded it from
    // the ALREADY EDITED A — so the first A→B changed nothing (the slot label
    // moved but the EQ and every other parameter stayed on A) and only later
    // switches, once B existed, appeared to work.  Recording both slots here
    // makes the first switch a real recall of the sound the plug-in started
    // with, while A remains the live/edited one.
    m_ab[0] = m_source->snapshotParams();
    m_ab[1] = m_ab[0];

    m_meters = m_source->meters();
    m_driveMode = m_source->driveModeIndex();
    m_spectrum = QVariantList();
    for (int i = 0; i < ovspec::kBins; ++i) m_spectrum << 0.0;

    refreshPresets();

    m_timer.setInterval (OvTiming::kPollMs);   // parameters + spectrum
    connect (&m_timer, &QTimer::timeout, this, &OvEngine::tick);
    m_timer.start();

    // Sample the light at display rate; OvGpuGlass animates between values with
    // the reference's 100 ms linear transition.
    m_lightTimer.setInterval (1000 / 60);
    connect (&m_lightTimer, &QTimer::timeout, this, &OvEngine::lightTick);
    m_lightTimer.start();
}

OvEngine::~OvEngine() = default;

// ---------------------------------------------------------------------------
//  The light behind the accent glow.  0.9.10 uses the louder L/R measurement
//  directly; only the QML glass adds the reference's 100 ms linear transition.
// ---------------------------------------------------------------------------
void OvEngine::lightTick()
{
    const auto now = std::chrono::steady_clock::now();

    // 0.9.10: (dB + 60) / 60，左右声道取较大者，随后由 QML 做平方映射。
    // 光源自身不再叠加 Smooth； OvGpuGlass 的 100 ms 线性过渡对应原 CSS。
    const auto toLight = [] (double db)
    {
        return std::clamp ((db + 60.0) / 60.0, 0.0, 1.0);
    };
    const double nextOut = toLight (std::max (m_meters.outL, m_meters.outR));
    const double nextIn  = toLight (std::max (m_meters.inL,  m_meters.inR));

    // Threshold in the 0..1 domain: 0.002 is well under one pixel of glow.
    if (std::abs (nextOut - m_outLight) > 0.002 || std::abs (nextIn - m_inLight) > 0.002)
    {
        m_outLight = nextOut;
        m_inLight  = nextIn;
        emit lightChanged();
    }

    if (ovEnvFlag ("OVERTEON_METERDBG"))
    {
        std::fprintf (stderr, "[l] %.3f  out %6.2f dB  in %6.2f dB  "
                              "light out %.4f in %.4f\n",
                      std::chrono::duration<double> (now.time_since_epoch()).count(),
                      std::max (m_meters.outL, m_meters.outR),
                      std::max (m_meters.inL, m_meters.inR),
                      m_outLight, m_inLight);
    }
}

QObject* OvEngine::param (const QString& id) const
{
    return typedParam (id);
}

OvParam* OvEngine::typedParam (const QString& id) const
{
    auto it = m_params.find (id);
    if (it == m_params.end()) return nullptr;
    return qobject_cast<OvParam*> (it->value<QObject*>());
}

void OvEngine::tick()
{
    // ---- the parameter scan is rate limited, the METERING IS NOT --------
    // The QTimer runs at kPollMs (8 ms) on the assumption of an event loop that
    // fires that often.  Inside a plug-in the loop is pumped by the host's idle
    // callback: jalv delivers that at roughly 15 Hz with a 3.4x spread, and when
    // processEvents() finally runs Qt dispatches EVERY expired timer at once,
    // so one pump produced a burst of 8-15 ticks.
    //
    // For PARAMETERS a burst is harmless (the same value is read again) but it
    // wasted work and repainted both plots, so that scan is gated on wall-clock
    // time.  The METERS and the SPECTRUM are different: they are instantaneous
    // snapshots of a moving signal, and they must be read on every call.  Gating
    // them is what made the VU meter and the analyser stop responding — there
    // was only one read per host heartbeat, and the drawing then showed a single
    // stale value held for the whole gap.
    using clk = std::chrono::steady_clock;
    static auto lastParamScan = clk::time_point();
    const auto nowT = clk::now();
    bool scanParams = true;
    if (lastParamScan != clk::time_point())
    {
        const auto since = std::chrono::duration_cast<std::chrono::milliseconds> (nowT - lastParamScan);
        scanParams = since.count() >= OvTiming::kPollMs;
    }
    if (scanParams) lastParamScan = nowT;

    // A parameter "changed" only matters to the UI when it moved by more than
    // the pixel resolution of a control.  Comparing exact doubles made every
    // float wobble (host automation, the mock's drift) count as a change, and
    // each change repaints both EQ plots — the plots then ran at the poll rate
    // instead of their own 30 Hz, costing ~25 % of a core.
    bool any = false;
    // Which parameter groups moved since the last poll.  Both EQ plots connect
    // to paramRevisionChanged, so without this a drag on an SC node repainted
    // the PQ plot too — its 2.4 ms render for a picture that had not changed.
    bool anySc = false, anyPq = false, anyOther = false;
    for (auto* p : m_paramList)
    {
        if (! scanParams) break;        // parameters: rate limited (see above)
        const auto before = p->norm();
        p->poll();
        if (std::abs (p->norm() - before) <= 1.0 / 4096.0) continue;

        any = true;
        const QString id = p->id();
        if (id.startsWith (QLatin1String ("sc")))      anySc = true;   // sc1Freq, scRange ...
        else if (id.startsWith (QLatin1String ("pq"))) anyPq = true;
        else if (id.startsWith (QLatin1String ("drive"))
                 || id == QLatin1String ("protectMode"))
        {   // these change what both plots show (dimming, gain scale)
            anySc = anyPq = true;
        }
        else anyOther = true;
    }
    Q_UNUSED (anyOther);

    if (any)
    {
        ovtick::markKind ("params-new", ovtick::TickKind::ParamsChanged);
        ++m_paramRevision;
        emit paramRevisionChanged (anySc, anyPq);
    }

    const int mode = m_source->driveModeIndex();
    if (mode != m_driveMode)
    {
        m_driveMode = mode;
        if (auto* p = typedParam (QStringLiteral ("protectMode")))
        {
            m_limiterMode = (int) std::lround (p->norm() * 3.0);
        }
        emit driveModeChanged();
    }
    else if (auto* p = typedParam (QStringLiteral ("protectMode")))
    {
        const int lm = (int) std::lround (p->norm() * 3.0);
        if (lm != m_limiterMode) { m_limiterMode = lm; emit driveModeChanged(); }
    }

    // One "sample" = one read of the analyser/meters.  This is the rate the
    // display ultimately cannot exceed, so it is reported separately from the
    // repaint rate: `--tick` prints both and the gap between them is the thing
    // that was being argued about.
    ovtick::markKind ("sample", ovtick::TickKind::Sample,
                      1000.0 / double (OvTiming::kPollMs));

    const auto m = m_source->meters();
    if (std::abs (m.inL - m_meters.inL) > 0.01f || std::abs (m.inR - m_meters.inR) > 0.01f
        || std::abs (m.outL - m_meters.outL) > 0.01f || std::abs (m.outR - m_meters.outR) > 0.01f
        || std::abs (m.harm - m_meters.harm) > 0.01f)
    {
        m_meters = m;
        ovtick::markKind ("meters-new", ovtick::TickKind::MetersChanged);
        emit metersChanged();
    }

    // Only notify on a real change.  Emitting unconditionally 30x a second
    // made both EQ plots repaint even with identical data, and each repaint
    // re-renders the plot and runs the glow blur.
    //
    // The threshold below is in display units, 0..1 over the plot's 96 dB
    // range, and the plot is ~900 px wide with 1024 bins — one bin is about
    // one pixel, so 0.004 (0.38 dB) cannot move a pixel by more than a
    // fraction.  It was 0.0005, which the FFT of a moving signal exceeds on
    // almost every poll.
    // The analyser is re-read at ~60 Hz wall-clock, not every poll and not
    // every other poll.  The poll timer is 8 ms, but inside a plug-in the host
    // only pumps Qt's events at its own idle rate (typically 30-60 Hz), so
    // "every second tick" was actually 15-30 spectrum updates a second — the
    // stutter this change is about.  A 12 ms gate gives 60 Hz whether the host
    // pumps at 60 or Qt's timer is allowed to run at 125; the plot's own 60 Hz
    // repaint is the other half of the pair.
    const auto specNow = std::chrono::steady_clock::now();
    const bool scanSpec = m_lastSpecScan == std::chrono::steady_clock::time_point()
                       || specNow - m_lastSpecScan >= std::chrono::milliseconds (12);
    if (scanSpec)
    {
        m_lastSpecScan = specNow;

        // Each tap is asked separately and each may have nothing new.  The
        // analyser is continuous now (a ring of the newest samples), so while
        // audio runs both report a fresh window on every scan; only a stopped
        // transport leaves the table alone, and that is handled by the stale
        // timer below.
        float specIn[ovspec::kBins], specOut[ovspec::kBins];
        const bool haveIn  = m_source->spectrumIn (specIn, ovspec::kBins);   // SC: enters
        const bool haveOut = m_source->spectrum (specOut, ovspec::kBins);    // PQ: leaves

        if (m_inSpectrum.size() != ovspec::kBins)
            m_inSpectrum = QVariantList (ovspec::kBins, 0.0);

        bool changed = false;

        // ---- silence has to reach the display -----------------------------
        //
        // A host that stops calling processBlock (transport stopped) stops
        // advancing the sample ring, so both taps report nothing and would
        // leave the last picture on screen for ever.  That is invisible with
        // the eased display but wrong for RAW, whose whole point is that it
        // shows the current measurement and nothing else.  After 150 ms
        // without new samples the table is authoritative silence.
        constexpr auto specStale = std::chrono::milliseconds (150);

        auto zeroTable = [&changed] (QVariantList& table)
        {
            for (auto& v : table)
                if (v.toDouble() > 0.0005) { v = 0.0; changed = true; }
        };

        if (haveIn)
        {
            // Same gate as the output table.  Without it the upper plot only
            // repainted when the LOWER one happened to change: this loop
            // stored the values but never raised `changed`, so an input-only
            // difference produced no spectrumChanged at all.
            for (int i = 0; i < ovspec::kBins; ++i)
            {
                const double v = (double) specIn[i];
                if (std::abs (m_inSpectrum.at (i).toDouble() - v) > 0.004)
                {
                    m_inSpectrum[i] = v;
                    changed = true;
                }
            }
            m_inSpecAt = specNow;
        }
        else if (m_inSpecAt != std::chrono::steady_clock::time_point()
                 && specNow - m_inSpecAt > specStale)
        {
            zeroTable (m_inSpectrum);
        }

        if (haveOut)
        {
            for (int i = 0; i < ovspec::kBins; ++i)
            {
                const double v = (double) specOut[i];
                if (std::abs (m_spectrum.at (i).toDouble() - v) > 0.004)
                {
                    m_spectrum[i] = v;
                    changed = true;
                }
            }
            m_outSpecAt = specNow;
        }
        else if (m_outSpecAt != std::chrono::steady_clock::time_point()
                 && specNow - m_outSpecAt > specStale)
        {
            zeroTable (m_spectrum);
        }
        // OVERTEON_ENGDBG: what the poll actually saw, once a second.
        //
        // Without this, "the spectrum is not moving" has a dozen possible
        // causes that all look identical on screen.  These three counters
        // separate them: haveIn/haveOut say whether the source produced a new
        // FFT, changed says whether it differed enough to reach the UI.  All
        // three at zero means the audio never arrived; changed at zero with the
        // other two moving means the data is constant, which is what a muted
        // or unconnected input looks like.
        if (ovEnvFlag ("OVERTEON_ENGDBG"))
        {
            static int nIn = 0, nOut = 0, nCh = 0, nPoll = 0;
            static auto t0 = std::chrono::steady_clock::now();
            ++nPoll; if (haveIn) ++nIn; if (haveOut) ++nOut; if (changed) ++nCh;
            const double dt = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - t0).count();
            if (dt >= 1.0)
            {
                std::fprintf (stderr, "[eng] poll %.0f/s haveIn %.0f/s haveOut %.0f/s "
                                      "changed %.0f/s  first %.3f %.3f\n",
                              nPoll/dt, nIn/dt, nOut/dt, nCh/dt,
                              m_inSpectrum.isEmpty() ? -1.0 : m_inSpectrum.at(0).toDouble(),
                              m_spectrum.isEmpty() ? -1.0 : m_spectrum.at(0).toDouble());
                nIn = nOut = nCh = nPoll = 0; t0 = std::chrono::steady_clock::now();
            }
        }
        if (haveIn || haveOut)
            ovtick::markKind ("fft-block", ovtick::TickKind::FftBlock);
        if (changed)
        {
            ovtick::markKind ("spectrum-new", ovtick::TickKind::SpectrumChanged);
            emit spectrumChanged();
        }
    }
}

void OvEngine::initAll()
{
    m_source->applyInit();
    for (auto* p : m_paramList) p->poll();
    ++m_paramRevision;
    emit paramRevisionChanged (true, true);
}

namespace
{
    // The preset tree is handed to QML as nested maps/lists:
    //   { name: "Factory", folders: [ {...} ], presets: ["Air Lift"] }
    // QML builds one cascading panel per folder node, so the depth of the tree
    // is the only limit on the depth of the menu.
    QVariantMap presetNodeFromJson (const QJsonObject& o)
    {
        QVariantMap node;
        node.insert (QStringLiteral ("name"), o.value (QStringLiteral ("name")).toString());

        QVariantList folders;
        for (const auto& v : o.value (QStringLiteral ("folders")).toArray())
            folders.append (presetNodeFromJson (v.toObject()));
        node.insert (QStringLiteral ("folders"), folders);

        QStringList presets;
        for (const auto& v : o.value (QStringLiteral ("presets")).toArray())
            presets << v.toString();
        node.insert (QStringLiteral ("presets"), presets);
        return node;
    }
}

void OvEngine::refreshPresets()
{
    const auto json = QString::fromStdString (m_source->presetListJson());
    const auto doc = QJsonDocument::fromJson (json.toUtf8());

    QVariantMap out;
    if (doc.isObject()) out = presetNodeFromJson (doc.object());

    if (out != m_presets)
    {
        m_presets = out;
        emit presetsChanged();
    }
}

void OvEngine::loadPreset (const QString& folder, const QString& name)
{
    if (m_source->loadPresetNamed (folder.toStdString(), name.toStdString()))
    {
        m_presetName = name;
        emit presetNameChanged();
        for (auto* p : m_paramList) p->poll();
        ++m_paramRevision;
        emit paramRevisionChanged (true, true);
    }
}

void OvEngine::loadPresetDialog() { m_source->openLoadDialog(); }
void OvEngine::savePresetDialog() { m_source->openSaveDialog(); }

void OvEngine::setPresetName (const QString& name)
{
    if (name != m_presetName) { m_presetName = name; emit presetNameChanged(); }
}

// ---- display settings -------------------------------------------------------
//
// Plain setters with a change guard.  None of this reaches the DSP: the analyser
// smoothing and the meter ballistics both live in the UI (OvSpectrum.h,
// OvBallistics.h) and read the tables the engine produces.  One shared signal is
// deliberate — the panel binds to it, and the ring and the two views each read
// the property they care about.
void OvEngine::setHarmMode (int v)
{
    v = (v == 1) ? 1 : 0;
    if (v != m_harmMode) { m_harmMode = v; emit settingsChanged(); }
}

void OvEngine::setUpperMode (int v)
{
    v = (v == 1) ? 1 : 0;
    if (v != m_upperMode) { m_upperMode = v; emit settingsChanged(); }
}

void OvEngine::setLowerMode (int v)
{
    v = (v == 1) ? 1 : 0;
    if (v != m_lowerMode) { m_lowerMode = v; emit settingsChanged(); }
}

void OvEngine::setGrRange (int v)
{
    if (v != 6 && v != 12 && v != 24 && v != 48) return;
    if (v != m_grRange) { m_grRange = v; emit settingsChanged(); }
}

void OvEngine::setMeterRelease (int v)
{
    v = (v == 1) ? 1 : 0;
    if (v != m_meterRelease) { m_meterRelease = v; emit settingsChanged(); }
}

// ---- window resize (the QML corner grip) ------------------------------------
QPointF OvEngine::cursorGlobalPos() const
{
    return QCursor::pos();
}

void OvEngine::requestWindowResize (int w, int h)
{
    if (windowResizeRequest) windowResizeRequest (w, h);
}

void OvEngine::windowSizeOriginal()
{
    if (windowSizeOriginalRequest) windowSizeOriginalRequest();
}

void OvEngine::windowSizeDefault()
{
    if (windowSizeDefaultRequest) windowSizeDefaultRequest();
}

void OvEngine::windowSizeSaveDefault()
{
    if (windowSizeSaveDefaultRequest) windowSizeSaveDefaultRequest();
}

// ---- diagnostics ------------------------------------------------------------
//
// The log build installs reportIssueRequest; an OFF build leaves it empty and
// the Settings panel never shows the row.
void OvEngine::reportIssue()
{
    if (reportIssueRequest) reportIssueRequest();
}

bool OvEngine::diagnosticsAvailable() const
{
    return bool (reportIssueRequest);
}

// ---- global controls --------------------------------------------------------
void OvEngine::setBypassed (bool on)
{
    if (on == m_bypassed) return;
    m_bypassed = on;

    // Through the parameter, not a private flag: the DSP reads the parameter and
    // the host is told.  A bool parameter's value IS its normalised value, so 0/1
    // is exact — no scaling to go wrong.
    if (auto* p = typedParam (QStringLiteral ("bypass")))
        p->setNorm (on ? 1.0 : 0.0);

    emit bypassChanged();
}

void OvEngine::setAbSlot (int slot)
{
    slot = (slot == 1) ? 1 : 0;
    if (slot == m_abSlot) return;

    // Leaving the current slot: record it, so the switch is a real comparison
    // and not a one-way trip.
    m_ab[m_abSlot] = m_source->snapshotParams();

    // Entering the other one: if it has never been written, seed it from the
    // live state rather than recalling the default over the user's work.
    if (! m_ab[slot].valid)
        m_ab[slot] = m_source->snapshotParams();
    else
        m_source->restoreParams (m_ab[slot]);

    m_abSlot = slot;
    emit abChanged();

    // The restored values reach QML the same way a preset load does: poll every
    // param first, then bump the revision so both EQ halves repaint.
    for (auto* pp : m_paramList) pp->poll();
    ++m_paramRevision;
    emit paramRevisionChanged (true, true);
}

void OvEngine::abCopyFrom (int from)
{
    from = (from == 1) ? 1 : 0;
    const int other = 1 - from;

    if (from == m_abSlot)
    {
        // Recording the live slot first, so the copy carries what is on screen
        // and not what was there when the slot was entered.
        m_ab[from] = m_source->snapshotParams();
    }
    else if (! m_ab[from].valid)
    {
        // Copying from a slot that has never been written: seed it from the
        // live state.  Otherwise the operation would blank the other slot with
        // an empty snapshot, which reads as "my parameters disappeared".
        m_ab[from] = m_source->snapshotParams();
    }

    m_ab[other] = m_ab[from];

    // Copying onto the active slot must also replace the live sound.  Merely
    // updating its snapshot made B → A appear to do nothing while A was active.
    if (other == m_abSlot)
    {
        m_source->restoreParams (m_ab[other]);
        for (auto* pp : m_paramList) pp->poll();
        ++m_paramRevision;
        emit paramRevisionChanged (true, true);
    }
    emit abChanged();
}

QString OvEngine::spectrumAt (int i) const
{
    if (i < 0 || i >= m_spectrum.size()) return QStringLiteral ("0");
    return QString::number (m_spectrum.at (i).toDouble(), 'f', 4);
}
