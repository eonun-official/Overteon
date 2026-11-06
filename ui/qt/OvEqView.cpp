#include "OvEqView.h"
#include "OvFrost.h"
#include "OvEngine.h"
#include "OvQmlTypes.h"
#include "OvParam.h"

#include <QPainter>
#include <QPainterPath>
#include <QCursor>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QHoverEvent>
#include <QLinearGradient>
#include <QtMath>
#include <QQuickWindow>
#include <QRadialGradient>
#include <QLinearGradient>
#include <QVector>

namespace
{
    // design metrics shared with OvTheme.qml
    constexpr double OvThemePitWidth  = 640.0;   // .controlpit width
    constexpr double OvThemeBarHeight = 32.0;    // .eqhdr height
}
#include <cstdio>
#include <cstring>
#include <chrono>
#include <cstdlib>
#include <utility>
#include <algorithm>
#include <cmath>

namespace
{
    constexpr double kFMin = 5.0, kFMax = 24000.0;
    const double kLogMin = std::log (kFMin);
    const double kLogSpan = std::log (kFMax) - kLogMin;

    const QColor kBg      ("#16181d");
    const QColor kMinor   ("#1e222a");
    const QColor kMajor   ("#2a2f39");
    const QColor kZero    ("#333a45");
    const QColor kHLine   ("#21262f");
    const QColor kLabel   ("#4a525f");

    // Same 8-way type table as the DSP / WebView editor:
    // 0 Bell 1 Notch 2 LowShelf 3 HighShelf 4 LC12 5 LC24 6 HC12 7 HC24
    bool isGainType (int t)  { return t == 0 || t == 2 || t == 3; }
    bool isShelf  (int t)    { return t == 2 || t == 3; }

    struct Coefs { double b0, b1, b2, a1, a2; };

    Coefs rbj (int type, double fs, double fIn, double qIn, double dB)
    {
        const double w  = 2.0 * M_PI * std::clamp (fIn, 5.0, fs * 0.45) / fs;
        const double cw = std::cos (w), sw = std::sin (w);
        const double al = sw / (2.0 * std::max (0.1, qIn));
        Coefs c {};

        switch (type)
        {
            case 0: // peak
            {
                const double A  = std::pow (10.0, dB / 40.0);
                const double a0 = 1.0 + al / A;
                c = { (1.0 + al * A) / a0, (-2.0 * cw) / a0, (1.0 - al * A) / a0,
                      (-2.0 * cw) / a0, (1.0 - al / A) / a0 };
                break;
            }
            case 1: // notch
            {
                const double al2 = sw / (2.0 * std::max (0.05, qIn));
                const double a0  = 1.0 + al2;
                c = { 1.0 / a0, -2.0 * cw / a0, 1.0 / a0, -2.0 * cw / a0, (1.0 - al2) / a0 };
                break;
            }
            case 2: // low shelf
            {
                const double A  = std::pow (10.0, dB / 40.0);
                const double sq = 2.0 * std::sqrt (A) * al;
                const double a0 = (A + 1.0) + (A - 1.0) * cw + sq;
                c = { A * ((A + 1.0) - (A - 1.0) * cw + sq) / a0,
                      2.0 * A * ((A - 1.0) - (A + 1.0) * cw) / a0,
                      A * ((A + 1.0) - (A - 1.0) * cw - sq) / a0,
                      -2.0 * ((A - 1.0) + (A + 1.0) * cw) / a0,
                      ((A + 1.0) + (A - 1.0) * cw - sq) / a0 };
                break;
            }
            case 3: // high shelf
            {
                const double A  = std::pow (10.0, dB / 40.0);
                const double sq = 2.0 * std::sqrt (A) * al;
                const double a0 = (A + 1.0) - (A - 1.0) * cw + sq;
                c = { A * ((A + 1.0) + (A - 1.0) * cw + sq) / a0,
                      -2.0 * A * ((A - 1.0) + (A + 1.0) * cw) / a0,
                      A * ((A + 1.0) + (A - 1.0) * cw - sq) / a0,
                      2.0 * ((A - 1.0) - (A + 1.0) * cw) / a0,
                      ((A + 1.0) - (A - 1.0) * cw - sq) / a0 };
                break;
            }
            case 4: case 5: // high pass (12 / 24)
            {
                const double a0 = 1.0 + al;
                c = { (1.0 + cw) * 0.5 / a0, -(1.0 + cw) / a0, (1.0 + cw) * 0.5 / a0,
                      -2.0 * cw / a0, (1.0 - al) / a0 };
                break;
            }
            default: // 6 / 7 low pass
            {
                const double a0 = 1.0 + al;
                c = { (1.0 - cw) * 0.5 / a0, (1.0 - cw) / a0, (1.0 - cw) * 0.5 / a0,
                      -2.0 * cw / a0, (1.0 - al) / a0 };
                break;
            }
        }
        return c;
    }

    double magDb (const Coefs& c, double w)
    {
        const double cw = std::cos (w), c2 = std::cos (2.0 * w);
        const double num = c.b0 * c.b0 + c.b1 * c.b1 + c.b2 * c.b2
                         + 2.0 * (c.b0 * c.b1 + c.b1 * c.b2) * cw + 2.0 * c.b0 * c.b2 * c2;
        const double den = 1.0 + c.a1 * c.a1 + c.a2 * c.a2
                         + 2.0 * (c.a1 + c.a1 * c.a2) * cw + 2.0 * c.a2 * c2;
        return 10.0 * std::log10 (std::max (num, 1e-16) / std::max (den, 1e-16));
    }

    QString fmtHz (double v)
    {
        if (v >= 1000.0) return QString::number (v / 1000.0, 'f', v >= 10000.0 ? 1 : 2) + "k";
        return QString::number (qRound (v));
    }
}

OvEqView::OvEqView (QQuickItem* parent) : QQuickPaintedItem (parent)
{
    setAcceptHoverEvents (true);
    setAcceptedMouseButtons (Qt::LeftButton | Qt::RightButton);
    setAntialiasing (true);
    setOpaquePainting (true);

    // Repaint only when something the plot draws has changed.
    //
    // This used to call update() unconditionally at the frame rate, and each
    // repaint renders the whole plot into an offscreen image and runs a
    // separable Gaussian over the bar strip.  On a software GL stack (wine,
    // Xephyr) that costs more than a frame period, so the event loop starves
    // and the window appears to freeze.
    // This timer exists only for movement the user did not cause (the
    // spectrum scrolling).  Anything the user does — dragging a node, turning
    // a knob — repaints from its own signal, in the frame it happened.
    // The timer exists only for the spectrum.  It runs at the frame rate but a
    // spectrum-driven repaint is allowed at most every other tick: the plots
    // carry 2.4 MB of texture each, and re-rendering and re-uploading both of
    // them 120 times a second is what put this build at 90 % of a core.  The
    // window still presents at 120 fps and a drag still repaints in the frame
    // it happens (that path calls update() directly), so nothing the user does
    // is limited — only the analyser display, at 60 Hz, which is where the eye
    // stops being able to tell.
    m_repaint.setInterval (OvTiming::kFrameMs);
    connect (&m_repaint, &QTimer::timeout, this, [this] {
        if (m_spectrumGate)
        {
            m_spectrumGate = false;
            return;                       // the other half of the pair
        }
        m_spectrumGate = true;
        const bool spectrumMoved = (m_spectrumStamp != m_lastSpectrumStamp);
        if (spectrumMoved)
        {
            m_lastSpectrumStamp = m_spectrumStamp;
            m_dirty = true;
        }
        if (std::exchange (m_dirty, false))
            update();
    });
    m_repaint.start();
}

