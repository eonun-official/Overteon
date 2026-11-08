#include "OvEnv.h"
#include "OvEngine.h"
#include "OvQmlTypes.h"
#include "OvTick.h"

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
    }

    m_meters = m_source->meters();
    m_driveMode = m_source->driveModeIndex();
    m_spectrum = QVariantList();
    for (int i = 0; i < 128; ++i) m_spectrum << 0.0;

    refreshPresets();

    m_timer.setInterval (OvTiming::kPollMs);   // parameters + spectrum
    connect (&m_timer, &QTimer::timeout, this, &OvEngine::tick);
    m_timer.start();

    // The light gets a clock of its own at the display rate.  Sharing the data
    // poll was the bug: a property bound to a value that only changes when a
    // measurement arrives cannot animate between measurements, so the accent
    // glow snapped from one brightness to the next and looked inert.
    m_lightTimer.setInterval (1000 / 60);
    connect (&m_lightTimer, &QTimer::timeout, this, &OvEngine::lightTick);
    m_lightTimer.start();
}

OvEngine::~OvEngine() = default;

// ---------------------------------------------------------------------------
//  The light behind the accent glow.
//
//  Same ballistics as the bars and the spectrum — an instant attack, and a
//  release whose per-frame step is derived from the refresh rate:
//
//      n_frames = rate * decay_seconds
//      step     = 1 - (target/start)^(1/n_frames)
//
//  (SpectrumDecayer::setDecaySpeed in ZLEqualizer.)  In the dB domain, because
//  the mapping to 0..1 is linear in dB and a light should fade at a constant
//  number of dB per second.
//
//  Faster than the bars: a glow that lags behind the music reads as broken
//  rather than as smooth.  120 ms to fall the full scale.
// ---------------------------------------------------------------------------
void OvEngine::lightTick()
{
    using clk = std::chrono::steady_clock;
    const auto now = clk::now();
    double dt = 1.0 / 60.0;
    if (m_lightAt != clk::time_point())
        dt = std::clamp (std::chrono::duration<double> (now - m_lightAt).count(),
                         0.0005, 0.20);
    m_lightAt = now;

    auto step = [dt] (double shown, double target) -> double
    {
        if (target >= shown) return target;                 // instant attack

        constexpr double kDecaySec = 0.12;
        constexpr double kFloorDb  = -60.0;

        const double rate    = (dt > 1e-6) ? 1.0 / dt : 60.0;
        const double nFrames = std::max (1.0, rate * kDecaySec);

        const double startDb   = shown  * 60.0 - 60.0;
        const double targetDb  = target * 60.0 - 60.0;
        const double startDist = std::max (1e-6, startDb - kFloorDb);
        const double goalDist  = std::max (0.0,   targetDb - kFloorDb);
        const double ratio     = std::clamp (goalDist / startDist, 0.0, 1.0);
        const double st        = std::clamp (1.0 - std::pow (ratio, 1.0 / nFrames), 0.0, 1.0);

        const double nextDb = startDb + (startDb - kFloorDb) * -st;
        return std::clamp ((std::max (nextDb, targetDb) + 60.0) / 60.0, 0.0, 1.0);
    };

    auto lvl = [] (double db) { return std::clamp ((db + 60.0) / 60.0, 0.0, 1.0); };

    const double targetOut = std::max (lvl (m_meters.outL), lvl (m_meters.outR));
    const double targetIn  = std::max (lvl (m_meters.inL),  lvl (m_meters.inR));

    const double nextOut = step (m_outLight, targetOut);
    const double nextIn  = step (m_inLight,  targetIn);

    // Threshold in the 0..1 domain: 0.002 is well under one pixel of glow.
    if (std::abs (nextOut - m_outLight) > 0.002 || std::abs (nextIn - m_inLight) > 0.002)
    {
        m_outLight = nextOut;
        m_inLight  = nextIn;
        emit lightChanged();
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
    // The threshold below is in display units, 0..1 over the plot's 85 dB
    // range, and the plot is ~900 px wide with 128 bins — one bin is about
    // 7 px, so 0.004 (0.34 dB) cannot move a pixel.  It was 0.0005, which the
    // FFT of a moving signal exceeds on almost every poll, so the plots
    // repainted at the full 30 Hz for changes nobody could see.
    if ((++m_tickCount % 2) == 0)
    {
        // Each tap is asked separately and each may have nothing new: a
        // 2048-point FFT at 48 kHz completes every 43 ms while a host can poll
        // at 15 Hz, so most polls find the buffer already consumed.  A tap that
        // reports no data leaves its table alone — writing zeros there made the
        // spectrum disappear and return, and every bounce tripped the change
        // threshold below, which repainted both plots each time.
        float specIn[128], specOut[128];
        const bool haveIn  = m_source->spectrumIn (specIn, 128);   // SC: enters
        const bool haveOut = m_source->spectrum (specOut, 128);    // PQ: leaves

        if (m_inSpectrum.size() != 128)
            m_inSpectrum = QVariantList (128, 0.0);

        bool changed = false;
        if (haveIn)
            for (int i = 0; i < 128; ++i)
                m_inSpectrum[i] = (double) specIn[i];

        if (haveOut)
            for (int i = 0; i < 128; ++i)
            {
                const double v = (double) specOut[i];
                if (std::abs (m_spectrum.at (i).toDouble() - v) > 0.004)
                {
                    m_spectrum[i] = v;
                    changed = true;
                }
            }
        // ONYXTEON_ENGDBG: what the poll actually saw, once a second.
        //
        // Without this, "the spectrum is not moving" has a dozen possible
        // causes that all look identical on screen.  These three counters
        // separate them: haveIn/haveOut say whether the source produced a new
        // FFT, changed says whether it differed enough to reach the UI.  All
        // three at zero means the audio never arrived; changed at zero with the
        // other two moving means the data is constant, which is what a muted
        // or unconnected input looks like.
        if (ovEnvFlag ("ONYXTEON_ENGDBG"))
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

void OvEngine::refreshPresets()
{
    const auto json = QString::fromStdString (m_source->presetListJson());
    QVariantMap out;
    const auto doc = QJsonDocument::fromJson (json.toUtf8());
    if (doc.isObject())
    {
        const auto obj = doc.object();
        for (auto it = obj.begin(); it != obj.end(); ++it)
        {
            QStringList names;
            for (const auto& v : it.value().toArray())
                names << v.toString();
            out.insert (it.key(), names);
        }
    }
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

QString OvEngine::spectrumAt (int i) const
{
    if (i < 0 || i >= m_spectrum.size()) return QStringLiteral ("0");
    return QString::number (m_spectrum.at (i).toDouble(), 'f', 4);
}
