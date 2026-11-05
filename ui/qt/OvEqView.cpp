#include "OvEqView.h"
#include "OvEngine.h"
#include "OvParam.h"

#include <QPainter>
#include <QPainterPath>
#include <QCursor>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QHoverEvent>
#include <QLinearGradient>
#include <QtMath>
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

    m_repaint.setInterval (33);
    connect (&m_repaint, &QTimer::timeout, this, [this] { update(); });
    m_repaint.start();
}

void OvEqView::setEngine (OvEngine* e)
{
    if (e == m_engine) return;
    m_engine = e;
    emit engineChanged();
    update();
}

void OvEqView::setPrefix (const QString& p)
{
    if (p == m_prefix) return;
    m_prefix = p;
    emit prefixChanged();
    update();
}

void OvEqView::setAccent (const QString& c)
{
    if (c == m_accent) return;
    m_accent = c;
    emit accentChanged();
    update();
}

void OvEqView::setHeaderTop (bool t)
{
    if (t == m_headerTop) return;
    m_headerTop = t;
    emit headerTopChanged();
    update();
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
void OvEqView::paint (QPainter* g)
{
    gatherBands();
    layoutGeometry (m_plot, m_rangeDb);

    const double w = width(), h = height();
    const double top = m_plot.top(), bottom = m_plot.bottom();
    const QColor accent (m_accent);

    g->fillRect (QRectF (0, 0, width(), height()), kBg);
    g->setRenderHint (QPainter::Antialiasing, true);
    g->setRenderHint (QPainter::TextAntialiasing, true);

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

    // ---- total curve + fill (clipped to the plot, then the overflow pass)
    auto drawTotal = [&] (bool overflow)
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

        QColor fc = accent; fc.setAlphaF (overflow ? 0.03f : 0.06f);
        g->fillPath (fill, fc);

        QColor sc = accent; sc.setAlphaF (overflow ? 0.55f : 1.0f);
        g->setPen (QPen (sc, overflow ? 1.2 : 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        g->setBrush (Qt::NoBrush);
        g->drawPath (path);
    };

    g->save();
    g->setClipRect (QRectF (0, top, w, bottom - top));
    drawTotal (false);
    g->restore();

    // overflow: the curve continues into the header area, faintly — this is
    // what the frosted trapezoid reveals as a blurred copy
    g->save();
    if (m_headerTop)
        g->setClipRect (QRectF (0, 0, w, top));
    else
        g->setClipRect (QRectF (0, bottom, w, h - bottom));
    drawTotal (true);
    g->restore();

    // ---- nodes on top (clipped like the canvas: always visible in the plot)
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
        const QRectF tr (b.x - 6, b.y - 6, 12, 12);
        g->drawText (tr, Qt::AlignCenter, QString::number (i + 1));
    }
}

// ----------------------------------------------------------- interaction
int OvEqView::nodeAt (const QPointF& pos) const
{
    int best = -1;
    double bestD = 12.0;
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
    gatherBands();
    layoutGeometry (m_plot, m_rangeDb);
    for (int i = 0; i < 5; ++i)
    {
        const auto& b = m_bands[i];
        const double nodeGain = isGainType (b.type) ? b.gain * displayMult()
                                                    : bandMagAt (b, b.freq);
        m_bands[i].x = toX (b.freq, width());
        m_bands[i].y = toY (std::clamp (nodeGain, -m_rangeDb, m_rangeDb),
                            height(), m_rangeDb, m_plot.top(), m_plot.bottom());
    }

    const int hit = nodeAt (e->position());
    if (e->button() == Qt::RightButton && hit >= 0)
    {
        static const char* names[] = { "Bell", "Notch", "Low Shelf", "High Shelf",
                                       "Low Cut 12", "Low Cut 24",
                                       "High Cut 12", "High Cut 24" };
        emit menuRequested (hit, e->position(), QString::fromLatin1 (names[m_bands[hit].type]));
        e->accept();
        return;
    }

    if (hit >= 0)
    {
        m_dragBand = hit;
        m_dragGain = isGainType (m_bands[hit].type);
        m_dragStart = e->position();
        m_dragFreqNorm = normOf (m_prefix + QString::number (hit + 1) + "Freq");
        m_dragGainNorm = normOf (m_prefix + QString::number (hit + 1) + "Gain");

        if (auto* pp = m_engine ? m_engine->typedParam (m_prefix + QString::number (hit + 1) + "Freq") : nullptr)
            pp->beginGesture();
        if (m_dragGain)
            if (auto* pp = m_engine ? m_engine->typedParam (m_prefix + QString::number (hit + 1) + "Gain") : nullptr)
                pp->beginGesture();

        e->accept();
        return;
    }
    e->ignore();
}

void OvEqView::mouseMoveEvent (QMouseEvent* e)
{
    if (m_dragBand < 0) { e->ignore(); return; }

    const auto& b = m_bands[m_dragBand];
    const QString base = m_prefix + QString::number (m_dragBand + 1);

    if (! m_dragGain)
    {
        const double hz = fromX (e->position().x(), width());
        setValueOf (base + "Freq", hz);
    }
    else
    {
        const double dy = e->position().y() - m_dragStart.y();
        const double dNorm = -dy / std::max (40.0, m_plot.height()) * 2.0 * (m_rangeDb / 12.0) / displayMult();
        double n = std::clamp (m_dragGainNorm + dNorm, 0.0, 1.0);
        setNormOf (base + "Gain", n);

        // horizontal drag moves the frequency too (Pro-Q style)
        const double dx = e->position().x() - m_dragStart.x();
        if (std::abs (dx) > 0.5)
        {
            const double w = width();
            const double fx0 = toX (b.freq, w);
            const double hz  = fromX (std::clamp (fx0 + dx, 0.0, w), w);
            setValueOf (base + "Freq", hz);
            m_dragStart.setX (e->position().x());
            m_bands[m_dragBand].freq = hz;
        }
    }

    QString tip = QStringLiteral ("#%1 %2 · %3 Hz").arg (m_dragBand + 1)
                    .arg (textOf (base + "Type"))
                    .arg (fmtHz (valueOf (base + "Freq", 0.0)));
    if (isGainType (b.type))
        tip += QStringLiteral (" · %1 dB").arg (valueOf (base + "Gain", 0.0) * displayMult(), 0, 'f', 1);
    if (! isShelf (b.type))
        tip += QStringLiteral (" · Q %1").arg (valueOf (base + "Q", 0.0), 0, 'f', 2);

    emit hoverInfo (tip, e->position());
    update();
    e->accept();
}

void OvEqView::mouseReleaseEvent (QMouseEvent* e)
{
    if (m_dragBand >= 0)
    {
        const QString base = m_prefix + QString::number (m_dragBand + 1);
        if (auto* pp = m_engine ? m_engine->typedParam (base + "Freq") : nullptr) pp->endGesture();
        if (m_dragGain)
            if (auto* pp = m_engine ? m_engine->typedParam (base + "Gain") : nullptr) pp->endGesture();
        m_dragBand = -1;
        emit hoverEnded();
    }
    e->accept();
}

void OvEqView::mouseDoubleClickEvent (QMouseEvent* e)
{
    gatherBands();
    layoutGeometry (m_plot, m_rangeDb);
    for (int i = 0; i < 5; ++i)
    {
        m_bands[i].x = toX (m_bands[i].freq, width());
        const double nodeGain = isGainType (m_bands[i].type) ? m_bands[i].gain * displayMult()
                                                             : bandMagAt (m_bands[i], m_bands[i].freq);
        m_bands[i].y = toY (std::clamp (nodeGain, -m_rangeDb, m_rangeDb),
                            height(), m_rangeDb, m_plot.top(), m_plot.bottom());
    }
    const int hit = nodeAt (e->position());
    if (hit >= 0)
    {
        // double click = flatten this band back into the pool (0 dB bell/shelf)
        const QString base = m_prefix + QString::number (hit + 1);
        if (auto* pp = m_engine ? m_engine->typedParam (base + "Gain") : nullptr)
            pp->resetToDefault();
        update();
    }
    e->accept();
}

void OvEqView::wheelEvent (QWheelEvent* e)
{
    gatherBands();
    layoutGeometry (m_plot, m_rangeDb);
    for (int i = 0; i < 5; ++i)
    {
        m_bands[i].x = toX (m_bands[i].freq, width());
        const double nodeGain = isGainType (m_bands[i].type) ? m_bands[i].gain * displayMult()
                                                             : bandMagAt (m_bands[i], m_bands[i].freq);
        m_bands[i].y = toY (std::clamp (nodeGain, -m_rangeDb, m_rangeDb),
                            height(), m_rangeDb, m_plot.top(), m_plot.bottom());
    }

    const int hit = nodeAt (e->position());
    if (hit >= 0)
    {
        const QString base = m_prefix + QString::number (hit + 1);
        const double dir = e->angleDelta().y() > 0 ? 1.0 : -1.0;
        double q = valueOf (base + "Q", 0.707);
        q = std::clamp (q * (dir > 0 ? 1.12 : 1.0 / 1.12), 0.1, 6.0);
        setValueOf (base + "Q", q);
        update();
        e->accept();
        return;
    }
    e->ignore();
}

void OvEqView::hoverMoveEvent (QHoverEvent* e)
{
    gatherBands();
    layoutGeometry (m_plot, m_rangeDb);
    for (int i = 0; i < 5; ++i)
    {
        m_bands[i].x = toX (m_bands[i].freq, width());
        const double nodeGain = isGainType (m_bands[i].type) ? m_bands[i].gain * displayMult()
                                                             : bandMagAt (m_bands[i], m_bands[i].freq);
        m_bands[i].y = toY (std::clamp (nodeGain, -m_rangeDb, m_rangeDb),
                            height(), m_rangeDb, m_plot.top(), m_plot.bottom());
    }

    const int hit = nodeAt (e->position());
    m_hoverBand = hit;
    setCursor (QCursor (hit >= 0 ? Qt::SizeAllCursor : Qt::ArrowCursor));

    if (hit >= 0)
    {
        const QString base = m_prefix + QString::number (hit + 1);
        QString tip = QStringLiteral ("#%1 %2 · %3 Hz").arg (hit + 1)
                        .arg (textOf (base + "Type"))
                        .arg (fmtHz (valueOf (base + "Freq", 0.0)));
        if (isGainType (m_bands[hit].type))
            tip += QStringLiteral (" · %1 dB").arg (valueOf (base + "Gain", 0.0) * displayMult(), 0, 'f', 1);
        if (! isShelf (m_bands[hit].type))
            tip += QStringLiteral (" · Q %1").arg (valueOf (base + "Q", 0.0), 0, 'f', 2);
        emit hoverInfo (tip, e->position());
    }
    else
        emit hoverEnded();
}

void OvEqView::hoverLeaveEvent (QHoverEvent*)
{
    m_hoverBand = -1;
    emit hoverEnded();
}