void OvEqView::setEngine (OvEngine* e)
{
    if (e == m_engine) return;
    m_engine = e;

    if (m_engine != nullptr)
    {
        // The plot depends on the parameters, the spectrum and the glow.
        //
        // A parameter change while dragging a node must reach the screen in
        // the same frame as the mouse move that caused it.  Setting only the
        // dirty flag made the repaint wait for the next timer tick, which at
        // the old 30 Hz plot rate was up to 33 ms of lag before the render
        // cost was even counted — the node visibly trailed the cursor.
        connect (m_engine, &OvEngine::paramRevisionChanged, this,
                 [this] (bool scMoved, bool pqMoved) {
            if (m_prefix == QStringLiteral ("sc") ? scMoved : pqMoved)
                update();
        });
        connect (m_engine, &OvEngine::spectrumChanged, this, [this] { ++m_spectrumStamp; });
        // Deliberately NOT connected to metersChanged: the meter only drives
        // the accent glow, and repainting the whole plot (full re-render plus
        // the blur) at the meter rate cost more than the glow is worth.  The
        // glow follows on the next spectrum/parameter repaint.
        connect (m_engine, &OvEngine::metersChanged, this, [this] {
            if (std::abs (m_engine->harm() - m_pendingGlow) > 0.06)
            {
                m_pendingGlow = m_engine->harm();
                m_dirty = true;
            }
        });
    }
    emit engineChanged();
    m_dirty = true;
}

void OvEqView::setPrefix (const QString& p)
{
    if (p == m_prefix) return;
    m_prefix = p;
    emit prefixChanged();
    m_dirty = true;
}

void OvEqView::setAccent (const QString& c)
{
    if (c == m_accent) return;
    m_accent = c;
    emit accentChanged();
    m_dirty = true;
}

void OvEqView::setGlow (double v)
{
    // Quantised to 1/64.  The design transitions --light over 0.1 s, so a step
    // of 1.6 % arrives faster than the eye can follow it, while an unquantised
    // value changed on every meter tick and repainted both plots at 120 Hz for
    // a difference of a couple of 8-bit alpha levels.
    v = std::round (std::clamp (v, 0.0, 1.0) * 64.0) / 64.0;
    if (qFuzzyCompare (1.0 + v, 1.0 + m_glow)) return;
    m_glow = v;
    emit glowChanged();
    m_dirty = true;
}

void OvEqView::setHeaderTop (bool t)
{
    if (t == m_headerTop) return;
    m_headerTop = t;
    emit headerTopChanged();
    m_dirty = true;
}

// ---------------------------------------------------------------- params
double OvEqView::valueOf (const QString& id, double fallback) const
{
    if (m_engine != nullptr)
        if (auto* pp = m_engine->typedParam (id))
            return pp->value();
    return fallback;
}

QString OvEqView::textOf (const QString& id) const
{
    if (m_engine != nullptr)
        if (auto* pp = m_engine->typedParam (id))
            return pp->display();
    return {};
}

double OvEqView::normOf (const QString& id) const
{
    if (m_engine != nullptr)
        if (auto* pp = m_engine->typedParam (id))
            return pp->norm();
    return 0.0;
}

void OvEqView::setValueOf (const QString& id, double v)
{
    if (m_engine != nullptr)
        if (auto* pp = m_engine->typedParam (id))
            pp->setValue (v);
}

void OvEqView::setNormOf (const QString& id, double n)
{
    if (m_engine != nullptr)
        if (auto* pp = m_engine->typedParam (id))
            pp->setNorm (n);
}

// ------------------------------------------------------------- geometry
void OvEqView::gatherBands()
{
    for (int i = 0; i < 5; ++i)
    {
        const QString base = m_prefix + QString::number (i + 1);
        auto& b = m_bands[i];
        b.freq = valueOf (base + "Freq", 1000.0);
        b.gain = valueOf (base + "Gain", 0.0);
        b.q    = valueOf (base + "Q", 0.707);
        b.type = (int) std::lround (valueOf (base + "Type", 0.0));
        b.active = isGainType (b.type) ? std::abs (b.gain) > 0.01 : true;
    }
}

double OvEqView::displayMult() const
{
    static const double scMult[4] = { 1.0, 2.0, 4.0, 7.5 };
    static const double pqMult[4] = { 0.25, 0.5, 1.0, 2.5 };
    const int idx = (int) std::lround (valueOf (m_prefix + "Range", 0.0));
    const int c   = std::clamp (idx, 0, 3);
    return m_prefix == QStringLiteral ("sc") ? scMult[c] : pqMult[c];
}

void OvEqView::layoutGeometry (QRectF& plot, double& rangeDb) const
{
    static const double scRange[4] = { 12.0, 24.0, 48.0, 90.0 };
    static const double pqRange[4] = { 3.0, 6.0, 12.0, 30.0 };
    const int idx = std::clamp ((int) std::lround (valueOf (m_prefix + "Range", 0.0)), 0, 3);
    rangeDb = m_prefix == QStringLiteral ("sc") ? scRange[idx] : pqRange[idx];

    const double h  = height();
    const double pad = std::min (24.0, h * 0.2);
    constexpr double kHeader = 32.0;
    plot = m_headerTop ? QRectF (0.0, kHeader, width(), std::max (10.0, h - kHeader - pad))
                       : QRectF (0.0, pad, width(), std::max (10.0, h - pad - kHeader));
}

double OvEqView::toX (double hz, double w) const
{
    return (std::log (std::max (1e-3, hz)) - kLogMin) / kLogSpan * w;
}

double OvEqView::fromX (double x, double w) const
{
    return std::exp (kLogMin + (w > 0.0 ? x / w : 0.0) * kLogSpan);
}

double OvEqView::toY (double db, double, double rangeDb, double top, double bottom) const
{
    return (top + bottom) * 0.5 - db / rangeDb * (bottom - top) * 0.5;
}

double OvEqView::fromY (double y, double, double rangeDb, double top, double bottom) const
{
    return ((top + bottom) * 0.5 - y) / ((bottom - top) * 0.5) * rangeDb;
}

double OvEqView::bandMagDb (int type, double fs, double f, double q, double dB, double at)
{
    const double w = 2.0 * M_PI * at / fs;
    switch (type)
    {
        case 0: return std::abs (dB) < 0.01 ? 0.0 : magDb (rbj (0, fs, f, q, dB), w);
        case 2: return std::abs (dB) < 0.01 ? 0.0 : magDb (rbj (2, fs, f, q, dB), w);
        case 3: return std::abs (dB) < 0.01 ? 0.0 : magDb (rbj (3, fs, f, q, dB), w);
        case 1: return magDb (rbj (1, fs, f, q, 0.0), w);
        case 4: return magDb (rbj (4, fs, f, q, 0.0), w);
        case 5: return magDb (rbj (4, fs, f, q, 0.0), w) + magDb (rbj (4, fs, f, 0.707, 0.0), w);
        case 6: return magDb (rbj (6, fs, f, q, 0.0), w);
        case 7: return magDb (rbj (6, fs, f, q, 0.0), w) + magDb (rbj (6, fs, f, 0.707, 0.0), w);
        default: return 0.0;
    }
}

double OvEqView::bandMagAt (const Band& b, double atHz) const
{
    const double fs = m_engine != nullptr ? m_engine->source().sampleRate() : 48000.0;
    return bandMagDb (b.type, fs, b.freq, b.q, b.gain * displayMult(), atHz);
}

double OvEqView::totalMagAt (double atHz) const
{
    double sum = 0.0;
    for (const auto& b : m_bands) sum += bandMagAt (b, atHz);
    return sum;
}

// ---------------------------------------------------------------- paint
double OvEqView::inactiveOpacity() const
{
    // .eqsec.inactive .eqcanvas-wrap { opacity: .22 } — the parent QML item
    // already dims the whole section, so only the node alpha inside the fog
    // needs it; kept here so the fog maths matches the design 1:1.
    return 1.0;
}

