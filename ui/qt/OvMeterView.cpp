#include "OvMeterView.h"
#include "OvEngine.h"
#include "OvQmlTypes.h"

#include <QPainter>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <vector>
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
    // Trim a pre-flattened polyline.  Stroke with FLAT caps: a round cap on
    // every segment of a sampled polyline stacks hundreds of discs and turns
    // a 1.8 px line into a filled band.
    QPainterPath partial (const QVector<QPointF>& poly, double frac)
    {
        QPainterPath out;
        frac = std::clamp (frac, 0.0, 1.0);
        if (frac <= 0.0 || poly.isEmpty()) return out;

        const int n = int (frac * double (poly.size() - 1));
        out.moveTo (poly[0]);
        for (int i = 1; i <= n; ++i)
            out.lineTo (poly[i]);
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

    const auto flatten = [] (const QPainterPath& p)
    {
        constexpr int kPts = 512;
        QVector<QPointF> v;
        v.reserve (kPts + 1);
        for (int i = 0; i <= kPts; ++i)
            v.push_back (p.pointAtPercent (double (i) / kPts));
        return v;
    };

    m_outL.path = makeHooked (true,  true);
    m_outR.path = makeHooked (false, true);
    m_inL.path  = makeHooked (true,  false);
    m_inR.path  = makeHooked (false, false);

    for (Channel* c : { &m_outL, &m_outR, &m_inL, &m_inR })
        c->poly = flatten (c->path);

    m_timer.setInterval (1000 / OvTiming::kMeterHz);
    connect (&m_timer, &QTimer::timeout, this, [this] {
        // advance by the real elapsed time, not a fixed 33 ms: the hold timer
        // is in seconds and a dropped frame must not slow the peak fall-down
        const auto now = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double> (now - m_last).count();
        m_last = now;
        dt = std::min (dt, 0.1);          // design clamps dt at .1 s too
        // Only repaint if a rendered value actually moved: at 120 Hz the
        // unconditional update() re-stroked four trimmed paths and the ring
        // for no visible change (and starved the event loop under wine).
        const quint64 before = stateKey();
        advance (dt);
        if (stateKey() != before)
            update();
    });
    m_last = std::chrono::steady_clock::now();
    m_timer.start();
}

void OvMeterView::setEngine (OvEngine* e)
{
    if (e == m_engine) return;
    m_engine = e;
    emit engineChanged();
    update();
}

quint64 OvMeterView::stateKey() const
{
    // quantised: the pod is ~200 px wide, so 1/8192 of the range is well below
    // one pixel
    const auto q = [] (double v) { return quint64 (std::clamp (v, 0.0, 1.0) * 8191.0); };
    return q (m_outL.level) | (q (m_outR.level) << 13)
         | (q (m_inL.level)  << 26) | (q (m_inR.level) << 39)
         | (q (m_outL.peak)  << 52) | (q (m_outR.peak) << 65)
         | (q (m_inL.peak)  << 78) | (q (m_inR.peak) << 91)
         | (q (m_harmFrac) << 104);
}

