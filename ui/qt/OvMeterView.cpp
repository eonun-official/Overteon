#include "OvMeterView.h"
#include "OvEngine.h"

#include <QPainter>
#include <QElapsedTimer>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace
{
    const QColor kOutBright ("#fa3c7f");
    const QColor kOutTrail  ("#6f2946");
    const QColor kInBright  ("#23ecd7");
    const QColor kInTrail   ("#155d58");
    const QColor kTrack     ("#2c313b");
    const QColor kLabel     ("#7a8290");
    const QColor kPct       ("#f2f4f8");

    // The four hook paths, verbatim from the design's viewBox 0 0 480 64.
    QPainterPath makeHooked (bool left, bool top)
    {
        QPainterPath p;
        const double x0 = left ? 213.78 : 266.22;
        const double x1 = left ? 210.50 : 269.50;
        const double x2 = left ? 210.79 : 269.21;
        const double xEnd = left ? 31.0 : 449.0;

        const double yHook = top ? 13.65 : 50.35;
        const double yC1   = top ? 18.32 : 45.68;
        const double yC2   = top ? 20.57 : 43.43;

        p.moveTo (x0, yHook);
        p.cubicTo (x1, yC1, x2, yC2, left ? 204.50 : 275.50, yC2);
        p.lineTo (xEnd, yC2);
        return p;
    }

    // QPainterPath has no trim(); sample the path and rebuild the visible part
    // so a level can be drawn as a partial stroke (the SVG used stroke-dash).
    QPainterPath partial (const QPainterPath& p, double frac)
    {
        QPainterPath out;
        frac = std::clamp (frac, 0.0, 1.0);
        if (frac <= 0.0) return out;

        constexpr int kSamples = 96;
        out.moveTo (p.pointAtPercent (0.0));
        for (int i = 1; i <= kSamples; ++i)
        {
            const double t = frac * (double) i / (double) kSamples;
            out.lineTo (p.pointAtPercent (std::min (0.999999, t)));
        }
        return out;
    }

    double levelToFraction (double db)
    {
        return std::clamp ((db + 60.0) / 60.0, 0.0, 1.0);
    }

    // green plateau -> amber -> red, with a hot red in the last 3 %
    QColor harmColor (double f)
    {
        auto mix = [] (QColor a, QColor b, double t)
        {
            return QColor (int (a.red()   + (b.red()   - a.red())   * t),
                           int (a.green() + (b.green() - a.green()) * t),
                           int (a.blue()  + (b.blue()  - a.blue())  * t));
        };
        const QColor G ("#23ecd7"), A ("#f0a23b"), R ("#fa3c7f"), X ("#ff2a44");
        if (f >= 0.97) return X;
        if (f < 0.45)  return G;
        if (f < 0.60)  return mix (G, A, (f - 0.45) / 0.15);
        if (f < 0.75)  return A;
        if (f < 0.88)  return mix (A, R, (f - 0.75) / 0.13);
        return R;
    }
}

OvMeterView::OvMeterView (QQuickItem* parent) : QQuickPaintedItem (parent)
{
    setAntialiasing (true);

    m_outL.path = makeHooked (true,  true);
    m_outR.path = makeHooked (false, true);
    m_inL.path  = makeHooked (true,  false);
    m_inR.path  = makeHooked (false, false);

    m_timer.setInterval (33);
    connect (&m_timer, &QTimer::timeout, this, [this] { advance (0.033); update(); });
    m_timer.start();
}

void OvMeterView::setEngine (OvEngine* e)
{
    if (e == m_engine) return;
    m_engine = e;
    emit engineChanged();
    update();
}

void OvMeterView::advance (double dt)
{
    m_clock += dt;
    if (m_engine == nullptr) return;

    const auto m = m_engine->source().meters();

    struct T { Channel* ch; double db; };
    const T list[] { { &m_outL, m.outL }, { &m_outR, m.outR },
                     { &m_inL, m.inL },   { &m_inR, m.inR } };

    for (const auto& t : list)
    {
        const double level = levelToFraction (t.db);
        if (level >= t.ch->p)
        {
            t.ch->p = level;
            t.ch->holdUntil = m_clock + 1.1;
        }
        else if (m_clock > t.ch->holdUntil)
        {
            t.ch->p = std::max (level, t.ch->p - dt * 0.18);
        }

        if (level >= t.ch->peak)
        {
            t.ch->peak = level;
            t.ch->holdUntil = m_clock + 1.1;
        }
        else if (m_clock > t.ch->holdUntil)
        {
            t.ch->peak = std::max (level, t.ch->peak - dt * 0.18);
        }
    }

    const double f = std::clamp ((double (m.harm) + 48.0) / 48.0, 0.0, 1.0);
    m_harmFrac = f;
}