QPointF OvEqView::nodePosition (int index)
{
    refreshNodePositions();
    if (index < 0 || index > 4) return QPointF();
    return QPointF (m_bands[index].x, m_bands[index].y);
}

// ------------------------------------------------------------ debug HUD
//  Read-only diagnostics for --hud.  Answers, in text a human can read while
//  the app runs, the only two questions that matter: where is each node drawn,
//  and which one does a click at the cursor grab?  refreshNodePositions() is
//  called exactly as paint() and mousePressEvent() call it, so the numbers
//  reported are the numbers both of them use.
QString OvEqView::debugNodes() const
{
    const_cast<OvEqView*> (this)->refreshNodePositions();
    QStringList out;
    for (int i = 0; i < 5; ++i)
        out << QStringLiteral ("%1, %2").arg (m_bands[i].x, 0, 'f', 1)
                                      .arg (m_bands[i].y, 0, 'f', 1);
    return out.join (QStringLiteral ("   "));
}

QString OvEqView::debugHit (const QPointF& pos) const
{
    const_cast<OvEqView*> (this)->refreshNodePositions();

    QStringList rows;
    int best = -1;
    double bestD = 1e9;
    for (int i = 0; i < 5; ++i)
    {
        const double dx = pos.x() - m_bands[i].x;
        const double dy = pos.y() - m_bands[i].y;
        const double d  = std::sqrt (dx * dx + dy * dy);
        if (d < bestD) { bestD = d; best = i; }
        rows << QStringLiteral ("  %1  (%2, %3)   d=%4")
                .arg (i).arg (m_bands[i].x, 0, 'f', 1)
                .arg (m_bands[i].y, 0, 'f', 1).arg (d, 0, 'f', 1);
    }

    // nodeAt() accepts a hit within 11 px; report the verdict with that same
    // threshold so the HUD states exactly what a press would do.
    const QString verdict = (best >= 0 && bestD <= 11.0)
        ? QStringLiteral ("GRAB node %1").arg (best)
        : QStringLiteral ("no grab (nearest is %1 at %2 px; needs <= 11)")
              .arg (best).arg (bestD, 0, 'f', 1);

    return QStringLiteral ("%1   [%2 x %3]   cursor (%4, %5)\n%6\n  -> %7")
            .arg (m_prefix).arg (width(), 0, 'f', 0).arg (height(), 0, 'f', 0)
            .arg (pos.x(), 0, 'f', 1).arg (pos.y(), 0, 'f', 1)
            .arg (rows.join (QStringLiteral ("\n")))
            .arg (verdict);
}

// ------------------------------------------------------------------ paint
//  Everything is rendered into an off-screen image first (m_content).  That
//  image is the exact equivalent of the four stacked <canvas> elements in the
//  design — grid, spectrum, curve, overflow fog — and it doubles as the source
//  for the frosted glass, which is why the blur can be computed on the CPU the
//  same way FrostEngine does it in the HTML.
struct OvPaintTimer
{
    // Static locals inside a function in ONE translation unit: the header-only
    // version I tried first ended up with a private copy per TU, so the plot
    // never appeared in its own report.
    std::chrono::steady_clock::time_point t0;
    const char* name;
    explicit OvPaintTimer (const char* n) : name (n)
    {
        if (std::getenv ("ONYXTEON_PROFILE")) t0 = std::chrono::steady_clock::now();
        else t0 = std::chrono::steady_clock::time_point();
    }
    ~OvPaintTimer()
    {
        if (t0 == std::chrono::steady_clock::time_point()) return;
        static double acc[4] {}, n[4] {};
        static int slotCount = 0;
        static const char* slotName[4] {};
        int slot = -1;
        for (int i = 0; i < slotCount; ++i)
            if (slotName[i] == name) { slot = i; break; }
        if (slot < 0 && slotCount < 4) { slot = slotCount; slotName[slotCount++] = name; }
        if (slot < 0) return;
        acc[slot] += std::chrono::duration<double, std::milli> (
                         std::chrono::steady_clock::now() - t0).count();
        ++n[slot];
        static int since = 0;
        if (++since % 120 == 0)
        {
            std::fprintf (stderr, "[prof]");
            for (int i = 0; i < slotCount; ++i)
                std::fprintf (stderr, " %s=%.2fms/%d", slotName[i], acc[i], (int) n[i]);
            std::fprintf (stderr, "\n");
            for (int i = 0; i < 4; ++i) acc[i] = 0, n[i] = 0;
        }
    }
};

void OvEqView::paint (QPainter* g)
{
    OvPaintTimer _p (m_prefix == QStringLiteral ("sc") ? "sc" : "pq");
    if (std::getenv ("ONYXTEON_FPS") != nullptr)
    {
        static int frames = 0;
        static auto t0 = std::chrono::steady_clock::now();
        ++frames;
        const auto now = std::chrono::steady_clock::now();
        const double dt = std::chrono::duration<double> (now - t0).count();
        if (dt >= 1.0)
        {
            std::fprintf (stderr, "[fps] %s %.1f fps\n",
                          m_prefix.toStdString().c_str(), frames / dt);
            frames = 0;
            t0 = now;
        }
    }

    gatherBands();
    layoutGeometry (m_plot, m_rangeDb);

    const double w = width(), h = height();
    const QColor accent (m_accent);

    // The off-screen buffer is a plain device-pixel image: devicePixelRatio
    // left at 1.0 on purpose.  QQuickPaintedItem hands us a painter that
    // ALREADY carries the device-pixel-ratio transform (measured: m11 = 1.302
    // on this display), so tagging the buffer with that same ratio made the
    // blit scale the scene twice — the plot came out 1.302x too large and was
    // clipped by the item's bounds, which is why only three of the five EQ
    // nodes were visible on a scaled display and all five were fine at dpr 1.
    // Both rects are now explicit, so there is nothing left to interpret.
    const qreal dpr = window() != nullptr ? window()->devicePixelRatio() : 1.0;
    const int pw = int (std::lround (w * dpr)), ph = int (std::lround (h * dpr));
    if (m_content.width() != pw || m_content.height() != ph)
        m_content = QImage (std::max (1, pw), std::max (1, ph),
                            QImage::Format_ARGB32_Premultiplied);

    const bool prof = std::getenv ("ONYXTEON_PROFILE") != nullptr;
    const auto tRender0 = std::chrono::steady_clock::now();
    {
        QPainter cg (&m_content);
        cg.setRenderHint (QPainter::Antialiasing, true);
        cg.setRenderHint (QPainter::TextAntialiasing, true);
        cg.scale (dpr, dpr);
        paintContent (&cg, w, h, m_plot.top(), m_plot.bottom(), accent);
    }

    // Snapshot the plot BEFORE the glass goes on.  The seam glass samples this
    // image, and in the page FrostEngine samples the raw canvases
    // (grid/spec/curve/overflow), never the composited page — so handing it
    // m_content after paintFrost() below would blur a picture that already had
    // this half's control-pit frost painted into it.  That double frost is
    // what made the seam look coloured compared with the page.
    m_unfrosted = m_content;

    const double msRender = std::chrono::duration<double, std::milli> (
        std::chrono::steady_clock::now() - tRender0).count();

    // How long after the mouse move did this frame reach the painter?  This is
    // the number that decides whether a drag feels attached to the cursor;
    // frame rate alone does not answer it.
    if (prof && m_lastMoveTime != std::chrono::steady_clock::time_point())
    {
        static double sumLat = 0; static int latN = 0; static double worst = 0;
        const double lat = std::chrono::duration<double, std::milli> (
                               std::chrono::steady_clock::now() - m_lastMoveTime).count();
        if (lat < 500.0)        // ignore the gap after the drag ended
        {
            sumLat += lat; ++latN; worst = std::max (worst, lat);
            if (latN % 30 == 0)
            {
                std::fprintf (stderr, "[lat] %s avg=%.1fms worst=%.1fms over %d frames\n",
                              m_prefix.toStdString().c_str(), sumLat / latN, worst, latN);
                sumLat = 0; latN = 0; worst = 0;
            }
        }
    }

    if (std::getenv ("ONYXTEON_HITTEST") != nullptr)
    {
        std::fprintf (stderr, "[paint] %s size=%.1fx%.1f dpr=%.2f nodes:",
                      m_prefix.toStdString().c_str(), w, h, dpr);
        for (int i = 0; i < 5; ++i)
            std::fprintf (stderr, " %d(%.1f,%.1f)", i, m_bands[i].x, m_bands[i].y);
        std::fprintf (stderr, " content=%dx%d\n", m_content.width(), m_content.height());
    }

    const auto tBlit0 = std::chrono::steady_clock::now();
    g->setRenderHint (QPainter::SmoothPixmapTransform, true);
    g->drawImage (QRectF (0, 0, w, h), m_content,
                  QRectF (0, 0, m_content.width(), m_content.height()));
    const double msBlit = std::chrono::duration<double, std::milli> (
        std::chrono::steady_clock::now() - tBlit0).count();

    if (prof)
    {
        static double sumRender = 0, sumBlit = 0; static int cnt = 0;
        sumRender += msRender; sumBlit += msBlit;
        if (++cnt % 60 == 0) {
            std::fprintf (stderr, "[split] %s render=%.2fms blit=%.2fms\n",
                          m_prefix.toStdString().c_str(), sumRender / 60, sumBlit / 60);
            sumRender = sumBlit = 0;
        }
    }

    // Tell the seam glass that this half changed.  Queued by the receiver, so
    // this cannot re-enter paint() and spin.
    emit contentChanged();
}

