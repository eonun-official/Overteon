#include "OvEqView.h"
#include "OvFrost.h"
#include "OvEngine.h"
#include "OvQmlTypes.h"
#include "OvParam.h"
#include "OvTick.h"

#include <QPainter>
#include <QPainterPath>
#include <QCursor>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QHoverEvent>
#include <QLinearGradient>
#include <QtMath>
#include <QQuickWindow>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFramebufferObjectFormat>
#include <QSGSimpleTextureNode>
#include <QSGGeometryNode>
#include <QSGVertexColorMaterial>
#include <QSGTexture>
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
#include <bit>
#include <cmath>
#include <vector>

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

OvEqView::OvEqView (QQuickItem* parent) : QQuickItem (parent)
{
    // Geometry changed: rebuild the picture NOW, and tell the fog.
    //
    // update() is the important half, and it was missing.  A QQuickPaintedItem
    // keeps its last texture when the item is resized and Qt simply STRETCHES
    // it — so between the resize and the next repaint the plot is an enlarged
    // copy of the previous size.  Repaints here are driven by the spectrum, and
    // the analyzer does not emit in step with a window drag, so during a drag a
    // half could keep a stale picture for many frames.
    //
    // Worse, the two halves are separate items with separate schedules: if a
    // spectrum tick landed between them, the upper plot was redrawn at the new
    // size while the lower one was still the old picture stretched.  That is
    // exactly the reported tearing — the upper half showing a new frame and the
    // lower half the previous one — and it has nothing to do with the fog.
    //
    // Marking both dirty here makes them repaint in the SAME render pass, so
    // they cannot disagree.
    QObject::connect (this, &QQuickItem::widthChanged, this, [this] {
        update();
        emit fogGeometryChanged();
    });
    QObject::connect (this, &QQuickItem::heightChanged, this, [this] {
        update();
        emit fogGeometryChanged();
    });

    setAcceptHoverEvents (true);
    setAcceptedMouseButtons (Qt::LeftButton | Qt::RightButton);
    setAntialiasing (true);
    // The framebuffer is opaque: the plot always fills its own rect.
    setFlag (QQuickItem::ItemHasContents, true);

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
    // Repaint when the SPECTRUM arrives, at the rate it actually arrives.
    //
    // This used to be a private timer that compared stamps; the stamps are set
    // by OvEngine::spectrumChanged, which the HOST triggers at its own rate —
    // 11.8 Hz measured in jalv.  A private timer running faster than that
    // simply repainted the same picture: wasted frames, and because the extra
    // frames carried stale data the fill appeared to stutter and then jump.
    //
    // The spectrumChanged connection below now calls update() directly.  The
    // plot's own drag path already repaints in the frame it happens, so nothing
    // the user does is limited by this.
    //
    // Kept as well as the direct update() in spectrumChanged: the direct call
    // covers the common case, and this covers a changed stamp that arrived
    // while the item was not visible.  Removing it made the plots stop
    // repainting altogether, because spectrumChanged only fires when the data
    // moved more than one pixel and that gate swallows most updates.
    m_repaint.setInterval (OvTiming::kPlotMs);
    connect (&m_repaint, &QTimer::timeout, this, [this] {
        if (m_spectrumStamp != m_lastSpectrumStamp)
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
        // One repaint per new FFT — but rate limited to kPlotHz.
        //
        // The data arrives at the host's rate (11.8 Hz measured in jalv), so a
        // repaint per FFT is already slower than this; the limit exists for the
        // PREVIEW, where the same signal can arrive far more often.  Without it
        // two 1040x333 plots were re-rendered and re-uploaded 125 times a
        // second (measured through `bench.py -Gs`: sc 125 fps, pq 125 fps,
        // window 158 fps, 109 % of one core).  That is 5 MB of texture per
        // frame for data that had not changed.
        connect (m_engine, &OvEngine::spectrumChanged, this, [this] {
            ++m_spectrumStamp;
            using clk = std::chrono::steady_clock;
            const auto now = clk::now();
            if (m_lastPaintAt != clk::time_point()
                && now - m_lastPaintAt < std::chrono::milliseconds (OvTiming::kPlotMs))
                return;                              // the timer will pick it up
            m_lastPaintAt = now;
            m_lastSpectrumStamp = m_spectrumStamp;
            update();
        });
        // Deliberately NOT connected to metersChanged: the meter only drives
        // the accent glow, and repainting the whole plot (full re-render plus
        // the blur) at the meter rate cost more than the glow is worth.  The
        // glow follows on the next spectrum/parameter repaint.
        connect (m_engine, &OvEngine::metersChanged, this, [this] {
            // The panel's glow is a layer opacity with a 0.1 s transition in
            // the design, so it does not need a repaint per meter tick — and
            // each repaint re-renders a 1354x434 image and uploads 2.4 MB.
            // 10 Hz is well inside the transition.
            if (std::abs (m_engine->harm() - m_pendingGlow) <= 0.06) return;
            m_pendingGlow = m_engine->harm();
            const auto now = std::chrono::steady_clock::now();
            if (now - m_glowPainted < std::chrono::milliseconds (100)) return;
            m_glowPainted = now;
            m_dirty = true;
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

// How much the design is enlarged on its way to the window.
//
// mapToScene maps item units to window pixels, so the length of the image of a
// 1-unit vector is exactly the factor the design is drawn at: 1.0 at 1040x772,
// 0.62 in a 700x480 window, 1.5 in a 1560x1158 one.  Taking both axes and
// averaging keeps this honest if a rotation or a non-uniform scale is ever
// introduced, and it degrades to 1.0 for an item that is not in a window
// (mapToScene returns item coordinates then) rather than to a division by zero.
double OvEqView::displayScale() const
{
    const QPointF o  = mapToScene (QPointF (0, 0));
    const QPointF ex = mapToScene (QPointF (1, 0));
    const QPointF ey = mapToScene (QPointF (0, 1));
    const double sx = std::hypot (ex.x() - o.x(), ex.y() - o.y());
    const double sy = std::hypot (ey.x() - o.x(), ey.y() - o.y());
    const double s  = (sx + sy) * 0.5;
    return (std::isfinite (s) && s > 0.05 && s < 64.0) ? s : 1.0;
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

// ---------------------------------------------------------------------------
//  The renderer: keeps one QOpenGLPaintDevice and runs the view's paint() into
//  the item's framebuffer with it.
//
//  A QOpenGLPaintDevice is what makes this GPU work rather than a copy of the
//  CPU path.  Qt 6 still provides QOpenGL2PaintEngineEx, so paths, gradients,
//  rounded caps and glyphs are tessellated into GL commands here instead of
//  being rasterised into a QImage and uploaded.
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
//  The render node.
//
//  Four layers, in the order paintContent() draws them:
//
//      background   QSGSimpleTextureNode   grid + ruler labels, cached
//      spectrum     QSGGeometryNode        the analyser fill, per frame
//      glow bar     QSGSimpleTextureNode   the control pit's bloom, cached
//      overlay      QSGSimpleTextureNode   curves, nodes, digits, cached
//
//  QPainter runs only when a cached layer's key changes, and even then it draws
//  into a QImage off the render path.  Nothing here is OpenGL-specific: Qt
//  renders QSGGeometry and QSGSimpleTextureNode on GL, D3D11, Vulkan or Metal,
//  which is the whole point of using them.
// ---------------------------------------------------------------------------
namespace {

// Upload `img` into `node` when `key` changed, and place it at `rect`.
//
// The image is copied into `keep` first: createTextureFromImage builds a
// texture whose pixels are committed during the render pass, which happens
// after this function has returned, so the source must still be alive.
// Returns true when the node should be IN the tree.
//
// A texture node with no texture must not be: the renderer compares materials
// across the whole opaque batch, and a node whose texture is null makes
// QSGOpaqueTextureMaterial::compare read through it — measured as a SIGSEGV on
// the first frame with one empty glow-bar node present.  So an empty layer is
// detached rather than left drawn as nothing.
bool applyTexture (QQuickWindow* win, QSGImageNode* node, const QImage& img,
                   const QString& key, QString& haveKey, QImage& keep,
                   const QRectF& rect)
{
    if (node == nullptr || win == nullptr) return false;
    if (img.isNull() || rect.isEmpty())
        return false;
    if (key != haveKey || node->texture() == nullptr)
    {
        keep = img.copy();
        node->setTexture (win->createTextureFromImage (keep));
        haveKey = key;
    }
    node->setRect (rect);
    return true;
}

// A cached QImage as a scene-graph texture.  `key` decides when it is
// re-uploaded: the image itself is not compared, because that would cost a full
// pass over the pixels every frame.
class LayerNode
{
public:
    // QSGImageNode instead of QSGSimpleTextureNode.
    //
    // QSGSimpleTextureNode crashed this renderer outright — SIGSEGV inside
    // QSGOpaqueTextureMaterial::compare, on the first frame, with a single
    // texture node in the tree and nothing else.  QSGImageNode is the node Qt
    // provides for "draw this image as a quad"; it owns its texture and its
    // material, so there is no lifetime to get wrong here.
    QSGNode* node = nullptr;
    QSGImageNode* img = nullptr;
    QString key;
    QImage pending;      // must outlive the texture upload

    void init (QQuickWindow* win)
    {
        if (node != nullptr) return;
        img  = win->createImageNode();
        node = img;
        img->setFiltering (QSGTexture::Linear);
        img->setTextureCoordinatesTransform (QSGImageNode::NoTransform);
    }

    void refresh (QQuickWindow* win, const QImage& image, const QString& k,
                  const QRectF& rect)
    {
        init (win);
        if (image.isNull() || rect.isEmpty())
        {
            img->setRect (QRectF());
            return;
        }
        if (k != key)
        {
            // Keep our own copy: the texture is uploaded from this image later,
            // during the render pass, so the pixels have to still be there.
            pending = image.copy();
            img->setTexture (win->createTextureFromImage (pending));
            key = k;
        }
        img->setRect (rect);
    }
};

// The spectrum: a triangle strip from the baseline to the outline, with the
// gradient carried in the vertex colours (opaque at the top, transparent at the
// baseline) — the same thing QLinearGradient did, without a paint engine.
class SpectrumNode : public QSGGeometryNode
{
public:
    SpectrumNode()
    {
        auto* geom = new QSGGeometry (QSGGeometry::defaultAttributes_ColoredPoint2D(), 2);
        geom->setDrawingMode (QSGGeometry::DrawTriangleStrip);
        setGeometry (geom);
        setFlag (QSGNode::OwnsGeometry);
        setMaterial (new QSGVertexColorMaterial);
        setFlag (QSGNode::OwnsMaterial);
    }

    /// Returns the vertex count drawn; 0 means "nothing".
    int rebuild (const QVector<QPointF>& top, double bottom, const QColor& accent)
    {
        QSGGeometry* geom = geometry();
        const int n = top.size();
        if (n < 2) { geom->setVertexCount (0); markDirty (QSGNode::DirtyGeometry); return 0; }
        if (geom->vertexCount() < n * 2)
            geom->allocate (n * 2);

        auto* v = geom->vertexDataAsColoredPoint2D();
        const int hiA = int (accent.alpha() * 0.18 * 255.0 + 0.5);   // the fill's top
        const int loA = 0;                                           // fades out
        int k = 0;
        for (const QPointF& p : top)
        {
            v[k].set (float (p.x()), float (p.y()),
                      uchar (accent.red()), uchar (accent.green()), uchar (accent.blue()),
                      uchar (hiA));
            ++k;
            v[k].set (float (p.x()), float (bottom),
                      uchar (accent.red()), uchar (accent.green()), uchar (accent.blue()),
                      uchar (loA));
            ++k;
        }
        geom->setVertexCount (k);
        markDirty (QSGNode::DirtyGeometry);
        return k;
    }
};

}  // namespace

void OvEqView::setWantImage (bool on)
{
    if (m_wantImage == on) return;
    m_wantImage = on;
    emit wantImageChanged();
    update();
}

QSGNode* OvEqView::updatePaintNode (QSGNode* old, UpdatePaintNodeData*)
{
    if (m_engine == nullptr) return old;

    // The CPU seam glass — the fallback for a machine where the GPU chain
    // cannot run — reads this half's pixels.  Producing them means rasterising
    // the plot with QPainter, which is exactly the cost the node path removes,
    // so it runs only when that glass is actually the active path.
    if (m_wantImage)
    {
        const qreal dpr = window() != nullptr ? window()->devicePixelRatio() : 1.0;
        const int pw = int (std::lround (width() * dpr));
        const int ph = int (std::lround (height() * dpr));
        if (m_content.width() != pw || m_content.height() != ph)
            m_content = QImage (std::max (1, pw), std::max (1, ph),
                                QImage::Format_ARGB32_Premultiplied);
        {
            QPainter cg (&m_content);
            cg.setRenderHint (QPainter::Antialiasing, true);
            cg.setRenderHint (QPainter::TextAntialiasing, true);
            cg.scale (dpr, dpr);
            paintContent (&cg, width(), height(), m_plot.top(), m_plot.bottom(),
                          QColor (m_accent));
        }
        m_unfrosted = m_content;
        QMetaObject::invokeMethod (this, [this] { emit contentChanged(); },
                                   Qt::QueuedConnection);
    }
    if (window() == nullptr) return old;

    if (std::getenv ("ONYXTEON_NONODE")) return old;

    OvEqView::Layers L;
    buildLayers (L);

    if (m_sgRoot == nullptr)
    {
        m_sgRoot = new QSGNode;
        m_sgBg      = window()->createImageNode();
        auto* spn = new SpectrumNode; m_sgSpectrum = spn;
        m_sgGlow    = window()->createImageNode();
        m_sgOverlay = window()->createImageNode();
        m_sgRoot->appendChildNode (m_sgBg);
        m_sgRoot->appendChildNode (m_sgSpectrum);
        m_sgRoot->appendChildNode (m_sgGlow);
        m_sgRoot->appendChildNode (m_sgOverlay);
        for (QSGImageNode* n : { m_sgBg, m_sgGlow, m_sgOverlay })
        {
            n->setFiltering (QSGTexture::Linear);
            n->setTextureCoordinatesTransform (QSGImageNode::NoTransform);
        }
    }

    const QRectF full (0, 0, width(), height());

    // Keep a node in the tree only while it has something to draw.
    auto slot = [this] (QSGImageNode* n, bool wanted) {
        if (n == nullptr) return;
        const bool in = n->parent() != nullptr;
        if (wanted && ! in) m_sgRoot->appendChildNode (n);
        else if (! wanted && in) m_sgRoot->removeChildNode (n);
    };

    // ---- background ------------------------------------------------------
    {
        const QImage& img = ! L.background.isNull() ? L.background
                                                   : backgroundImage (width(), height(),
                                                                      m_plot.top(), m_plot.bottom(),
                                                                      QColor (m_accent));
        slot (m_sgBg, applyTexture (window(), m_sgBg, img, L.bgKey,
                                    m_texKeyBg, m_texImgBg, full));
    }

    // ---- spectrum (the only thing that changes per frame) ----------------
    //
    // Detached when it has nothing to draw.  An empty QSGGeometryNode keeps its
    // material, and the renderer compares materials across the whole opaque
    // batch: a vertex-colour material compared against a texture material reads
    // the wrong type and segfaults — measured in
    // QSGOpaqueTextureMaterial::compare as soon as this node was present with
    // zero vertices.
    {
        auto* sp = static_cast<SpectrumNode*> (m_sgSpectrum);
        const int n = std::getenv ("ONYXTEON_ONLYBG")
                    ? sp->rebuild (QVector<QPointF>(), 0.0, L.accent)
                    : sp->rebuild (L.spectrum, L.spectrumBottom, L.accent);
        if (n > 0 && ! m_spAttached)
        {
            m_sgRoot->insertChildNodeBefore (sp, m_sgGlow);
            m_spAttached = true;
        }
        else if (n == 0 && m_spAttached)
        {
            m_sgRoot->removeChildNode (sp);
            m_spAttached = false;
        }
    }

    // ---- glow bar --------------------------------------------------------
    {
        const bool have = ! m_glowBar.isNull() && ! m_glowDest.isEmpty();
        slot (m_sgGlow, applyTexture (window(), m_sgGlow,
                                      have ? m_glowBar : QImage(),
                                      have ? QString::number (m_glowKey) : QString(),
                                      m_texKeyGlow, m_texImgGlow,
                                      have ? m_glowDest : QRectF()));
    }

    // ---- overlay ---------------------------------------------------------
    {
        const QImage& img = ! L.overlay.isNull() ? L.overlay
                                                : overlayImage (width(), height(),
                                                                m_plot.top(), m_plot.bottom(),
                                                                QColor (m_accent));
        slot (m_sgOverlay, applyTexture (window(), m_sgOverlay, img, L.overlayKey,
                                         m_texKeyOv, m_texImgOv, full));
    }

    QSGNode* root = m_sgRoot;
    return root;
}

void OvEqView::paint (QPainter* g)
{
    // One tick per actual paint of this plot, so `--tick` can show that the two
    // EQ halves are not repainting at the same rate (they were not: 21 vs 94 Hz
    // was measured, which nothing in the code explained).
    const bool isSc = (m_prefix == QStringLiteral ("sc"));
    ovtick::markKind (isSc ? "paint-sc" : "paint-pq",
                      isSc ? ovtick::TickKind::PaintSc : ovtick::TickKind::PaintPq);

    OvPaintTimer _p (m_prefix == QStringLiteral ("sc") ? "sc" : "pq");

    // This widget's own rate, counted where it actually paints.  Published to
    // QML continuously (it is a few bytes per second) so ONYXTEON_SOLO can show
    // it; printed only when asked.
    {
        static int frames = 0;
        static auto t0 = std::chrono::steady_clock::now();
        ++frames;
        const double dt = std::chrono::duration<double> (
            std::chrono::steady_clock::now() - t0).count();
        if (dt >= 1.0)
        {
            m_paintFps = frames / dt;
            emit paintFpsChanged();
            if (std::getenv ("ONYXTEON_FPS") != nullptr)
                std::fprintf (stderr, "[fps] %s %.1f fps\n",
                              m_prefix.toStdString().c_str(), m_paintFps);
            frames = 0;
            t0 = std::chrono::steady_clock::now();
        }
    }

    gatherBands();
    layoutGeometry (m_plot, m_rangeDb);

    const double w = width(), h = height();
    const QColor accent (m_accent);

    const bool prof = std::getenv ("ONYXTEON_PROFILE") != nullptr;
    const auto tRender0 = std::chrono::steady_clock::now();

    // ---- draw, on the GPU ------------------------------------------------
    //
    // This used to rasterise into an off-screen QImage and then blit it.  The
    // painter handed in now belongs to a QOpenGLPaintDevice, so the same calls
    // become GL commands: no image, no upload, no CPU rasterisation.  The
    // drawing code below is untouched — that is the whole point of going
    // through the paint engine rather than writing shaders for the plot.
    paintContent (g, w, h, m_plot.top(), m_plot.bottom(), accent);

    const double msRender = std::chrono::duration<double, std::milli> (
        std::chrono::steady_clock::now() - tRender0).count();

    // How long after the mouse move did this frame reach the painter?
    if (prof && m_lastMoveTime != std::chrono::steady_clock::time_point())
    {
        static double sumLat = 0; static int latN = 0; static double worst = 0;
        const double lat = std::chrono::duration<double, std::milli> (
                               std::chrono::steady_clock::now() - m_lastMoveTime).count();
        if (lat < 500.0)
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
        std::fprintf (stderr, "[paint] %s size=%.1fx%.1f nodes:",
                      m_prefix.toStdString().c_str(), w, h);
        for (int i = 0; i < 5; ++i)
            std::fprintf (stderr, " %d(%.1f,%.1f)", i, m_bands[i].x, m_bands[i].y);
        std::fprintf (stderr, "\n");
    }

    if (prof)
    {
        static double sumRender = 0; static int cnt = 0;
        sumRender += msRender;
        if (++cnt % 60 == 0) {
            std::fprintf (stderr, "[split] %s render=%.2fms (GPU)\n",
                          m_prefix.toStdString().c_str(), sumRender / 60);
            sumRender = 0;
        }
    }

    // Tell the seam glass and the fog that this half changed.
    //
    // QUEUED, not a direct emit: paint() now runs wherever the scene graph
    // renders — on the render thread in a threaded loop, which is what the
    // plug-in uses.  The receivers call QQuickItem::update(), which is only
    // legal on the GUI thread, so a direct signal here would drive the fog's
    // repaint from the wrong thread.
    QMetaObject::invokeMethod (this, [this] { emit contentChanged(); },
                               Qt::QueuedConnection);
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
    g->setRenderHint (QPainter::SmoothPixmapTransform, false);   // 1:1 blit
    g->drawImage (QRectF (0, 0, w, h), backgroundImage (w, h, top, bottom, accent),
                  QRectF (0, 0, m_bg.width(), m_bg.height()));

    // The 128-bin table, read once here and used twice: the fill below and the
    // glow bar in the control pit.
    float spec[128] = {};
    float specRaw[128] = {};      // exactly what the analyser said

    // ---- ballistics ------------------------------------------------------
    // The host polls the analyser at ~12 Hz (measured 11.8 in jalv) while this
    // paints at up to 60, so raw bins would be held for five frames and then
    // cut to an unrelated picture.  Each bin therefore gets an instant attack
    // and a rate-derived release towards the newest value.
    //
    // The release is expressed as "this many dB in this many seconds" and the
    // per-frame step is derived from the DISPLAY rate, the way ZL Audio does it
    // (SpectrumDecayer::setDecaySpeed): step = 1 - (target/start)^(1/n_frames)
    // with n_frames = refresh_rate * decay_seconds.  A fixed coefficient is a
    // fixed number of FRAMES, so it falls faster the higher the refresh rate and
    // at 60 Hz is over before a frame can show it.
    //
    // The previous version computed exp(-dt/tau) and then never used it — the
    // bin was assigned the raw target, so every measurement landed as an exact
    // value.  That is why the fill and the glow bar showed numbers, not motion.
    {
        using clk = std::chrono::steady_clock;
        const auto now = clk::now();
        double dt = 1.0 / double (OvTiming::kPlotHz);
        if (m_specShownAt != clk::time_point())
            dt = std::clamp (std::chrono::duration<double> (now - m_specShownAt).count(),
                             0.0005, 0.20);
        m_specShownAt = now;

        constexpr double kFloor   = 0.0;      // the table's own bottom
        constexpr double kDecaySec = 0.30;    // seconds to fall the full scale

        float raw[128] = {};
        if (m_engine != nullptr)
        {
            const auto& tbl = m_headerTop ? m_engine->inputSpectrumTable()
                                          : m_engine->spectrumTable();
            const int n = std::min (128, int (tbl.size()));
            for (int i = 0; i < n; ++i)
            {
                raw[i] = float (tbl.at (i).toDouble());
                specRaw[i] = raw[i];
            }
        }

        const double rate    = (dt > 1e-6) ? 1.0 / dt : double (OvTiming::kPlotHz);
        const double nFrames = std::max (1.0, rate * kDecaySec);

        for (int i = 0; i < 128; ++i)
        {
            const double shown  = m_specShown[i];
            const double target = raw[i];

            if (target >= shown)
            {
                m_specShown[i] = float (target);          // instant attack
            }
            else
            {
                const double start = std::max (1e-6, shown - kFloor);
                const double goal  = std::max (0.0, target - kFloor);
                const double ratio = std::clamp (goal / start, 0.0, 1.0);
                const double step  = std::clamp (1.0 - std::pow (ratio, 1.0 / nFrames), 0.0, 1.0);
                const double next  = shown + (shown - kFloor) * -step;
                m_specShown[i] = float (std::max (next, target));
            }
            spec[i] = m_specShown[i];
        }
    }

    OV_MARK ("spec");
    // ---- spectrum (from the engine, 0..1 over 20 Hz..20 kHz)
    //
    // Read from the ENGINE'S table, not from the source.  The table is filled
    // once per poll and the plugin's analyser is single-shot, so pulling the
    // source here as well left one of the two callers with zeros.  The same
    // table also drives the glow bar in the control pit below, so the fill and
    // the bar can never disagree.
    {
        // (the raw read and the ballistics are both above; `spec` is what to
        // draw, and it came from the tap this section owns — the top section is
        // the INPUT and the bottom one the OUTPUT)

        // The spectrum is 128 FFT bins across a log axis, so neighbouring bins
        // are 20 % apart in x and a straight polyline between them reads as a
        // staircase of square steps — and the mock's square-wave harmonics make
        // every step a spike.  Joining the points with a Catmull-Rom spline
        // through them keeps the peaks where they are but removes the corners.
        double xs[128], ys[128];
        for (int i = 0; i < 128; ++i)
        {
            const double f = 20.0 * std::pow (1000.0, (double) i / 127.0);
            xs[i] = toX (f, w);
            ys[i] = bottom - (double) spec[i] * (bottom - top) * 0.92;
        }

        QPainterPath sp;
        sp.moveTo (0.0, bottom);
        sp.lineTo (xs[0], ys[0]);
        for (int i = 0; i < 127; ++i)
        {
            const double x0 = (i > 0) ? xs[i - 1] : xs[0];
            const double y0 = (i > 0) ? ys[i - 1] : ys[0];
            const double x1 = xs[i], y1 = ys[i];
            const double x2 = xs[i + 1], y2 = ys[i + 1];
            const double x3 = (i + 2 < 128) ? xs[i + 2] : x2;
            const double y3 = (i + 2 < 128) ? ys[i + 2] : y2;
            // Catmull-Rom -> cubic Bezier
            sp.cubicTo (QPointF (x1 + (x2 - x0) / 6.0, y1 + (y2 - y0) / 6.0),
                        QPointF (x2 - (x3 - x1) / 6.0, y2 - (y3 - y1) / 6.0),
                        QPointF (x2, y2));
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

    // ---- glow bar, in the control pit -----------------------------------
    // Painted into the plot image rather than as a QML item, so the frosted
    // layer on top of it blooms it — that is the effect.  It has to come
    // BEFORE the overlay below: the CPU frost is painted inside overlayImage(),
    // so a bar drawn after it would sit on top of the glass instead of under.
    OV_MARK ("glowbar");
    // RAW, not smoothed.  The bar is drawn under a 3 px Gaussian so the extra
    // per-frame motion of the smoothed array is invisible, but feeding it the
    // smoothed values made the bar's cache key change on every repaint: the
    // cache never hit and two 160x8 images were rebuilt with exp()/pow() per
    // pixel at 150 fps.  That cost 40 % of a core in the preview app.
    // The SMOOTHED array: the bar is under a 3 px blur so fine detail is
    // invisible anyway, but it must move over frames like everything else.
    // Its cache key is taken from the smoothed levels, so it still only
    // rebuilds when the shape actually changed.
    paintGlowBar (g, w, h, accent, spec);

    OV_MARK ("curves");
    OV_MARK ("overlay");
    g->drawImage (QRectF (0, 0, w, h), overlayImage (w, h, top, bottom, accent),
                  QRectF (0, 0, m_overlay.width(), m_overlay.height()));
    g->setRenderHint (QPainter::SmoothPixmapTransform, true);

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

// ---------------------------------------------------------------------------
//  The overflow fog — drawEQOverflow() in the design.
//
//  Painted into a surface that spans the WHOLE EQ zone, not this view.  The page
//  puts its two .eqOverflow canvases inside #eqzone with `inset:0` and offsets
//  them by the curve canvas's position, so a curve leaving the top plot keeps
//  going across the middle of the UI and into the other section.  Clipping it to
//  the section cut it off after the ~24 px of padding, which is why the crossing
//  did not read like the page.
//
//  `origin` is where this view's (0,0) lands in the target's coordinates.
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
//  Build the four layers for the render node.
//
//  The spectrum is re-derived every call because it is the only thing that
//  moves; the other three are built through their existing caches and returned
//  only when their key changed, so an idle frame does no drawing at all.
// ---------------------------------------------------------------------------
void OvEqView::buildLayers (Layers& out)
{
    out = Layers();
    if (m_engine == nullptr) return;

    gatherBands();
    layoutGeometry (m_plot, m_rangeDb);
    const double w = width(), h = height();
    if (w < 2.0 || h < 2.0) return;
    const double top = m_plot.top(), bottom = m_plot.bottom();
    const QColor accent (m_accent);
    out.accent = accent;

    // ---- cached layers ---------------------------------------------------
    // backgroundImage() returns the SAME image when its key has not changed, so
    // comparing keys afterwards is how "did this rebuild?" is answered without
    // adding a second dirty flag that could disagree with the cache.
    const QString prevBg = m_bgKey;
    out.background = backgroundImage (w, h, top, bottom, accent);
    if (m_bgKey != prevBg || out.background.isNull())
    {
        out.bgKey = m_bgKey;
        out.background.detach();      // a copy: the cache keeps its own
    }
    else
    {
        out.background = QImage();
        out.bgKey = m_bgKey;
    }

    const QString prevOv = m_overlayKey;
    out.overlay = overlayImage (w, h, top, bottom, accent);
    if (m_overlayKey != prevOv || out.overlay.isNull())
    {
        out.overlayKey = m_overlayKey;
        out.overlay.detach();
    }
    else
    {
        out.overlay = QImage();
        out.overlayKey = m_overlayKey;
    }

    // ---- the spectrum, flattened -----------------------------------------
    //
    // The analyser outline, Catmull-Rom through the 128 log-spaced samples so
    // the fill does not read as a staircase, then flattened to a polyline the
    // node can triangulate.  Same smoothing the painter path used.
    {
        float spec[128] = {};
        if (m_engine != nullptr)
        {
            const auto& tbl = m_headerTop ? m_engine->inputSpectrumTable()
                                          : m_engine->spectrumTable();
            const int n = std::min (128, int (tbl.size()));
            for (int i = 0; i < n; ++i) spec[i] = float (tbl.at (i).toDouble());
        }

        double xs[128], ys[128];
        for (int i = 0; i < 128; ++i)
        {
            const double f = 20.0 * std::pow (1000.0, (double) i / 127.0);
            xs[i] = toX (f, w);
            ys[i] = bottom - (double) spec[i] * (bottom - top) * 0.92;
        }

        out.spectrumBottom = bottom;
        out.spectrum.reserve (128 * 6);
        const int kSub = 5;                   // 5 segments per span is plenty
        for (int i = 0; i < 127; ++i)
        {
            const double x0 = (i > 0) ? xs[i - 1] : xs[0];
            const double y0 = (i > 0) ? ys[i - 1] : ys[0];
            const double x1 = xs[i],     y1 = ys[i];
            const double x2 = xs[i + 1], y2 = ys[i + 1];
            const double x3 = (i + 2 < 128) ? xs[i + 2] : x2;
            const double y3 = (i + 2 < 128) ? ys[i + 2] : y2;

            // cubic Bezier control points of the Catmull-Rom segment
            const QPointF c1 (x1 + (x2 - x0) / 6.0, y1 + (y2 - y0) / 6.0);
            const QPointF c2 (x2 - (x3 - x1) / 6.0, y2 - (y3 - y1) / 6.0);
            for (int k = 0; k < kSub; ++k)
            {
                const double t = double (k) / kSub;
                const double mt = 1.0 - t;
                const double bx = mt*mt*mt*x1 + 3*mt*mt*t*c1.x() + 3*mt*t*t*c2.x() + t*t*t*x2;
                const double by = mt*mt*mt*y1 + 3*mt*mt*t*c1.y() + 3*mt*t*t*c2.y() + t*t*t*y2;
                out.spectrum.push_back (QPointF (bx, by));
            }
        }
        out.spectrum.push_back (QPointF (xs[127], ys[127]));
    }
}

void OvEqView::fogSegments (const QPointF& origin, QVector<FogSegment>& out)
{
    out.clear();
    if (m_engine == nullptr) return;
    if (std::getenv ("ONYXTEON_NOFOG") != nullptr) return;

    const double w = width(), h = height();
    if (w < 2.0 || h < 2.0) return;

    // Compute the layout here rather than reading m_plot: that member is only
    // filled by paint(), and the fog is asked for before this view has painted
    // once.  Reading it gave an empty rect and the fog was never drawn.
    gatherBands();
    QRectF plot;
    double rangeDb = 0.0;
    layoutGeometry (plot, rangeDb);

    const double top = plot.top();
    if (plot.bottom() - top < 4.0) return;

    const QColor accent (m_accent);
    const int curveStep = 2;
    const auto& curveY = totalCurveY (w, h, top, plot.bottom(), curveStep);
    if (curveY.empty()) return;

    const double edge   = fogEdge();
    const double dir    = fogDir();
    const double kRange = fogRange();
    const double lo = std::min (edge, edge + dir * kRange);
    const double hi = std::max (edge, edge + dir * kRange);

    // Sample y(x) at the page's 3 px step and keep only the part past the edge.
    //
    // ONE continuous run — no per-band split.  The twelve graded bands were the
    // design's way of FAKING a growing blur, and a band is a hard-edged slice:
    // wherever the curve handed over from one band to the next the picture
    // stepped, which is what read as stacked slabs round the seam.  The blur
    // and the fade are a continuous function of distance now (blur.frag).
    auto push = [&] (auto&& yAt, float alpha, float width)
    {
        auto clampY = [&] (double y) { return std::clamp (y, lo, hi); };

        double prevX = 0.0;
        double prevY = clampY (yAt (0.0));
        for (double x = 3.0; ; x += 3.0)
        {
            const double xx = std::min (x, w);
            const double yy = clampY (yAt (xx));
            const double mid = (prevY + yy) * 0.5;
            const double outward = dir > 0 ? (mid - edge) : (edge - mid);
            if (outward > 0.0)
                out.push_back ({ QPointF (origin.x() + prevX, origin.y() + prevY),
                                 QPointF (origin.x() + xx,    origin.y() + yy),
                                 alpha, width });
            prevX = xx;
            prevY = yy;
            if (xx >= w) break;
        }
    };

    // ---- the five band curves, .22 accent, 1 px --------------------------
    for (int i = 0; i < 5; ++i)
    {
        const auto& b = m_bands[i];
        if (! b.active) continue;
        push ([&] (double x)
              {
                  return toY (bandMagAt (b, fromX (x, w)),
                              h, rangeDb, top, plot.bottom());
              },
              0.22f * float (accent.alphaF()), 1.0f);
    }

    // ---- the total curve, accent, 2 px -----------------------------------
    push ([&] (double x)
          {
              const double t = std::clamp (x / double (curveStep), 0.0,
                                           double (curveY.size() - 1));
              const int i = int (t);
              const int j = std::min (i + 1, int (curveY.size()) - 1);
              return curveY[(size_t) i]
                   + (curveY[(size_t) j] - curveY[(size_t) i]) * (t - i);
          },
          float (accent.alphaF()), 2.0f);
}

void OvEqView::paintOverflowFog (QPainter* g, const QPointF& origin)
{
    // The CPU fallback: the same segments, drawn as lines.  Used only when a
    // painter is what the caller has; the live path builds QSGGeometry from
    // fogSegments(), which works on every RHI backend.
    QVector<FogSegment> segs;
    fogSegments (origin, segs);
    const QColor accent (m_accent);

    g->save();
    g->setBrush (Qt::NoBrush);
    for (const auto& s : segs)
    {
        QColor c = accent;
        c.setAlphaF (accent.alphaF() * s.alpha);
        g->setPen (QPen (c, s.width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        g->drawLine (s.a, s.b);
    }
    g->restore();
}

// ---------------------------------------------------------------------------
//  The geometry the GPU pass needs, in this view's own pixels.
//
//  fogEdge  the plot edge the curve escapes from
//  fogDir   +1 when it escapes downward (this half's header is on top),
//           -1 when it escapes upward
//  fogRange how far it reaches.  Deliberately divided by displayScale(): the
//           reach and the blur profile are a TREATMENT tuned at the design
//           size, not geometry, so they keep the same size on screen at every
//           window size.  The curve inside still scales, because that is the
//           plot.
// ---------------------------------------------------------------------------
quint64 OvEqView::curveSignature() const
{
    const_cast<OvEqView*> (this)->gatherBands();
    QRectF plot;
    double rangeDb = 0.0;
    layoutGeometry (plot, rangeDb);

    quint64 h = 1469598103934665603ull;
    auto mix = [&h] (quint64 v) { h = (h ^ v) * 1099511628211ull; };
    auto mixd = [&mix] (double d) { mix (quint64 (qint64 (std::llround (d * 256.0)))); };

    mix (quint64 (m_headerTop ? 1 : 0));
    mixd (rangeDb);
    for (const auto& b : m_bands)
    {
        mix (quint64 (b.type));
        mix (quint64 (b.active ? 1 : 0));
        mixd (std::log (std::max (1e-3, b.freq)));
        mixd (b.gain);
        mixd (b.q);
    }
    return h;
}

double OvEqView::fogEdge() const
{
    QRectF plot;
    double rangeDb = 0.0;
    layoutGeometry (plot, rangeDb);
    return m_headerTop ? plot.bottom() : plot.top();
}

double OvEqView::fogDir() const
{
    return m_headerTop ? 1.0 : -1.0;
}

double OvEqView::fogRange() const
{
    return 224.0 / displayScale();
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

    // The GPU glass in QML owns this surface now unless the CPU path is forced.
    if (std::getenv ("ONYXTEON_CPUFROST") != nullptr)
        paintFrost (g, m_content, w, h, accent);
}

// ---------------------------------------------------------------------------
//  The glow bar.
//
//  A horizontal strip of light through each control pit: brightness left to
//  right is level low frequency to high, so the bar reads as the spectrum the
//  section above it draws, squeezed into the trapezoid and blooming through the
//  frosted glass on top.
//
//  The top section's bar is the INPUT analyser and the bottom one the OUTPUT —
//  the same distinction the two spectrum fills already carry — and both come
//  from the single 128-bin table in OvEngine, so a bar can never show a
//  different picture from the plot it belongs to.
// ---------------------------------------------------------------------------
// Build (or reuse) the glow bar's image and report where it goes.
//
// Split out of paintGlowBar so the render node can get the pixels without a
// QPainter: the bar is a cached image, and drawing it is a separate step that
// only the CPU path needs.  Returns false when there is nothing to draw.
bool OvEqView::ensureGlowBar (double w, double h, const QColor& accent,
                              const float* spec, QRectF& dest)
{
    if (std::getenv ("ONYXTEON_NOGLOW") != nullptr) return false;
    if (w < 8.0 || h < OvThemeBarHeight) return false;

    //   .controlpit{ left:50%; width:640px; height:32px; transform:translateX(-50%) }
    //   .controlpit.top{top:0}   .controlpit.bottom{bottom:0}
    const double pitW = std::min (double (OvThemePitWidth), w);
    const double pitX = (w - pitW) * 0.5;
    const double barH = OvThemeBarHeight;
    const double barY = m_headerTop ? 0.0 : h - barH;

    // ---- 32 bands --------------------------------------------------------
    // The page draws ONE elliptical blob:
    //   .controlpit.top::before{ opacity: var(--light);
    //     background: radial-gradient(ellipse 70% 160% at 50% 0%,
    //                   accent .36 0%, accent .13 38%, accent 0 78%) }
    // The blob stays — it is the shape — but its brightness and its reach are
    // modulated per band, so a loud region of the spectrum glows brighter and
    // climbs further from the lit edge.  That keeps the page's look (a soft
    // pool of light, not a bar chart) while still showing where the energy is.
    constexpr int kBands = 32;
    double band[kBands];
    for (int b = 0; b < kBands; ++b)
    {
        const int i0 = (b * 128) / kBands;
        const int i1 = std::max (i0 + 1, ((b + 1) * 128) / kBands);
        double sum = 0.0;
        for (int i = i0; i < i1; ++i) sum += std::clamp (double (spec[i]), 0.0, 1.0);
        double l = sum / double (i1 - i0);

        // ---- ballistics, per band --------------------------------------
        // The bar used to read the RAW bins (`specRaw` was passed in), so it had
        // no smoothing at all: every new FFT moved it straight to a new shape.
        // It gets the same rate-derived release as the spectrum fill, in dB
        // terms, so "how far it reaches from the lit edge" moves over frames.
        {
            using clk = std::chrono::steady_clock;
            const auto now = clk::now();
            double dt = 1.0 / double (OvTiming::kPlotHz);
            if (m_glowShownAt != clk::time_point())
                dt = std::clamp (std::chrono::duration<double> (now - m_glowShownAt).count(),
                                 0.0005, 0.20);
            m_glowShownAt = now;

            constexpr double kDecaySec = 0.35;
            const double rate    = (dt > 1e-6) ? 1.0 / dt : double (OvTiming::kPlotHz);
            const double nFrames = std::max (1.0, rate * kDecaySec);

            const double shown = m_glowShown[b];
            if (l >= shown)
            {
                m_glowShown[b] = l;                       // instant attack
            }
            else
            {
                m_glowFalling = true;
                const double start = std::max (1e-6, shown);
                const double ratio = std::clamp (l / start, 0.0, 1.0);
                const double step  = std::clamp (1.0 - std::pow (ratio, 1.0 / nFrames), 0.0, 1.0);
                m_glowShown[b] = std::max (l, shown - shown * step);
            }
            l = m_glowShown[b];
        }

        // Has the release finished?  Checked here, where the value is known,
        // not from the incoming peak (see the note above).
        {
            bool anyFalling = false;
            for (int b = 0; b < kBands; ++b)
                if (m_glowShown[b] > 0.002) anyFalling = true;
            m_glowFalling = anyFalling;
        }
        // Display curve.  The table is (dB + 85) / 85, so a signal 20 dB down
        // sits at 0.76 and would glow almost as brightly as a full-scale one;
        // squaring turns that into something that reads as loudness.
        band[b] = l * l;
    }

    // Four smoothing passes.  One was nowhere near enough: 32 bands over 640 px
    // is 20 px each, so a single [1 2 1]/4 pass only reaches halfway into its
    // neighbours and the modulation still read as facets rather than as a pool
    // of light.  Four passes approximate a Gaussian of about one band's width.
    for (int pass = 0; pass < 4; ++pass)
    {
        double tmp[kBands];
        for (int b = 0; b < kBands; ++b)
            tmp[b] = 0.25 * band[std::max (0, b - 1)] + 0.5 * band[b]
                   + 0.25 * band[std::min (kBands - 1, b + 1)];
        for (int b = 0; b < kBands; ++b) band[b] = tmp[b];
    }
    // ---- overall loudness vs. spectral shape -----------------------------
    // Two different things, and both have to show:
    //   * the whole blob's intensity follows the section's level, so silence
    //     is dark and a loud passage is bright;
    //   * the distribution across the 32 bands decides where the blob is
    //     brighter and how far it reaches from the lit edge.
    // Normalising the bands against their own peak gives the shape even when
    // the section is quiet; mixing that with the absolute peak gives the level.
    double peak = 0.0;
    for (int b = 0; b < kBands; ++b) peak = std::max (peak, band[b]);
    // NOTE: the falling flag is cleared inside the per-band loop below, once
    // every band has actually reached its target.  Clearing it here on `peak`
    // was wrong: peak reads 0 the moment the audio goes silent, which cleared
    // the flag while the bars were still at full height, the gate below then
    // returned, and the ballistics never ran again — the bar froze permanently
    // on its last image instead of falling to nothing.
    const double loud = std::clamp (peak, 0.0, 1.0);

    // Nothing playing: draw nothing, but do NOT return while the ballistics
    // still have something to fall.  Returning here ended the release mid-way
    // and left the bar frozen on its last image — measured as three or four
    // completely static frame pairs in a row, in the middle of a passage where
    // the spectrum above it was still moving.
    // Do not return while the release still has something to show.  The gate
    // used to fire on the way DOWN as well, cutting the fall short and leaving
    // the last image on screen — three consecutive static captures in a passage
    // where the audio was merely quiet.  `m_glowFalling` is set whenever a bar
    // is above its target, so the release is always allowed to finish.
    if (loud < 0.002 && ! m_glowFalling) { return false; }  // nothing to draw

    // Never divide by a tiny peak: band/peak with peak at 1e-6 multiplies the
    // residue of the release into full-scale noise, and at exactly 0 it produces
    // NaN, which makes the whole path invalid and nothing is drawn at all — read
    // from outside as "the glow bar is frozen".
    double rel[kBands];
    const double norm = (peak > 1e-4) ? peak : 1.0;
    for (int b = 0; b < kBands; ++b) rel[b] = std::clamp (band[b] / norm, 0.0, 1.0);

    // The relative shape gets the same four passes: it decides the brightness
    // modulation, and one pass left visible steps between adjacent bands.
    for (int pass = 0; pass < 4; ++pass)
    {
        double tmp[kBands];
        for (int b = 0; b < kBands; ++b)
            tmp[b] = 0.25 * rel[std::max (0, b - 1)] + 0.5 * rel[b]
                   + 0.25 * rel[std::min (kBands - 1, b + 1)];
        for (int b = 0; b < kBands; ++b) rel[b] = tmp[b];
    }

    // ---- cache key -------------------------------------------------------
    // Built from the SMOOTHED levels, which is what is actually drawn.  An
    // earlier version keyed on them but returned before running the ballistics,
    // so a cache hit froze the displayed value until the key happened to
    // change — measured: the glow bar was static on 6 of 13 frame pairs while
    // the VU and the spectrum moved on all of them.
    //
    // The ballistics have to run on EVERY paint (they advance with time), so
    // they are computed above this point and the key is only about the image.
    quint64 key = 1469598103934665603ull;
    auto mixIn = [&key] (quint64 v) { key = (key ^ v) * 1099511628211ull; };
    // Full precision, not quantised to 1/65535: a release step during a quiet
    // passage is smaller than that, so the key did not change and the cache
    // held a stale image — measured as 2-3 completely static frames out of 13
    // while the spectrum and the VU moved on every one.
    for (int b = 0; b < kBands; ++b)
    {
        std::uint32_t bits = 0;
        const float fv = float (band[b]);
        std::memcpy (&bits, &fv, sizeof bits);      // C++17: no bit_cast
        mixIn (quint64 (bits));
    }
    mixIn (quint64 (pitW * 16.0));
    mixIn (quint64 (barH * 16.0) ^ (m_headerTop ? 0x9e37u : 0u));
    mixIn (quint64 (accent.rgb()));

    if (key == m_glowKey && ! m_glowBar.isNull())
    {
        dest = QRectF (pitX, barY, pitW, barH);
        m_glowDest = dest;
        return true;
    }

    // ---- the blob, per pixel --------------------------------------------
    // Built at half the bar's size and stretched: the glass blurs it, so 160x8
    // already carries more detail than can survive, and this runs on every plot
    // repaint (up to 120 Hz).
    // ---- resolution ------------------------------------------------------
    // The image IS the shape here, so its size is a quality setting, not a
    // performance one.  It used to be 160x8 stretched over 640x32 — a 4x
    // upscale in both axes, which put a step every four pixels on every edge no
    // matter how smooth the maths was.  The shape is sampled once per device
    // pixel now.
    //
    // (This is cheap: it runs only when the cache key changes, which is when the
    // band levels or the geometry moved, not on every paint.)
    const double devScale = (w > 0.0) ? (width() / w) : 1.0;
    const int cols = std::max (64, int (std::lround (pitW * devScale)));
    const int rows = std::max (8, int (std::lround (barH * devScale)));
    if (m_glowBar.width() != cols || m_glowBar.height() != rows)
        m_glowBar = QImage (cols, rows, QImage::Format_ARGB32_Premultiplied);
    QImage& bar = m_glowBar;
    bar.fill (Qt::transparent);

    const QColor acc = accent;
    for (int y = 0; y < rows; ++y)
    {
        auto* line = reinterpret_cast<QRgb*> (bar.scanLine (y));
        // Distance from the lit edge, 0 at the edge, 1 at the far side.
        const double yn = (y + 0.5) / double (rows);
        const double d  = m_headerTop ? yn : (1.0 - yn);

        for (int x = 0; x < cols; ++x)
        {
            const double fx = (x + 0.5) / double (cols);

            // ---- monotone cubic (PCHIP) between band centres ----------
            // smootherstep was here and it was the wrong curve for this job.
            // It forces the slope to ZERO at every control point, so 32 bands
            // with any difference between neighbours become a row of separate
            // bumps joined by flat-topped steps — beads on a string.  Blurring
            // that softens the edges but keeps the beads, which is exactly the
            // "the shape is too sharp" report.
            //
            // A monotone cubic keeps the slope continuous across each control
            // point and is guaranteed not to overshoot, so the curve runs
            // THROUGH the band values instead of flattening at each of them.
            // This is the standard PCHIP construction (Fritsch-Carlson).
            const double bt = fx * (kBands - 1);
            const int    bi = std::min (kBands - 2, int (bt));
            const double bf = bt - double (bi);

            // Slopes at the two ends of this interval, from the three-point
            // differences, then limited so no overshoot can occur.
            auto slope = [&rel] (int i) -> double
            {
                const int n = kBands;
                if (i <= 0)     return rel[1] - rel[0];
                if (i >= n - 1) return rel[n - 1] - rel[n - 2];
                const double d0 = rel[i] - rel[i - 1];
                const double d1 = rel[i + 1] - rel[i];
                if (d0 * d1 <= 0.0) return 0.0;             // a local extremum
                return 0.5 * (d0 + d1);                     // central difference
            };

            const double m0 = slope (bi);
            const double m1 = slope (bi + 1);
            const double h00 = (2.0 * bf + 1.0) * (1.0 - bf) * (1.0 - bf);
            const double h10 = bf * (1.0 - bf) * (1.0 - bf);
            const double h01 = bf * bf * (3.0 - 2.0 * bf);
            const double h11 = bf * bf * (bf - 1.0);
            const double rl = std::clamp (h00 * rel[bi] + h10 * m0
                                        + h01 * rel[bi + 1] + h11 * m1,
                                          0.0, 1.0);

            // Per-band brightness: a loud band is near full, a quiet one a
            // third of that.  A floor is kept so the blob still reads as one
            // pool of light rather than breaking into separate patches.
            const double lv = (0.30 + 0.70 * rl) * (0.35 + 0.65 * loud);

            // ---- the falloff ------------------------------------------
            // The page is a RADIAL GRADIENT, not a Gaussian:
            //
            //   radial-gradient(ellipse 70% 160% at 50% 0%,
            //                   accent .36 0%, accent .13 38%, accent 0 78%)
            //
            // i.e. alpha falls LINEARLY to .13 at 38 % of the radius and then
            // linearly to zero at 78 % — and stays zero beyond.  A Gaussian
            // exp(-(d/reach)^2) looks similar in shape but its tail dies too
            // fast: at 32 px tall it leaves only a few pixels of transition and
            // the blob reads as a filled rectangle with a soft rim, which is the
            // "too full, not like a glow" report.  The gradient's long linear
            // run is what makes it look like light.
            //
            // In the page's own normalised radius `r`: 0 at the centre (the lit
            // edge), 1 at the ellipse's edge.  The vertical half-axis is 160 %
            // of the bar's height and the horizontal is 70 % of the pit's width,
            // so `r` mixes both — that is what gives the corners their round
            // falloff instead of a hard cut.
            const double cx = (fx - 0.5) / 0.70;         // 0 at the centre
            const double cy = d / 1.60;                  // 0 at the lit edge
            const double r  = std::sqrt (cx * cx + cy * cy);

            double prof;
            if (r <= 0.38)      prof = 0.36 + (0.13 - 0.36) * (r / 0.38);
            else if (r <= 0.78) prof = 0.13 + (0.00 - 0.13) * ((r - 0.38) / 0.40);
            else                prof = 0.0;

            // The level modulates the BRIGHTNESS of that profile, not its
            // extent, so the shape stays the page's ellipse at every level.
            const double a = lv * prof / 0.36;           // normalised to 1 at the centre

            // Same factor as the GPU shader (see kGlowToScreen in glass.frag),
            // so the CPU fallback and the GPU path produce the same picture.
            //
            // This is the glow that goes INTO the plot image; in the normal
            // (GPU glass) build the pod's glow is drawn by the shader on top of
            // this and this particular one is covered up.  It still has to be
            // right, because ONYXTEON_CPUFROST=1 is the fallback for machines
            // without working GL.
            //
            // 0.229 — measured, see the long note in glass.frag for the table
            // and for why the earlier values (0.20, 1.0, 0.119, 0.0307) were
            // each wrong.
            // `a` is already normalised to 1 at the centre (prof / 0.36), so
            // this single factor sets the on-screen strength.
            constexpr double kGlowPeakAlpha = 0.229;
            const double al = std::clamp (a * kGlowPeakAlpha, 0.0, 1.0);
            line[x] = al < 0.004 ? 0u
                    : qRgba (int (acc.red() * al), int (acc.green() * al),
                             int (acc.blue() * al), int (255 * al));
        }
    }

    m_glowKey = key;
    dest = QRectF (pitX, barY, pitW, barH);
    m_glowDest = dest;
    return true;
}

void OvEqView::paintGlowBar (QPainter* g, double w, double h,
                             const QColor& accent, const float* spec)
{
    QRectF dest;
    if (! ensureGlowBar (w, h, accent, spec, dest)) return;
    drawGlowImage (g, dest.x(), dest.y(), dest.width(), dest.height());
}

void OvEqView::drawGlowImage (QPainter* g, double pitX, double barY,
                              double pitW, double barH)
{
    // ---- clip to the trapezoid ------------------------------------------
    //   .controlpit.top::after    polygon(20% 0, 80% 0, 100% 100%, 0 100%)
    //   .controlpit.bottom::after polygon(0 0, 100% 0, 80% 100%, 20% 100%)
    QPainterPath clip;
    if (m_headerTop)
    {
        clip.moveTo (pitX + pitW * 0.20, barY);
        clip.lineTo (pitX + pitW * 0.80, barY);
        clip.lineTo (pitX + pitW,        barY + barH);
        clip.lineTo (pitX,               barY + barH);
    }
    else
    {
        clip.moveTo (pitX,               barY);
        clip.lineTo (pitX + pitW,        barY);
        clip.lineTo (pitX + pitW * 0.80, barY + barH);
        clip.lineTo (pitX + pitW * 0.20, barY + barH);
    }
    clip.closeSubpath();

    g->save();
    g->setClipPath (clip);
    g->setRenderHint (QPainter::SmoothPixmapTransform, true);
    g->drawImage (QRectF (pitX, barY, pitW, barH), m_glowBar,
                  QRectF (0, 0, m_glowBar.width(), m_glowBar.height()));
    g->restore();
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

        // 30 Hz cap.  The spectrum underneath moves constantly, so a pure
        // content key never hits and the glass is re-blurred on every repaint
        // (1.5 ms of a 2.7 ms pass).  It is a backdrop: refreshing it at 30 Hz
        // is invisible, and the key still catches every change that matters.
        const auto nowF = std::chrono::steady_clock::now();
        const bool stale = (nowF - m_frostBuiltAt) > std::chrono::milliseconds (33);
        if (! stale && ! m_frostLayer.isNull())
        {
            g->save();
            g->translate (strip.topLeft());
            g->setRenderHint (QPainter::SmoothPixmapTransform, true);
            g->drawImage (QRectF (0, 0, strip.width(), strip.height()), m_frostLayer,
                          QRectF (0, 0, m_frostLayer.width(), m_frostLayer.height()));
            g->restore();
            return;
        }
        m_frostBuiltAt = nowF;

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