void OvMeterView::paint (QPainter* g)
{
    const double sx = width() / 480.0;
    const double sy = height() / 64.0;
    g->scale (sx, sy);
    g->setRenderHint (QPainter::Antialiasing, true);

    // ---- static hook guides --------------------------------------------
    g->setPen (QPen (kTrack, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    g->setBrush (Qt::NoBrush);
    for (const auto* p : { &m_outL.path, &m_outR.path, &m_inL.path, &m_inR.path })
        g->drawPath (*p);

    // ---- harmonic ring (drawn first so the bars overlap it like the SVG)
    const double frac = m_harmFrac;
    const QColor hc = harmColor (frac);
    const QRectF ring (240 - 28, 32 - 28, 56, 56);

    g->setBrush (Qt::NoBrush);
    QColor guide = QColor ("#3b4350"); guide.setAlphaF (0.07f);
    g->setPen (QPen (guide, 3.0));
    g->drawEllipse (ring);

    // glow: wide, low-alpha stroke underneath the crisp arc
    QColor glow = hc; glow.setAlphaF (0.30f);
    g->setPen (QPen (glow, 8.0, Qt::SolidLine, Qt::RoundCap));
    g->drawArc (ring, 90 * 16, -int (frac * 360.0 * 16.0));

    g->setPen (QPen (hc, 4.0, Qt::SolidLine, Qt::RoundCap));
    g->drawArc (ring, 90 * 16, -int (frac * 360.0 * 16.0));

    // ---- meter bars -----------------------------------------------------
    struct Bar { const Channel* ch; QColor bright, trail; };
    const Bar bars[] { { &m_outL, kOutBright, kOutTrail }, { &m_outR, kOutBright, kOutTrail },
                       { &m_inL,  kInBright,  kInTrail  }, { &m_inR,  kInBright,  kInTrail  } };

    for (const auto& b : bars)
    {
        if (b.ch->peak > 0.002)
        {
            g->setPen (QPen (b.trail, 2.6, Qt::SolidLine, Qt::RoundCap));
            g->drawPath (partial (b.ch->path, b.ch->peak));
        }
        if (b.ch->p > 0.002)
        {
            QColor glowCol = b.bright; glowCol.setAlphaF (0.28f);
            g->setPen (QPen (glowCol, 6.0, Qt::SolidLine, Qt::RoundCap));
            g->drawPath (partial (b.ch->path, b.ch->p));

            g->setPen (QPen (b.bright, 1.8, Qt::SolidLine, Qt::RoundCap));
            g->drawPath (partial (b.ch->path, b.ch->p));

            const QPointF tip = b.ch->path.pointAtPercent (std::min (0.999, b.ch->p));
            g->setPen (Qt::NoPen);
            g->setBrush (b.bright);
            g->drawEllipse (tip, 1.7, 1.7);
        }
    }

    // ---- text -----------------------------------------------------------
    QFont f = g->font();
    f.setFamily (QStringLiteral ("Inter"));
    f.setPixelSize (15);
    f.setBold (true);
    g->setFont (f);

    const bool schranz = m_engine != nullptr && m_engine->driveMode() == 2;
    if (schranz)
    {
        g->setPen (QColor ("#ff2a44"));
        g->drawText (QRectF (200, 20, 80, 20), Qt::AlignCenter, QStringLiteral ("N/A"));
    }
    else
    {
        g->setPen (frac >= 0.97 ? QColor ("#ff2a44") : kPct);
        g->drawText (QRectF (200, 20, 80, 20), Qt::AlignCenter,
                     QString::number (qRound (frac * 100)) + "%");
    }

    f.setPixelSize (7);
    f.setBold (false);
    f.setLetterSpacing (QFont::AbsoluteSpacing, 2);
    g->setFont (f);
    g->setPen (kLabel);
    g->drawText (QRectF (200, 43, 80, 10), Qt::AlignCenter, QStringLiteral ("HARM"));

    // IN / OUT captions and L / R marks
    f.setPixelSize (7);
    f.setLetterSpacing (QFont::AbsoluteSpacing, 0.5);
    g->setFont (f);
    g->drawText (QRectF (13, 16, 30, 10), Qt::AlignLeft,  QStringLiteral ("OUT"));
    g->drawText (QRectF (13, 38, 30, 10), Qt::AlignLeft,  QStringLiteral ("IN"));
    g->drawText (QRectF (437, 16, 31, 10), Qt::AlignRight, QStringLiteral ("OUT"));
    g->drawText (QRectF (437, 38, 31, 10), Qt::AlignRight, QStringLiteral ("IN"));

    f.setPixelSize (10);
    f.setBold (true);
    f.setLetterSpacing (QFont::AbsoluteSpacing, 0);
    g->setFont (f);
    g->setPen (kOutBright);
    g->drawText (QRectF (0, 14, 12, 12), Qt::AlignLeft, QStringLiteral ("L"));
    g->drawText (QRectF (468, 14, 12, 12), Qt::AlignRight, QStringLiteral ("R"));
    g->setPen (kInBright);
    g->drawText (QRectF (0, 36, 12, 12), Qt::AlignLeft, QStringLiteral ("L"));
    g->drawText (QRectF (468, 36, 12, 12), Qt::AlignRight, QStringLiteral ("R"));
}