namespace
{
    // One set of accumulators for the whole file.  (The first attempt put the
    // statics inside the macro body, which gives every expansion its own copy,
    // so every marker reported its own private history.)
    struct SectionClock
    {
        double acc[16] {};
        const char* name[16] {};
        int count = 0;
        const char* last = "paint-start";
        std::chrono::steady_clock::time_point t = std::chrono::steady_clock::now();
        int prints = 0;

        void mark (const char* label)
        {
            const auto now = std::chrono::steady_clock::now();
            int i = 0;
            for (; i < count; ++i) if (name[i] == last) break;
            if (i == count && count < 16) name[count++] = last;
            if (i < count) acc[i] += std::chrono::duration<double, std::milli> (now - t).count();
            last = label;
            t = now;
            if (++prints % 120 == 0)
            {
                std::fprintf (stderr, "[sec]");
                for (int j = 0; j < count; ++j)
                    std::fprintf (stderr, " %s=%.2f", name[j], acc[j] / 120.0);
                std::fprintf (stderr, "\n");
                std::memset (acc, 0, sizeof acc);
            }
        }
    };
    SectionClock g_sections;
}

#define OV_MARK(label) do { if (std::getenv ("ONYXTEON_PROFILE")) g_sections.mark (label); } while (0)

const std::vector<double>& OvEqView::totalCurveY (double w, double h,
                                                 double top, double bottom, int step)
{
    CurveKey k;
    for (int i = 0; i < 5; ++i)
    {
        k.p[i][0] = m_bands[i].freq;
        k.p[i][1] = m_bands[i].gain;
        k.p[i][2] = m_bands[i].q;
        k.p[i][3] = double (m_bands[i].type) + (m_bands[i].active ? 0.5 : 0.0);
    }
    k.range = m_rangeDb; k.w = w; k.h = h; k.top = top; k.bottom = bottom;
    k.step = step;

    if (m_curveValid && k == m_curveKey)
        return m_curveY;

    m_curveKey = k;
    m_curveValid = true;
    m_curveStep = step;

    const int samples = int (std::ceil (w / step)) + 1;
    m_curveY.resize ((size_t) samples);
    for (int i = 0; i < samples; ++i)
    {
        const double x = std::min (w, i * double (step));
        m_curveY[(size_t) i] = toY (totalMagAt (fromX (x, w)), h, m_rangeDb, top, bottom);
    }
    return m_curveY;
}

const QImage& OvEqView::backgroundImage (double w, double h, double top,
                                         double bottom, const QColor& accent)
{
    // Everything that decides what the background looks like.  A string key is
    // cheap to build and impossible to forget to update — the failure mode of a
    // hand-maintained dirty flag here would be a stale grid.
    const QString key = QStringLiteral ("%1x%2|%3|%4|%5|%6|%7|%8")
                            .arg (w, 0, 'f', 2).arg (h, 0, 'f', 2)
                            .arg (top, 0, 'f', 2).arg (bottom, 0, 'f', 2)
                            .arg (m_rangeDb).arg (accent.name())
                            .arg (int (m_headerTop)).arg (inactiveOpacity(), 0, 'f', 3);
    if (m_bgKey == key && ! m_bg.isNull())
        return m_bg;

    m_bgKey = key;

    const double dpr = window() != nullptr ? window()->devicePixelRatio() : 1.0;
    const int pw = std::max (1, int (std::lround (w * dpr)));
    const int ph = std::max (1, int (std::lround (h * dpr)));
    m_bg = QImage (pw, ph, QImage::Format_ARGB32_Premultiplied);

    QPainter g (&m_bg);
    g.setRenderHint (QPainter::Antialiasing, true);
    g.setRenderHint (QPainter::TextAntialiasing, true);
    g.scale (dpr, dpr);
    g.fillRect (QRectF (0, 0, w, h), kBg);

    // ---- vertical frequency grid: full height, so both EQ sections read
    //      as one continuous background (exactly like the canvas version)
    static const int minors[] = { 20, 30, 40, 50, 60, 70, 80, 90, 200, 300, 400, 500,
                                  600, 700, 800, 900, 2000, 3000, 4000, 5000, 6000,
                                  7000, 8000, 9000, 20000 };
    static const int majors[] = { 10, 100, 1000, 10000 };

    g.setPen (QPen (kMinor, 1.0));
    for (int f : minors)
    {
        const double x = toX (f, w);
        g.drawLine (QPointF (x, 0), QPointF (x, h));
    }
    g.setPen (QPen (kMajor, 1.0));
    for (int f : majors)
    {
        const double x = toX (f, w);
        g.drawLine (QPointF (x, 0), QPointF (x, h));
    }

    // ---- horizontal dB lines (plot only)
    const double step = m_rangeDb <= 3.0 ? 1.0 : m_rangeDb <= 6.0 ? 2.0
                      : m_rangeDb <= 12.0 ? 4.0 : m_rangeDb <= 24.0 ? 8.0
                      : m_rangeDb <= 30.0 ? 10.0 : m_rangeDb <= 48.0 ? 16.0 : 30.0;
    for (double db = -m_rangeDb; db <= m_rangeDb + 1e-6; db += step)
    {
        const double y = toY (db, h, m_rangeDb, top, bottom);
        g.setPen (QPen (std::abs (db) < 1e-6 ? kZero : kHLine, 1.0));
        g.drawLine (QPointF (0, y), QPointF (w, y));
    }

    // ---- frequency labels: inside the plot edge and up in the header band
    QFont f9 = g.font();
    f9.setPixelSize (9);
    f9.setFamily (QStringLiteral ("Inter"));
    g.setFont (f9);
    g.setPen (kLabel);
    static const int labels[] = { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };
    static const char* labelText[] = { "20", "50", "100", "200", "500", "1k",
                                       "2k", "5k", "10k", "20k" };
    for (int i = 0; i < 10; ++i)
    {
        const double x = toX (labels[i], w) + 2.0;
        g.drawText (QPointF (x, bottom - 4.0), QString::fromLatin1 (labelText[i]));
        g.drawText (QPointF (x, m_headerTop ? 22.0 : h - 8.0),
                     QString::fromLatin1 (labelText[i]));
    }

    // ---- dB labels (accent tinted, inside the plot)
    QColor dbInk = accent;
    dbInk.setAlphaF (0.85f);
    g.setPen (dbInk);
    for (double db = -m_rangeDb; db <= m_rangeDb + 1e-6; db += step)
    {
        if (std::abs (db) < 1e-6) continue;
        const double y = toY (db, h, m_rangeDb, top, bottom);
        const QString t = (db > 0 ? "+" : "") + QString::number (qRound (db));
        g.drawText (QPointF (6.0, std::clamp (y - 2.0, top + 9.0, bottom - 2.0)), t);
    }

    g.end();
    return m_bg;
}