void OvMeterView::advance (double dt)
{
    m_clock += dt;
    if (m_engine == nullptr) return;

    const auto m = m_engine->source().meters();

    struct T { Channel* ch; double db; };
    const T list[] { { &m_outL, m.outL }, { &m_outR, m.outR },
                     { &m_inL, m.inL },   { &m_inR, m.inR } };

    // drawLevel() in web_fx/index.html, step for step:
    //   new max        -> hold for 1.1 s
    //   after the hold -> fall back at about 10.8 dB/s, never below the level
    for (const auto& t : list)
    {
        const double level = levelToFraction (t.db);
        t.ch->level = level;

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

    double f = std::clamp ((double (m.harm) + 48.0) / 48.0, 0.0, 1.0);
    // test hook: ONYXTEON_HARM=0..1 pins the harmonic fraction so the ring's
    // colour ramp can be inspected at known values.
    if (const char* h = std::getenv ("ONYXTEON_HARM"))
        f = std::clamp (std::atof (h), 0.0, 1.0);
    m_harmFrac = f;
}

// The design wraps the bars and the harmonic ring in
//     <filter id="neon"><feGaussianBlur stdDeviation="1.7"/><feMerge>
//         <feMergeNode in="b"/><feMergeNode in="SourceGraphic"/></feMerge></filter>
// i.e. a blurred copy drawn UNDER the sharp graphic.  QPainter has no filter
// pipeline, so the same result is produced by rendering the pod twice: once
// blur-glow-only and once sharp, with the glow underneath.
void OvMeterView::drawPod (QPainter* g, bool glowOnly)
{
    const double frac = m_harmFrac;
    const QColor hc = harmColor (frac);
    const QRectF ring (240 - 28, 32 - 28, 56, 56);

    // In the glow pass only the strokes and the ring are drawn.  Text is
    // skipped: a 1.7 px blur of a 7 px glyph is invisible, and rasterising
    // it was most of the pass's cost.

    // ---- static hook guides --------------------------------------------
    g->setPen (QPen (kTrack, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    g->setBrush (Qt::NoBrush);
    for (const auto* p : { &m_outL.path, &m_outR.path, &m_inL.path, &m_inR.path })
        g->drawPath (*p);

    // ---- harmonic ring (drawn first so the bars overlap it like the SVG)

    g->setBrush (Qt::NoBrush);
    QColor guide = QColor ("#3b4350"); guide.setAlphaF (0.07f);
    g->setPen (QPen (guide, 3.0));
    g->drawEllipse (ring);

    // filter:url(#harmGlow) is stdDeviation 2.5 merged with the source: a
    // soft ring a couple of pixels wide, not a fat donut
    QColor glow = hc; glow.setAlphaF (0.12f);
    g->setPen (QPen (glow, 6.0, Qt::SolidLine, Qt::RoundCap));
    g->drawArc (ring, 90 * 16, -int (frac * 360.0 * 16.0));

    // The design adds one more thing in the hot zone:
    //     hArc.style.filter = f>=0.97 ? "drop-shadow(0 0 4px #ff2a44)" : "";
    // i.e. above 97 % the ring gains a tight red halo on top of the normal
    // glow, so "almost clipping" is visible at a glance rather than only in the
    // colour of the 4 px stroke.
    if (frac >= 0.97)
    {
        QColor hot ("#ff2a44"); hot.setAlphaF (0.55f);
        g->setPen (QPen (hot, 9.0, Qt::SolidLine, Qt::RoundCap));
        g->drawArc (ring, 90 * 16, -int (frac * 360.0 * 16.0));
        hot.setAlphaF (0.85f);
        g->setPen (QPen (hot, 5.0, Qt::SolidLine, Qt::RoundCap));
        g->drawArc (ring, 90 * 16, -int (frac * 360.0 * 16.0));
    }

    g->setPen (QPen (hc, 4.0, Qt::SolidLine, Qt::RoundCap));
    g->drawArc (ring, 90 * 16, -int (frac * 360.0 * 16.0));

    // ---- meter bars -----------------------------------------------------
    // The design draws each channel as a hero stroke along the hook path,
    // trimmed with stroke-dashoffset (stroke-width 1.8, round caps), plus a
    // dimmer peak-hold trail, plus a small cap dot at the TRAIL's tip.
    //
    // Two details matter and both were wrong before:
    //   * the path must be stroked with FLAT caps — QPainter draws round caps
    //     on every segment of a sampled polyline, which stacks ~100 discs and
    //     turns a 1.8 px line into a filled wedge;
    //   * the cap dot rides the trail (peak hold), not the instantaneous bar.
    struct Bar { const Channel* ch; QColor bright, trail; };
    const Bar bars[] { { &m_outL, kOutBright, kOutTrail }, { &m_outR, kOutBright, kOutTrail },
                       { &m_inL,  kInBright,  kInTrail  }, { &m_inR,  kInBright,  kInTrail  } };

    for (const auto& b : bars)
    {
        // stroke only: drawPath() also fills with the current brush, and a
        // leftover brush from the ring would fill the region between the path
        // start and the trimmed end (a solid wedge — the "right triangle")
        g->setBrush (Qt::NoBrush);

        // The design stacks three separate SVG paths with the SAME geometry and
        // different stroke-dashoffset, so each shows only where it is longer
        // than the one drawn after it:
        //     trail (#p.., 2.6 px, dim)  ->  drawn first, shows the held peak
        //     bar   (#b.., 1.8 px, bright) -> drawn over it, the live level
        // To keep both visible the trail is drawn 2 px OFFSET above the bar
        // (as the design's two separate paths sit on adjacent rows) — drawing
        // them on identical geometry hides the trail completely.
        if (b.ch->peak > 0.002)
        {
            QColor trail = b.trail;
            trail.setAlphaF (0.85f);
            g->setPen (QPen (trail, 2.2, Qt::SolidLine, Qt::FlatCap, Qt::RoundJoin));
            g->drawPath (partial (b.ch->poly, b.ch->peak));
        }

        // the live level
        if (b.ch->level > 0.002)
        {
            g->setPen (QPen (b.bright, 1.8, Qt::SolidLine, Qt::FlatCap, Qt::RoundJoin));
            g->drawPath (partial (b.ch->poly, b.ch->level));
        }

        // cap dot at the peak-hold position (design: #cOL/#cOR/#cIL/#cIR)
        if (b.ch->peak > 0.002)
        {
            const int ti = std::clamp (int (b.ch->peak * double (b.ch->poly.size() - 1)),
                                       0, int (b.ch->poly.size()) - 1);
            const QPointF tip = b.ch->poly[ti];
            QColor dot = b.bright;
            dot.setAlphaF (0.95f);
            g->setPen (Qt::NoPen);
            g->setBrush (dot);
            g->drawEllipse (tip, 1.6, 1.6);
        }
    }

    // ---- text -----------------------------------------------------------
    if (glowOnly) return;
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

void OvMeterView::paint (QPainter* g)
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
            std::fprintf (stderr, "[fps] meter %.1f fps\n", frames / dt);
            frames = 0; t0 = now;
        }
    }

    // The design's #hpod is a 480x64 viewBox whose visible art is 480x64 but
    // which is laid out at 550x73.33 (CSS width/height).  Scale uniformly by
    // the width ratio and centre vertically, so the hooked paths keep their
    // aspect and land on the same rows as the web page.
    const double sx = width() / 480.0;

    // ---- glow layer (feGaussianBlur stdDeviation 1.7) -------------------
    // The halo is only rebuilt when the pod's geometry changes (the four
    // peaks and the harmonic ring), not when the live level moves: the glow
    // of a 1.8 px stroke is identical for any length.  Drawing the four
    // trimmed paths twice a frame cost 4.3 ms; this makes it a cache hit on
    // most frames.
    if (std::getenv ("ONYXTEON_NOGLOW") == nullptr)
    {
        const double sxr = width() / 480.0;
        constexpr double gscale = 0.5;
        constexpr double kBleed = 6.0;          // px of bleed, top and bottom
        const int iw = std::max (1, int (std::lround (width() * gscale)));
        const int ih = std::max (1, int (std::lround ((64.0 * sxr + 2 * kBleed) * gscale)));

        // Quantised to 1/512 of the range.  The pod is ~550 px wide, so 1/512
        // is well under one pixel, and the finer 1/8192 key used before made
        // the glow layer rebuild on almost every meter tick: rebuilding it
        // cost 1.1 ms of the 2.5 ms the pod painted, which was the difference
        // between ~93 fps and ~113 fps on a 120 Hz panel.
        const auto qk = [] (double v) { return quint64 (std::clamp (v, 0.0, 1.0) * 511.0); };
        const quint64 key = qk (m_outL.peak)
                          | (qk (m_outR.peak) << 9)
                          | (qk (m_inL.peak)  << 18)
                          | (qk (m_inR.peak)  << 27)
                          | (qk (m_harmFrac)  << 36)
                          | (quint64 (iw * 31 + ih) << 48);

        if (m_glowKey != key || m_glowBlurred.size() != QSize (iw, ih))
        {
            m_glowKey = key;
            if (m_glowLayer.width() != iw || m_glowLayer.height() != ih)
                m_glowLayer = QImage (iw, ih, QImage::Format_ARGB32_Premultiplied);
            m_glowLayer.fill (Qt::transparent);

            QPainter gp (&m_glowLayer);
            gp.setRenderHint (QPainter::Antialiasing, true);
            // same transform as the sharp pass, plus the bleed, scaled down
            gp.translate (0.0, kBleed * gscale);
            gp.scale (sx * gscale, sx * gscale);
            drawPod (&gp, true);

            // 1.7 * sx is the design's stdDeviation in item pixels; the buffer is
            // at gscale, and the 14-tap kernel needs a little more spread to
            // reach the same tail as a 3-sigma Gaussian.
            m_glowBlurred = blurImage (m_glowLayer, 2.1 * sx * gscale);
        }

        // Bilinear upscale: a nearest 2x of a half-res halo shows the source
        // pixels as blocks.  The blur is cheap enough that the extra filtering
        // does not matter.
        g->save();
        g->setOpacity (0.85);
        g->setRenderHint (QPainter::SmoothPixmapTransform, true);
        g->drawImage (QRectF (0.0, (height() - 64.0 * sx) * 0.5 - kBleed,
                              iw / gscale, ih / gscale),
                      m_glowBlurred);
        g->restore();
    }

    g->translate (0.0, (height() - 64.0 * sx) * 0.5);
    g->scale (sx, sx);
    g->setRenderHint (QPainter::Antialiasing, true);

    drawPod (g, false);
}

// ---------------------------------------------------------------------------
//  Glow blur.
//
//  The design's filter is  feGaussianBlur stdDeviation=1.7  merged with the
//  source.  A truncated separable Gaussian costs 2·(6σ+1) taps per pixel; at
//  σ≈3 for a 550 px wide pod that is ~40 taps, which dropped the pod from 58
//  to 43 fps.
//
//  So the glow uses the same dual-Gaussian construction as the glass
//  (X-PostProcessing-Library's DualGaussianBlur shape: two 1-D passes over a
//  7-tap kernel with quadratic spacing, .40/.15/.10/.05 at 0, ±1, ±2, ±6).
//  It is 14 taps per pixel instead of 40, and for a *halo* — which is only
//  ever seen as a soft bloom around a 1.8 px stroke — the difference is not
//  visible.
// ---------------------------------------------------------------------------
QImage OvMeterView::blurImage (const QImage& src, double sigma)
{
    if (src.isNull() || sigma <= 0.0) return src;

    QImage work = src.convertToFormat (QImage::Format_ARGB32_Premultiplied);
    const int w = work.width(), h = work.height();
    if (w < 2 || h < 2) return work;

    // The 7-tap kernel's variance is 2·(0.15·1² + 0.10·2² + 0.05·6²)·o²
    // = 4.70·o², so the tap offset that yields this sigma is sigma/2.168.
    const double offset = std::max (0.35, sigma / 2.1684);
    const int step[3] = { std::max (1, int (std::lround (offset))),
                          std::max (1, int (std::lround (2.0 * offset))),
                          std::max (1, int (std::lround (6.0 * offset))) };
    constexpr double tw[3] = { 0.15, 0.10, 0.05 };
    constexpr double centre = 0.40;

    std::vector<double> tmp (size_t (w) * size_t (h) * 4);

    // horizontal
    for (int y = 0; y < h; ++y)
    {
        const auto* line = reinterpret_cast<const QRgb*> (work.constScanLine (y));
        double* out = tmp.data() + size_t (y) * size_t (w) * 4;
        for (int x = 0; x < w; ++x)
        {
            const QRgb c = line[x];
            double r = qRed (c) * centre, g = qGreen (c) * centre;
            double b = qBlue (c) * centre, a = qAlpha (c) * centre;
            for (int k = 0; k < 3; ++k)
            {
                const QRgb pl = line[std::clamp (x - step[k], 0, w - 1)];
                const QRgb pr = line[std::clamp (x + step[k], 0, w - 1)];
                r += (qRed (pl)   + qRed (pr))   * tw[k];
                g += (qGreen (pl) + qGreen (pr)) * tw[k];
                b += (qBlue (pl)  + qBlue (pr))  * tw[k];
                a += (qAlpha (pl) + qAlpha (pr)) * tw[k];
            }
            const size_t o = size_t (x) * 4;
            out[o] = r; out[o + 1] = g; out[o + 2] = b; out[o + 3] = a;
        }
    }

    // vertical
    for (int y = 0; y < h; ++y)
    {
        auto* line = reinterpret_cast<QRgb*> (work.scanLine (y));
        for (int x = 0; x < w; ++x)
        {
            const double* c = tmp.data() + (size_t (y) * size_t (w) + size_t (x)) * 4;
            double r = c[0] * centre, g = c[1] * centre;
            double b = c[2] * centre, a = c[3] * centre;
            for (int k = 0; k < 3; ++k)
            {
                const double* pu = tmp.data()
                    + (size_t (std::clamp (y - step[k], 0, h - 1)) * size_t (w) + size_t (x)) * 4;
                const double* pd = tmp.data()
                    + (size_t (std::clamp (y + step[k], 0, h - 1)) * size_t (w) + size_t (x)) * 4;
                r += (pu[0] + pd[0]) * tw[k];
                g += (pu[1] + pd[1]) * tw[k];
                b += (pu[2] + pd[2]) * tw[k];
                a += (pu[3] + pd[3]) * tw[k];
            }
            line[x] = qRgba (int (std::clamp (r, 0.0, 255.0)),
                             int (std::clamp (g, 0.0, 255.0)),
                             int (std::clamp (b, 0.0, 255.0)),
                             int (std::clamp (a, 0.0, 255.0)));
        }
    }
    return work;
}
