#include "OvEqView.h"
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
    m_repaint.setInterval (OvTiming::kPlotMs);
    connect (&m_repaint, &QTimer::timeout, this, [this] {
        // A repaint costs a full plot render plus a 1040x386 texture upload,
        // so it only happens when the spectrum moved or the parameters
        // changed — not on a fixed cadence.
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
        // the plot depends on the parameters, the spectrum and the glow
        connect (m_engine, &OvEngine::paramRevisionChanged, this, [this] { m_dirty = true; });
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
    if (qFuzzyCompare (v, m_glow)) return;
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

// ------------------------------------------------------------------ paint
//  Everything is rendered into an off-screen image first (m_content).  That
//  image is the exact equivalent of the four stacked <canvas> elements in the
//  design — grid, spectrum, curve, overflow fog — and it doubles as the source
//  for the frosted glass, which is why the blur can be computed on the CPU the
//  same way FrostEngine does it in the HTML.
void OvEqView::paint (QPainter* g)
{
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

    // the off-screen buffer, at the window's device pixel ratio
    const qreal dpr = window() != nullptr ? window()->devicePixelRatio() : 1.0;
    const int pw = int (std::lround (w * dpr)), ph = int (std::lround (h * dpr));
    if (m_content.width() != pw || m_content.height() != ph)
    {
        m_content = QImage (std::max (1, pw), std::max (1, ph),
                            QImage::Format_ARGB32_Premultiplied);
        m_content.setDevicePixelRatio (dpr);
    }

    {
        QPainter cg (&m_content);
        cg.setRenderHint (QPainter::Antialiasing, true);
        cg.setRenderHint (QPainter::TextAntialiasing, true);
        cg.scale (dpr, dpr);
        paintContent (&cg, w, h, m_plot.top(), m_plot.bottom(), accent);
    }

    g->setRenderHint (QPainter::SmoothPixmapTransform, true);
    g->drawImage (QRectF (0, 0, w, h), m_content);
}

void OvEqView::paintContent (QPainter* g, double w, double h,
                             double top, double bottom, const QColor& accent)
{
    g->fillRect (QRectF (0, 0, w, h), kBg);

    // ---- vertical frequency grid: full height, so both EQ sections read
    //      as one continuous background (exactly like the canvas version)
    static const int minors[] = { 20, 30, 40, 50, 60, 70, 80, 90, 200, 300, 400, 500,
                                  600, 700, 800, 900, 2000, 3000, 4000, 5000, 6000,
                                  7000, 8000, 9000, 20000 };
    static const int majors[] = { 10, 100, 1000, 10000 };

    g->setPen (QPen (kMinor, 1.0));
    for (int f : minors)
    {
        const double x = toX (f, w);
        g->drawLine (QPointF (x, 0), QPointF (x, h));
    }
    g->setPen (QPen (kMajor, 1.0));
    for (int f : majors)
    {
        const double x = toX (f, w);
        g->drawLine (QPointF (x, 0), QPointF (x, h));
    }

    // ---- horizontal dB lines (plot only)
    const double step = m_rangeDb <= 3.0 ? 1.0 : m_rangeDb <= 6.0 ? 2.0
                      : m_rangeDb <= 12.0 ? 4.0 : m_rangeDb <= 24.0 ? 8.0
                      : m_rangeDb <= 30.0 ? 10.0 : m_rangeDb <= 48.0 ? 16.0 : 30.0;
    for (double db = -m_rangeDb; db <= m_rangeDb + 1e-6; db += step)
    {
        const double y = toY (db, h, m_rangeDb, top, bottom);
        g->setPen (QPen (std::abs (db) < 1e-6 ? kZero : kHLine, 1.0));
        g->drawLine (QPointF (0, y), QPointF (w, y));
    }

    // ---- frequency labels: inside the plot edge and up in the header band
    QFont f9 = g->font();
    f9.setPixelSize (9);
    f9.setFamily (QStringLiteral ("Inter"));
    g->setFont (f9);
    g->setPen (kLabel);
    static const int labels[] = { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };
    static const char* labelText[] = { "20", "50", "100", "200", "500", "1k",
                                       "2k", "5k", "10k", "20k" };
    for (int i = 0; i < 10; ++i)
    {
        const double x = toX (labels[i], w) + 2.0;
        g->drawText (QPointF (x, bottom - 4.0), QString::fromLatin1 (labelText[i]));
        g->drawText (QPointF (x, m_headerTop ? 22.0 : h - 8.0),
                     QString::fromLatin1 (labelText[i]));
    }

    // ---- dB labels (accent tinted, inside the plot)
    QColor dbInk = accent;
    dbInk.setAlphaF (0.85f);
    g->setPen (dbInk);
    for (double db = -m_rangeDb; db <= m_rangeDb + 1e-6; db += step)
    {
        if (std::abs (db) < 1e-6) continue;
        const double y = toY (db, h, m_rangeDb, top, bottom);
        const QString t = (db > 0 ? "+" : "") + QString::number (qRound (db));
        g->drawText (QPointF (6.0, std::clamp (y - 2.0, top + 9.0, bottom - 2.0)), t);
    }

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

    // ---- per-band curves + node positions
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
            g->drawPath (path);
        }

        const double nodeGain = isGainType (b.type) ? b.gain * displayMult()
                                                    : bandMagAt (b, b.freq);
        b.x = toX (b.freq, w);
        b.y = toY (std::clamp (nodeGain, -m_rangeDb, m_rangeDb), h, m_rangeDb, top, bottom);
    }

    // ---- total curve + fill (clipped to the plot)
    auto drawTotal = [&]
    {
        QPainterPath path;
        for (double x = 0; x <= w; x += 1.0)
        {
            const double y = toY (totalMagAt (fromX (x, w)), h, m_rangeDb, top, bottom);
            if (x == 0.0) path.moveTo (x, y); else path.lineTo (x, y);
        }

        QPainterPath fill = path;
        fill.lineTo (w, toY (0.0, h, m_rangeDb, top, bottom));
        fill.lineTo (0.0, toY (0.0, h, m_rangeDb, top, bottom));
        fill.closeSubpath();

        QColor fc = accent; fc.setAlphaF (0.06f);
        g->fillPath (fill, fc);

        g->setPen (QPen (accent, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        g->setBrush (Qt::NoBrush);
        g->drawPath (path);
    };

    g->save();
    g->setClipRect (QRectF (0, top, w, bottom - top));
    drawTotal ();
    g->restore();

    // Overflow fog (requestEQOverflow / drawEQOverflow in the design): the
    // curve keeps going past the plot edge in 12 graded bands, each one
    // blurrier and fainter than the last — blur(0.4 + k*0.55 px) and
    // alpha (1 - k/12)^1.15.
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
        double prevY = std::clamp (toY (totalMagAt (fromX (0.0, w)), h, m_rangeDb, top, bottom),
                                   std::min (edge, edge + dir * kRange),
                                   std::max (edge, edge + dir * kRange));
        for (double x = 1.0; x <= w; x += 1.0)
        {
            const double yy = std::clamp (toY (totalMagAt (fromX (x, w)), h, m_rangeDb, top, bottom),
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

    // ---- nodes on top
    for (int i = 0; i < 5; ++i)
    {
        const auto& b = m_bands[i];
        const double r = b.active ? 6.5 : 5.0;
        g->setPen (Qt::NoPen);
        QColor fill = accent;
        fill.setAlphaF (b.active ? 1.0f : 0.5f);
        g->setBrush (fill);
        g->drawEllipse (QPointF (b.x, b.y), r, r);

        QFont nf = f9;
        nf.setPixelSize (10);
        nf.setBold (true);
        g->setFont (nf);
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
    const double dpr = content.devicePixelRatio() > 0 ? content.devicePixelRatio() : 1.0;

    auto blurredRect = [&] (const QRectF& r) -> QImage
    {
        // +8 px of bleed so the filter never clamps at the rim
        QRectF bled = r.adjusted (-8, -8, 8, 8).intersected (QRectF (0, 0, w, h));
        const QRect src (int (bled.x() * dpr), int (bled.y() * dpr),
                         int (bled.width() * dpr), int (bled.height() * dpr));
        QImage crop = content.copy (src);
        crop.setDevicePixelRatio (1.0);
        // CSS blur(3px) == Gaussian std-dev 3 px; the crop is in device pixels
        QImage b = gaussianBlur (crop, 3.0 * dpr, 0.85);
        b.setDevicePixelRatio (dpr);

        if (std::getenv ("ONYXTEON_FROSTDEBUG") != nullptr)
        {
            auto stats = [] (const QImage& im)
            {
                double sum = 0, sum2 = 0; long n = 0;
                for (int y = 0; y < im.height(); y += 2)
                {
                    const auto* p = reinterpret_cast<const QRgb*> (im.constScanLine (y));
                    for (int x = 0; x < im.width(); x += 2)
                    { const double v = qGray (p[x]); sum += v; sum2 += v * v; ++n; }
                }
                const double m = sum / n;
                return std::pair<double,double> { m, std::sqrt (sum2 / n - m * m) };
            };
            const auto a = stats (crop), bb = stats (b);
            std::fprintf (stderr, "[frost] %s src=%dx%d dpr=%.2f crop(mean=%.1f sd=%.2f) blur(mean=%.1f sd=%.2f)\n",
                          m_prefix.toStdString().c_str(), crop.width(), crop.height(), dpr,
                          a.first, a.second, bb.first, bb.second);
        }
        return b;
    };

    if (std::getenv ("ONYXTEON_NOFROST") != nullptr) return;

    // ---- control pit ---------------------------------------------------
    const double pitW = std::min (double (OvThemePitWidth), w);
    const double pitX = (w - pitW) * 0.5;
    const double barH = OvThemeBarHeight;
    const double barY = m_headerTop ? 0.0 : h - barH;

    {
        const QRectF strip (pitX, barY, pitW, barH);
        const QImage b = blurredRect (strip);

        QPainterPath trap;
        if (m_headerTop)
        {
            trap.moveTo (pitX + pitW * 0.20, barY);
            trap.lineTo (pitX + pitW * 0.80, barY);
            trap.lineTo (pitX + pitW, barY + barH);
            trap.lineTo (pitX, barY + barH);
        }
        else
        {
            trap.moveTo (pitX, barY);
            trap.lineTo (pitX + pitW, barY);
            trap.lineTo (pitX + pitW * 0.80, barY + barH);
            trap.lineTo (pitX + pitW * 0.20, barY + barH);
        }
        trap.closeSubpath();

        g->save();
        g->setClipPath (trap);
        g->drawImage (strip, b, QRectF (0, 0, b.width(), b.height()));

        // background: rgba(255,255,255,.018)
        g->fillRect (strip, QColor (255, 255, 255, int (std::lround (0.018 * 255.0))));


        // .controlpit::before — the accent glow band.
        //
        //   background: radial-gradient(ellipse 70% 160% at 50% 0%,
        //               accent .36 0%, accent .13 38%, accent 0 78%)
        //   opacity: var(--light) = level²      (transition: opacity .1s linear)
        //
        // Two things this must get right, both were wrong before:
        //   * the gradient's alphas are STATIC (.36/.13/0) — the meter only
        //     drives the layer OPACITY.  Squaring the level again here made the
        //     glow level⁴, i.e. invisible at any sane meter reading.
        //   * the gradient stops at 78 %, not 100 %, so the band has a flat
        //     core and fades out well before the bar edge.
        if (m_glow > 0.001)
        {
            const QPointF anchor (pitX + pitW * 0.5, m_headerTop ? barY : barY + barH);

            g->save();
            g->setOpacity (std::clamp (m_glow, 0.0, 1.0));   // --light
            g->translate (anchor);
            g->scale (pitW * 0.70, barH * 1.60);             // ellipse 70% 160%
            QRadialGradient grad (0, 0, 1);
            QColor c0 = accent; c0.setAlphaF (0.36);
            QColor c1 = accent; c1.setAlphaF (0.13);
            QColor c2 = accent; c2.setAlphaF (0.00);
            grad.setColorAt (0.00, c0);
            grad.setColorAt (0.38, c1);
            grad.setColorAt (0.78, c2);
            grad.setColorAt (1.00, c2);
            g->setBrush (grad);
            g->setPen (Qt::NoPen);
            g->drawRect (QRectF (-1, -1, 2, 2));
            g->restore();
        }
        g->restore();
    }

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