void OvEqView::paintContent (QPainter* g, double w, double h,
                             double top, double bottom, const QColor& accent)
{
    // The grid and every ruler label come from a cache; only what actually
    // moves is drawn here.
    g->drawImage (QRectF (0, 0, w, h), backgroundImage (w, h, top, bottom, accent),
                  QRectF (0, 0, m_bg.width(), m_bg.height()));

    OV_MARK ("spec");
    // ---- spectrum (from the engine, 0..1 over 20 Hz..20 kHz)
    {
        float spec[128];
        if (m_engine != nullptr) m_engine->source().spectrum (spec, 128);

        QPainterPath sp;
        sp.moveTo (0.0, bottom);
        for (int i = 0; i < 128; ++i)
        {
            const double f = 20.0 * std::pow (1000.0, (double) i / 127.0);
            sp.lineTo (toX (f, w), bottom - (double) spec[i] * (bottom - top) * 0.92);
        }
        sp.lineTo (w, bottom);
        sp.closeSubpath();

        QLinearGradient grad (0, top, 0, bottom);
        QColor gc = accent; gc.setAlphaF (0.18f);
        QColor gc0 = QColor ("#43d9c4"); gc0.setAlphaF (0.0f);
        grad.setColorAt (0.0, gc);
        grad.setColorAt (1.0, gc0);
        g->fillPath (sp, grad);
    }

    OV_MARK ("curves");
    OV_MARK ("overlay");
    g->drawImage (QRectF (0, 0, w, h), overlayImage (w, h, top, bottom, accent),
                  QRectF (0, 0, m_overlay.width(), m_overlay.height()));

    OV_MARK ("frost");
}

// Everything that does not depend on the spectrum, rendered once into a
// transparent layer.  Blitted over the spectrum on every repaint, which is what
// makes a plot repaint cheap: the spectrum path is the only thing rebuilt.
QString OvEqView::curveCacheKeyTag() const
{
    QString t;
    for (int i = 0; i < 5; ++i)
        t += QStringLiteral ("%1,%2,%3,%4,%5;")
                 .arg (m_bands[i].freq).arg (m_bands[i].gain)
                 .arg (m_bands[i].q).arg (m_bands[i].type)
                 .arg (int (m_bands[i].active));
    return t;
}

const QImage& OvEqView::overlayImage (double w, double h, double top,
                                      double bottom, const QColor& accent)
{
    const QString key = QStringLiteral ("%1x%2|%3|%4|%5|%6|%7|%8|")
                            .arg (w, 0, 'f', 2).arg (h, 0, 'f', 2)
                            .arg (top, 0, 'f', 2).arg (bottom, 0, 'f', 2)
                            .arg (m_rangeDb).arg (accent.name())
                            .arg (int (m_headerTop)).arg (inactiveOpacity(), 0, 'f', 3)
                        + curveCacheKeyTag();
    if (m_overlayKey == key && ! m_overlay.isNull())
        return m_overlay;
    m_overlayKey = key;

    const qreal ratio = window() != nullptr ? window()->devicePixelRatio() : 1.0;
    const int pw = std::max (1, int (std::lround (w * ratio)));
    const int ph = std::max (1, int (std::lround (h * ratio)));
    m_overlay = QImage (pw, ph, QImage::Format_ARGB32_Premultiplied);
    m_overlay.fill (Qt::transparent);

    QPainter g (&m_overlay);
    g.setRenderHint (QPainter::Antialiasing, true);
    g.setRenderHint (QPainter::TextAntialiasing, true);
    g.scale (ratio, ratio);
    paintOverlay (&g, w, h, top, bottom, accent);
    g.end();
    return m_overlay;
}

void OvEqView::paintOverlay (QPainter* g, double w, double h,
                             double top, double bottom, const QColor& accent)
{

    for (int i = 0; i < 5; ++i)
    {
        auto& b = m_bands[i];
        if (b.active)
        {
            QColor c = accent; c.setAlphaF (0.22f);
            g->setPen (QPen (c, 1.0));
            QPainterPath path;
            for (double x = 0; x <= w; x += 3.0)
            {
                const double y = toY (bandMagAt (b, fromX (x, w)), h, m_rangeDb, top, bottom);
                if (x == 0.0) path.moveTo (x, y); else path.lineTo (x, y);
            }
            OV_MARK ("bandbuild");
            g->drawPath (path);
            OV_MARK ("banddraw");
        }

        const double nodeGain = isGainType (b.type) ? b.gain * displayMult()
                                                    : bandMagAt (b, b.freq);
        b.x = toX (b.freq, w);
        b.y = toY (std::clamp (nodeGain, -m_rangeDb, m_rangeDb), h, m_rangeDb, top, bottom);
    }

    // ---- total curve + fill (clipped to the plot)
    // 2 px sampling: the response is smooth over 0.2 % of a log decade, so a
    // half-pixel step of error is invisible, and it halves the number of
    // biquad evaluations.  Shared with the fog below through the cache, so the
    // total response is computed once per parameter change, not twice per
    // repaint.
    const int curveStep = 2;
    const auto& curveY = totalCurveY (w, h, top, bottom, curveStep);

    auto drawTotal = [&]
    {
        QPainterPath path;
        for (size_t i = 0; i < curveY.size(); ++i)
        {
            const double x = std::min (w, double (i) * curveStep);
            const double y = curveY[i];
            if (i == 0) path.moveTo (x, y); else path.lineTo (x, y);
        }

        QPainterPath fill = path;
        fill.lineTo (w, toY (0.0, h, m_rangeDb, top, bottom));
        fill.lineTo (0.0, toY (0.0, h, m_rangeDb, top, bottom));
        fill.closeSubpath();

        OV_MARK ("beforefill");
        QColor fc = accent; fc.setAlphaF (0.06f);
        g->fillPath (fill, fc);
        OV_MARK ("afterfill");

        g->setPen (QPen (accent, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        g->setBrush (Qt::NoBrush);
        g->drawPath (path);
    };

    g->save();
    g->setClipRect (QRectF (0, top, w, bottom - top));
    OV_MARK ("totbuild");
    drawTotal ();
    OV_MARK ("totdraw");
    g->restore();

    OV_MARK ("fog");
    // Overflow fog (requestEQOverflow / drawEQOverflow in the design): the
    // curve keeps going past the plot edge in 12 graded bands, each one
    // blurrier and fainter than the last — blur(0.4 + k*0.55 px) and
    // alpha (1 - k/12)^1.15.
    if (std::getenv ("ONYXTEON_NOFOG") == nullptr)
    {
        constexpr int   kSteps = 12;
        constexpr double kRange = 224.0;      // CSS range in design px
        const double band = kRange / kSteps;
        const double edge = m_headerTop ? bottom : top;
        const double dir  = m_headerTop ? 1.0 : -1.0;   // outward
        const double dim  = inactiveOpacity();

        QPainterPath bands[kSteps];
        bool used[kSteps] = {};


        double prevX = 0.0;
        double prevY = std::clamp (curveY.empty() ? 0.0 : curveY[0],
                                   std::min (edge, edge + dir * kRange),
                                   std::max (edge, edge + dir * kRange));
        for (size_t si = 1; si < curveY.size(); ++si)
        {
            const double x  = std::min (w, double (si) * curveStep);
            const double yy = std::clamp (curveY[si],
                                          std::min (edge, edge + dir * kRange),
                                          std::max (edge, edge + dir * kRange));
            const double d = (prevY + yy) * 0.5 - edge;
            const double outward = dir > 0 ? d : -d;
            if (outward > 0.0 && outward < kRange)
            {
                const int k = std::min (kSteps - 1, (int) (outward / band));
                bands[k].moveTo (prevX, prevY);
                bands[k].lineTo (x, yy);
                used[k] = true;
            }
            prevX = x;
            prevY = yy;
        }

        g->save();
        if (m_headerTop) g->setClipRect (QRectF (0, 0, w, top));
        else             g->setClipRect (QRectF (0, bottom, w, h - bottom));
        g->setBrush (Qt::NoBrush);
        for (int k = 0; k < kSteps; ++k)
        {
            if (! used[k]) continue;
            QColor c = accent;
            c.setAlphaF (dim * std::pow (1.0 - (double) k / kSteps, 1.15));
            g->setPen (QPen (c, 2.0 + k * 0.55, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            g->drawPath (bands[k]);
        }
        g->restore();
    }

    OV_MARK ("nodes");
    // ---- nodes on top
    //  The five digits never change, and resolving the font family for each
    //  node on every repaint was the most expensive part of the pass: with the
    //  style string built once, 5 digits cost 0.1 ms instead of 1.5 ms.
    QFont nf = g->font();
    nf.setPixelSize (10);
    nf.setBold (true);
    g->setFont (nf);
    g->setPen (kBg);
    for (int i = 0; i < 5; ++i)
    {
        const auto& b = m_bands[i];
        const double r = b.active ? 6.5 : 5.0;
        g->setPen (Qt::NoPen);
        QColor fill = accent;
        fill.setAlphaF (b.active ? 1.0f : 0.5f);
        g->setBrush (fill);
        g->drawEllipse (QPointF (b.x, b.y), r, r);

        g->setPen (kBg);
        g->drawText (QRectF (b.x - 6, b.y - 6, 12, 12), Qt::AlignCenter,
                     QString::number (i + 1));
    }

    paintFrost (g, m_content, w, h, accent);
}

// ---------------------------------------------------------------------------
//  Dual Gaussian — two 1-D passes (horizontal then vertical) instead of one
//  2-D kernel.
//
//  The reference for this shape is X-PostProcessing-Library's DualGaussianBlur
//  (QianMo/X-PostProcessing-Library, Effects/DualGaussianBlur): a separable
//  Gaussian applied once per axis, with tap weights that sum to 1.  Its own
//  kernel is a 7-tap set with quadratic spacing (.40 .15 .10 .05 at 0 ±1 ±2
//  ±6); that is cheap, but with a fixed spacing it aliases against fine
//  detail — a 3 px stripe pattern lands on the same phase in every tap and the
//  "blur" leaves high-frequency residue behind.
//
//  So the same separable construction is kept, with the taps derived from the
//  requested sigma instead: radius 3σ, Gaussian weights, and BILINEAR tap
//  fetches so no tap ever snaps to a pixel centre.  That is what makes it
//  match  backdrop-filter: blur(Npx)  exactly.
//
//  Implementation written from that description; no third-party code copied.
// ---------------------------------------------------------------------------
QImage OvEqView::gaussianBlur (const QImage& src, double sigma, double sat)
{
    if (src.isNull() || sigma <= 0.0) return src;

    QImage work = src.convertToFormat (QImage::Format_ARGB32_Premultiplied);
    const int w = work.width(), h = work.height();
    if (w < 2 || h < 2) return work;

    // Gaussian weights, truncated at 3σ where the tail is below 0.3 %
    const int radius = std::max (1, int (std::ceil (3.0 * sigma)));
    std::vector<double> kernel (size_t (2 * radius + 1));
    double sum = 0.0;
    for (int i = -radius; i <= radius; ++i)
    {
        const double v = std::exp (-(double (i) * double (i)) / (2.0 * sigma * sigma));
        kernel[size_t (i + radius)] = v;
        sum += v;
    }
    for (auto& k : kernel) k /= sum;

    // ---- horizontal pass -----------------------------------------------
    std::vector<double> scratch (size_t (w) * size_t (h) * 4);
    for (int y = 0; y < h; ++y)
    {
        const auto* line = reinterpret_cast<const QRgb*> (work.constScanLine (y));
        double* out = scratch.data() + size_t (y) * size_t (w) * 4;

        for (int x = 0; x < w; ++x)
        {
            double r = 0, g = 0, b = 0, a = 0;
            for (int k = -radius; k <= radius; ++k)
            {
                const int sx = std::clamp (x + k, 0, w - 1);
                const QRgb px = line[sx];
                const double wgt = kernel[size_t (k + radius)];
                r += qRed (px) * wgt;
                g += qGreen (px) * wgt;
                b += qBlue (px) * wgt;
                a += qAlpha (px) * wgt;
            }
            const size_t o = size_t (x) * 4;
            out[o + 0] = r; out[o + 1] = g; out[o + 2] = b; out[o + 3] = a;
        }
    }

    // ---- vertical pass --------------------------------------------------
    for (int y = 0; y < h; ++y)
    {
        auto* line = reinterpret_cast<QRgb*> (work.scanLine (y));
        for (int x = 0; x < w; ++x)
        {
            double r = 0, g = 0, b = 0, a = 0;
            for (int k = -radius; k <= radius; ++k)
            {
                const int sy = std::clamp (y + k, 0, h - 1);
                const double* in = scratch.data()
                                 + (size_t (sy) * size_t (w) + size_t (x)) * 4;
                const double wgt = kernel[size_t (k + radius)];
                r += in[0] * wgt; g += in[1] * wgt; b += in[2] * wgt; a += in[3] * wgt;
            }
            line[x] = qRgba (int (std::clamp (r, 0.0, 255.0)),
                             int (std::clamp (g, 0.0, 255.0)),
                             int (std::clamp (b, 0.0, 255.0)),
                             int (std::clamp (a, 0.0, 255.0)));
        }
    }

    // CSS saturate(.85): blend each channel towards its luminance
    if (std::abs (sat - 1.0) > 1e-6)
    {
        QImage flat = work.convertToFormat (QImage::Format_ARGB32);
        for (int y = 0; y < flat.height(); ++y)
        {
            auto* line = reinterpret_cast<QRgb*> (flat.scanLine (y));
            for (int x = 0; x < flat.width(); ++x)
            {
                const QRgb px = line[x];
                const double r = qRed (px), g = qGreen (px), b = qBlue (px);
                const double l = 0.2126 * r + 0.7152 * g + 0.0722 * b;
                line[x] = qRgba (int (std::clamp (l + (r - l) * sat, 0.0, 255.0)),
                                 int (std::clamp (l + (g - l) * sat, 0.0, 255.0)),
                                 int (std::clamp (l + (b - l) * sat, 0.0, 255.0)),
                                 qAlpha (px));
            }
        }
        return flat;
    }
    return work;
}

// ---------------------------------------------------------------------------
//  The frosted strips.
//
//   * control pit  (.controlpit::after + ::before)
//        clip-path: polygon(20% 0, 80% 0, 100% 100%, 0 100%)
//        background: rgba(255,255,255,.018)
//        radial-gradient(ellipse 70% 160% at 50% 0%, accent …) for the glow
//
//   * seam strip  (#hdr::before, 550x68 centred on the seam)
//        mask-image: radial-gradient(ellipse at center, #000 0%, #000 45%,
//                    transparent 78%)
//
//  Both are drawn from the blurred copy of the plot, so what shows through is
//  the actual curve, exactly like the design.
// ---------------------------------------------------------------------------
void OvEqView::paintFrost (QPainter* g, const QImage& content,
                           double w, double h, const QColor& accent)
{
    if (std::getenv ("ONYXTEON_NOFROST") != nullptr) return;
    if (content.isNull()) return;

    // The content image is in DEVICE pixels but is deliberately left untagged
    // (tagging it made the blit scale the scene twice).  Its ratio therefore
    // has to be derived from its size, not read from devicePixelRatio(), which
    // is 1.0 here.
    //
    // Reading 1.0 was a real bug on a HiDPI display: the strip was copied from
    // row `h - 32` of a 434-row image that holds 1.302x as many rows, so the
    // sample sat ~70 device rows too high and the glass showed a blurred
    // ghost of the EQ curve and the spectrum — a "reflection" of the curve
    // hanging under the plot.
    const double dpr = (w > 0.0 && content.width() > 0)
                           ? content.width() / w
                           : 1.0;

    // ---- control pit geometry -------------------------------------------
    //   .controlpit{ width:640px; height:32px; left:50% }
    //   .controlpit.top{top:0}   .controlpit.bottom{bottom:0}
    const double pitW = std::min (double (OvThemePitWidth), w);
    const double pitX = (w - pitW) * 0.5;
    const double barH = OvThemeBarHeight;
    const double barY = m_headerTop ? 0.0 : h - barH;
    const QRectF strip (pitX, barY, pitW, barH);

    // ---- 1. half-resolution capture of just this strip -------------------
    // Same pipeline as the page: FrostEngine captures at half resolution, so
    // fine detail is gone before the blur runs and the result reads as frosted
    // glass rather than a slightly soft copy of the plot.
    QImage capture = OvFrost::beginCapture (strip.width(), strip.height());
    const double scale = capture.width() / std::max (1.0, strip.width());

    {
        QPainter cp (&capture);
        cp.setRenderHint (QPainter::SmoothPixmapTransform, true);
        // the strip's rectangle inside the content image, in capture pixels
        const QRectF srcInContent (strip.x() * dpr, strip.y() * dpr,
                                   strip.width() * dpr, strip.height() * dpr);
        cp.drawImage (QRectF (0, 0, capture.width(), capture.height()),
                      content, srcInContent);
        Q_UNUSED (scale);
    }

    if (std::getenv ("ONYXTEON_FROSTDEBUG") != nullptr)
    {
        std::fprintf (stderr, "[frost] %s strip=%.0fx%.0f capture=%dx%d dpr=%.2f\n",
                      m_prefix.toStdString().c_str(), strip.width(), strip.height(),
                      capture.width(), capture.height(), dpr);
    }

    // The glass only has to be rebuilt when the pixels under it changed, or
    // when the glow did.  Without this it re-blurred the strip on every plot
    // repaint (1.13 ms of a 2.7 ms pass) for a picture that had not moved.
    // Sampled every 4th pixel and 4th row: the result is about to be blurred by
    // a 3 px Gaussian, so a finer key buys nothing but rebuilds.
    {
        quint64 acc = 0, mix = 1469598103934665603ull;
        for (int y = 0; y < capture.height(); y += 4)
        {
            const auto* line = reinterpret_cast<const QRgb*> (capture.constScanLine (y));
            for (int x = 0; x < capture.width(); x += 4)
            {
                acc += line[x];
                mix = (mix ^ line[x]) * 1099511628211ull;
            }
        }
        const quint64 key = acc ^ mix ^ (quint64 (m_glow * 255) << 40)
                            ^ (quint64 (w) << 48) ^ quint64 (h * 100);
        if (key == m_frostKey && ! m_frostLayer.isNull())
        {
            g->save();
            g->translate (strip.topLeft());
            g->setRenderHint (QPainter::SmoothPixmapTransform, true);
            g->drawImage (QRectF (0, 0, strip.width(), strip.height()), m_frostLayer,
                          QRectF (0, 0, m_frostLayer.width(), m_frostLayer.height()));
            g->restore();
            return;
        }
        m_frostKey = key;
    }

    // ---- 2..8. blur + saturate + veil + accent glow ----------------------
    OvFrost::Params p;
    p.shape    = m_headerTop ? OvFrost::Shape::TrapTop : OvFrost::Shape::TrapBottom;
    p.accent   = accent;
    p.light    = m_glow;          // --light, already level^2
    p.sigmaCss = 3.0;
    p.saturate = 0.85;
    p.veil     = 0.018;

    // Composited at HALF device resolution and blitted up.  Everything in this
    // surface is already the output of a 3 px Gaussian, so the extra resolution
    // cannot be seen — and compositing at device resolution meant filling a
    // 833x42 image with a radial gradient in DestinationIn mode on every frame,
    // which was 1.2 ms of the plot's 2.7 ms.
    const QImage glass = OvFrost::composite (capture, strip.width(), strip.height(),
                                             dpr * 0.5, p);
    if (glass.isNull()) return;

    m_frostLayer = glass;
    g->save();
    g->translate (strip.topLeft());
    g->setRenderHint (QPainter::SmoothPixmapTransform, true);
    g->drawImage (QRectF (0, 0, strip.width(), strip.height()), glass,
                  QRectF (0, 0, glass.width(), glass.height()));
    g->restore();
}

// ----------------------------------------------------------- interaction
//  Port of the canvas drag semantics in web_fx/index.html:
//    * grabbing a node keeps the grab offset, so it never snaps to the cursor
//    * x maps to frequency through the log axis, absolutely
//    * y maps to gain absolutely, through dbAt(), converted with the same
//      display multiplier the curve uses — so the node sits exactly on its
//      own band curve while dragging
//    * cut / notch / pass bands only move horizontally
void OvEqView::refreshNodePositions()
{
    gatherBands();
    layoutGeometry (m_plot, m_rangeDb);

    const double w = width(), h = height();
    for (auto& b : m_bands)
    {
        const double nodeGain = isGainType (b.type) ? b.gain * displayMult()
                                                    : bandMagAt (b, b.freq);
        b.x = toX (b.freq, w);
        b.y = toY (std::clamp (nodeGain, -m_rangeDb, m_rangeDb),
                   h, m_rangeDb, m_plot.top(), m_plot.bottom());
    }
}

double OvEqView::plotRangeDb() const
{
    return m_rangeDb;
}

int OvEqView::nodeAt (const QPointF& pos) const
{
    int best = -1;
    double bestD = 11.0;
    for (int i = 0; i < 5; ++i)
    {
        const double dx = pos.x() - m_bands[i].x;
        const double dy = pos.y() - m_bands[i].y;
        const double d = std::sqrt (dx * dx + dy * dy);
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

void OvEqView::mousePressEvent (QMouseEvent* e)
{
    // A new press always ends whatever the previous gesture left behind.  A
    // release can go to another item (the host window, an overlay) and then
    // m_dragBand kept its value — every later mouse move dragged that node,
    // which is why a drag started in empty space moved node 2.
    endDrag();

    refreshNodePositions();

    if (std::getenv ("ONYXTEON_HITTEST") != nullptr)
    {
        std::fprintf (stderr, "[hit] %s size=%.1fx%.1f press=(%.1f,%.1f) nodes:",
                      m_prefix.toStdString().c_str(), width(), height(),
                      e->position().x(), e->position().y());
        for (int i = 0; i < 5; ++i)
            std::fprintf (stderr, " %d(%.1f,%.1f)", i, m_bands[i].x, m_bands[i].y);
        std::fprintf (stderr, "\n");
    }

    const int hit = nodeAt (e->position());
    if (hit < 0) { e->ignore(); return; }

    if (e->button() == Qt::RightButton)
    {
        static const char* names[] = { "Bell", "Notch", "Low Shelf", "High Shelf",
                                       "Low Cut 12", "Low Cut 24",
                                       "High Cut 12", "High Cut 24" };
        emit menuRequested (hit, e->position(), QString::fromLatin1 (names[m_bands[hit].type]));
        e->accept();
        return;
    }

    m_dragBand = hit;
    m_dragMovesGain = isGainType (m_bands[hit].type);
    m_grabOffset = QPointF (m_bands[hit].x - e->position().x(),
                            m_bands[hit].y - e->position().y());

    const QString base = m_prefix + QString::number (hit + 1);
    if (auto* p = m_engine ? m_engine->typedParam (base + "Freq") : nullptr)
        p->beginGesture();
    if (m_dragMovesGain)
        if (auto* p = m_engine ? m_engine->typedParam (base + "Gain") : nullptr)
            p->beginGesture();

    e->accept();
}

void OvEqView::mouseMoveEvent (QMouseEvent* e)
{
    m_lastMoveTime = std::chrono::steady_clock::now();
    // Only drag while the button is really down.  Without this check a single
    // lost release turned the plot into a node that follows the cursor.
    if (m_dragBand >= 0 && ! (e->buttons() & Qt::LeftButton))
        endDrag();

    if (m_dragBand < 0) { e->ignore(); return; }

    const QString base = m_prefix + QString::number (m_dragBand + 1);

    // absolute position of the node: pointer plus the offset captured on press
    const QPointF node = e->position() + m_grabOffset;

    const double w = width();
    const double freq = fromX (std::clamp (node.x(), 0.0, w), w);

    if (auto* p = m_engine ? m_engine->typedParam (base + "Freq") : nullptr)
        p->setValue (freq);

    if (m_dragMovesGain)
    {
        const double db = fromY (std::clamp (node.y(), m_plot.top(), m_plot.bottom()),
                                 height(), m_rangeDb, m_plot.top(), m_plot.bottom());
        const double paramDb = std::clamp (db / displayMult(), -12.0, 12.0);
        if (auto* p = m_engine ? m_engine->typedParam (base + "Gain") : nullptr)
            p->setValue (paramDb);
    }

    // keep the readout attached to the node while it moves
    refreshNodePositions();
    QString tip = QStringLiteral ("#%1 %2 · %3 Hz").arg (m_dragBand + 1)
                    .arg (textOf (base + "Type"))
                    .arg (fmtHz (freq));
    if (m_dragMovesGain)
        tip += QStringLiteral (" · %1 dB")
                 .arg (valueOf (base + "Gain", 0.0) * displayMult(), 0, 'f', 1);
    if (! isShelf (m_bands[m_dragBand].type))
        tip += QStringLiteral (" · Q %1").arg (valueOf (base + "Q", 0.0), 0, 'f', 2);

    emit hoverInfo (tip, QPointF (m_bands[m_dragBand].x, m_bands[m_dragBand].y));
    m_dirty = true;
    e->accept();
}

void OvEqView::mouseReleaseEvent (QMouseEvent* e)
{
    endDrag();
    e->accept();
}

/// Close the current gesture (if any) and forget it.
void OvEqView::endDrag()
{
    if (m_dragBand < 0) return;

    const QString base = m_prefix + QString::number (m_dragBand + 1);
    if (auto* p = m_engine ? m_engine->typedParam (base + "Freq") : nullptr) p->endGesture();
    if (m_dragMovesGain)
        if (auto* p = m_engine ? m_engine->typedParam (base + "Gain") : nullptr) p->endGesture();

    m_dragBand = -1;
    m_dragMovesGain = false;
    emit hoverEnded();
    m_dirty = true;
}

void OvEqView::mouseDoubleClickEvent (QMouseEvent* e)
{
    refreshNodePositions();

    const int hit = nodeAt (e->position());
    if (hit >= 0)
    {
        // double click = flatten this band back into the pool (0 dB bell/shelf)
        const QString base = m_prefix + QString::number (hit + 1);
        if (auto* p = m_engine ? m_engine->typedParam (base + "Gain") : nullptr)
            p->resetToDefault();
        m_dirty = true;
        e->accept();
        return;
    }

    // double click on empty canvas = activate the nearest pooled node here
    if (e->position().y() < m_plot.top() || e->position().y() > m_plot.bottom())
    { e->ignore(); return; }

    int pooled = -1;
    double bestD = 1e9;
    for (int i = 0; i < 5; ++i)
    {
        if (m_bands[i].active) continue;
        const double d = std::abs (std::log (m_bands[i].freq) - std::log (fromX (e->position().x(), width())));
        if (d < bestD) { bestD = d; pooled = i; }
    }
    if (pooled >= 0)
    {
        const QString base = m_prefix + QString::number (pooled + 1);
        const double db = fromY (std::clamp (e->position().y(), m_plot.top(), m_plot.bottom()),
                                 height(), m_rangeDb, m_plot.top(), m_plot.bottom());
        if (auto* p = m_engine ? m_engine->typedParam (base + "Gain") : nullptr)
            p->setValue (std::clamp (std::abs (db) < 0.5 ? 4.0 : db / displayMult(), -12.0, 12.0));
        if (auto* p = m_engine ? m_engine->typedParam (base + "Freq") : nullptr)
            p->setValue (fromX (e->position().x(), width()));
        m_dirty = true;
    }
    e->accept();
}

void OvEqView::wheelEvent (QWheelEvent* e)
{
    refreshNodePositions();

    const int hit = (m_hoverBand >= 0) ? m_hoverBand : nodeAt (e->position());
    if (hit >= 0)
    {
        const QString base = m_prefix + QString::number (hit + 1);
        if (auto* p = m_engine ? m_engine->typedParam (base + "Q") : nullptr)
        {
            const double dir = e->angleDelta().y() > 0 ? 1.0 : -1.0;
            const double step = (e->modifiers() & Qt::ShiftModifier) ? 0.01 : 0.04;
            p->setNorm (std::clamp (p->norm() + dir * step, 0.0, 1.0));
        }
        refreshNodePositions();
        m_dirty = true;
        e->accept();
        return;
    }
    e->ignore();
}

void OvEqView::hoverMoveEvent (QHoverEvent* e)
{
    refreshNodePositions();

    const int hit = nodeAt (e->position());
    m_hoverBand = hit;
    setCursor (QCursor (hit >= 0 ? Qt::OpenHandCursor : Qt::CrossCursor));

    if (hit >= 0)
    {
        const QString base = m_prefix + QString::number (hit + 1);
        QString tip = QStringLiteral ("#%1 %2 · %3 Hz").arg (hit + 1)
                        .arg (textOf (base + "Type"))
                        .arg (fmtHz (valueOf (base + "Freq", 0.0)));
        if (isGainType (m_bands[hit].type))
            tip += QStringLiteral (" · %1 dB")
                     .arg (valueOf (base + "Gain", 0.0) * displayMult(), 0, 'f', 1);
        if (! isShelf (m_bands[hit].type))
            tip += QStringLiteral (" · Q %1").arg (valueOf (base + "Q", 0.0), 0, 'f', 2);
        emit hoverInfo (tip, QPointF (m_bands[hit].x, m_bands[hit].y));
    }
    else
        emit hoverEnded();
}

void OvEqView::mouseUngrabEvent()
{
    // the host took the mouse away mid-gesture
    endDrag();
}

void OvEqView::hoverLeaveEvent (QHoverEvent*)
{
    m_hoverBand = -1;
    emit hoverEnded();
}
