#include "OvEnv.h"
#include "OvMeterView.h"
#include "OvBallistics.h"
#include <QQuickWindow>
#include "OvFrost.h"
#include "OvEngine.h"
#include "OvQmlTypes.h"
#include "OvTick.h"

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

    // ---- the pod's numbers, in real units --------------------------------
    // Every one of these used to be a per-frame coefficient somewhere in this
    // file; expressed per second they mean the same thing at jalv's 15 Hz idle
    // rate and at the preview's 60-120 Hz.
    constexpr double kBarSpanDb   = 60.0;                // bottom of the bar
    constexpr double kBarFloorDb  = -60.0;

    // IEC 60268-10 PPM Type II (EBU) is 20 dB in 1.7 s.  That was the rate here
    // until the whole display was asked to run 25 % faster, so it is 20 dB in
    // 1.7/1.25 = 1.36 s -> 14.7 dB/s.  (Type I / DIN, for reference, is 24 dB
    // in 2.8 s.)
    //
    // Worth knowing: the measurement behind it is true RMS (ovb::Rms, tau
    // 300 ms) whose own natural decay is 4.343/0.3 = 14.5 dB/s, and the display
    // can never fall faster than the number it is chasing.  At 14.7 against
    // 14.5 the two are now almost exactly equal, so the bar's measured fall is
    // set by the DETECTOR: 14.5 dB/s, which is 24 % faster than the 11.7 dB/s
    // this had before the change.  Making the bar faster than this would mean
    // shortening the RMS window, i.e. changing the measurement rather than the
    // display.
    constexpr double kBarDbPerSec = 20.0 / 1.36;        // 20 dB in 1.36 s = 14.7 dB/s

    constexpr double kPeakDbPerSec = 0.18 * 60.0 * 1.25; // 10.8 dB/s (the page's) x 1.25

    // The ring follows a harmonic RATIO that the DSP smooths with a 131 ms
    // one-pole (kHarmTau in FxProcessor.cpp), so 4.343/0.131 = 33 dB/s is that
    // measurement's own decay — the display adds no shape of its own.
    constexpr double kHarmFloorDb = -48.0;
    constexpr double kHarmDbPerSec = 4.343 / 0.105;      // 131 ms tau x 1.25

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

    // ---- latching vs. displaying: two clocks, on purpose ------------------
    // A professional meter has two separate rates and I had collapsed them into
    // one, twice.
    //
    //   LATCH   the measurement arrives at whatever rate the host polls the
    //           analyser: 11.8 Hz measured in jalv, 60-125 Hz in the preview.
    //           That value is stored as the TARGET and nothing else.
    //
    //   DISPLAY runs at a fixed rate of its own and moves what is on screen
    //           towards the target with ballistics.  This is where the smooth
    //           motion comes from, and it is why a hardware meter moves at all
    //           between two samples that are 85 ms apart.
    //
    // Painting only when data arrives (what I did last) cannot show motion
    // between measurements, and moving towards the target with a 45 ms release
    // against an 85 ms measurement interval snapped straight to the new value
    // every time: min(1, 0.085/0.045) == 1. So the bars jumped to exact numbers,
    // which is exactly what was reported.
    //
    // The ballistics below are the standard ones: an instant attack (a
    // transient must show in the frame it happens) and a release of ~300 ms for
    // 20 dB, which is the VU/PPM convention; the peak hold and its 10.8 dB/s
    // fall are the page's own.
    m_timer.setInterval (1000 / OvTiming::kMeterHz);
    connect (&m_timer, &QTimer::timeout, this, [this] {
        ovtick::markKind ("meter-tick", ovtick::TickKind::MetersChanged);
        const auto now = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double> (now - m_last).count();
        m_last = now;
        dt = std::min (dt, 0.1);

        const quint64 before = stateKey();
        advance (dt);
        if (stateKey() != before)
            update();

        // OVERTEON_METERDBG=1: the ballistic state, one line per display frame.
        //
        // "The meters move like this / not like that" is not something a
        // screenshot can settle — a still frame looks the same whether the value
        // arrived by a curve or by a jump.  This prints the numbers the paint
        // uses, so the curve can be plotted (or diffed) from outside.
        if (ovEnvFlag ("OVERTEON_METERDBG"))
        {
            std::fprintf (stderr, "[m] %.3f  in %6.2f %6.2f  out %6.2f %6.2f  "
                                  "peak in %6.2f out %6.2f  harm %.4f\n",
                          m_clock,
                          ovb::normToDb (m_inL.level,  60.0, -60.0),
                          ovb::normToDb (m_inR.level,  60.0, -60.0),
                          ovb::normToDb (m_outL.level, 60.0, -60.0),
                          ovb::normToDb (m_outR.level, 60.0, -60.0),
                          ovb::normToDb (m_inL.peak,   60.0, -60.0),
                          ovb::normToDb (m_outL.peak,  60.0, -60.0),
                          m_harmFrac);
        }
    });
    m_last = std::chrono::steady_clock::now();
    m_timer.start();
}

void OvMeterView::setEngine (OvEngine* e)
{
    if (e == m_engine) return;

    if (m_engine != nullptr)
        disconnect (m_engine, nullptr, this, nullptr);

    m_engine = e;

    // LATCH the measurement.  This handler used to emit advanceRequested() and
    // let advance() read the source again — but advance() now reads m_target, so
    // without this assignment the meter would have shown the default silence
    // forever.  The measurement is stored; the timer below moves the bars
    // towards it.
    if (m_engine != nullptr)
        connect (m_engine, &OvEngine::metersChanged, this,
                 [this] { m_target = m_engine->source().meters(); },
                 Qt::QueuedConnection);

    emit engineChanged();
    update();
}

quint64 OvMeterView::stateKey() const
{
    // Quantised: the pod is ~200 px wide, so 1/8192 of the range is well below
    // one pixel.
    //
    // Nine values at 13 bits each is 117 bits, and this used to be a shift-and-
    // or chain into a quint64: everything above the sixth value was shifted by
    // 65, 78, 91 and 104 bits, which is UNDEFINED in C++ and warned about on
    // every build.  On x86 the count wraps, so the last four values were
    // aliasing onto the first four — the peak columns collided with the level
    // columns and a repaint could be skipped while the trail was visibly
    // moving.  Mixed instead, so all nine contribute.
    const auto q = [] (double v) { return quint64 (std::clamp (v, 0.0, 1.0) * 8191.0); };
    quint64 key = 1469598103934665603ull;                       // FNV-1a
    const auto mix = [&key] (quint64 v)
    {
        key = (key ^ v) * 1099511628211ull;
    };
    for (const auto* ch : { &m_outL, &m_outR, &m_inL, &m_inR })
    {
        mix (q (ch->level));
        mix (q (ch->peak));
    }
    mix (q (m_harmFrac));
    return key;
}

void OvMeterView::advance (double dt)
{
    m_clock += dt;
    if (m_engine == nullptr) return;

    // The LATCHED measurement, not a fresh one: reading the source here meant
    // the "previous" value was also the newest, so the filter had nothing to
    // filter against.
    const auto m = m_target;

    struct T { Channel* ch; double db; };
    const T list[] { { &m_outL, m.outL }, { &m_outR, m.outR },
                     { &m_inL, m.inL },   { &m_inR, m.inR } };

    // drawLevel() in web_fx/index.html, step for step:
    //   new max        -> hold for 1.1 s
    //   after the hold -> fall back at about 10.8 dB/s, never below the level
    //
    // The motion itself comes from ovb::Fall (ui/qt/OvBallistics.h), which is
    // the SAME code the light, the harmonic ring, the analyser and the glow
    // bands use.  The bars used to carry their own copy of
    //       step = 1 - (target/start)^(1/n_frames)
    // here: an exponential, i.e. maximum speed on the first frame of a fall and
    // an infinite tail, which is why a bar snapped to its value and then crept.
    // Constant dB/s with a smootherstep ease-in lands exactly and starts from
    // rest; the numbers are the same ones (20 dB in 0.35 s), only the curve is.
    for (const auto& t : list)
    {
        const double targetDb = ovb::normToDb (levelToFraction (t.db), kBarSpanDb, kBarFloorDb);
        t.ch->levelFall.advance (targetDb, dt, kBarDbPerSec);
        t.ch->level = ovb::dbToNorm (t.ch->levelFall.shown, kBarSpanDb, kBarFloorDb);

        if (t.ch->level >= t.ch->peak)
        {
            t.ch->peak = t.ch->level;
            t.ch->holdUntil = m_clock + 1.1;
            // Start the trail's fall from rest again: it is a new peak.
            t.ch->peakFall.reset (ovb::normToDb (t.ch->peak, kBarSpanDb, kBarFloorDb));
        }
        else if (m_clock > t.ch->holdUntil)
        {
            // 0.18 per second of the 0..1 scale is 10.8 dB/s of a 60 dB one.
            t.ch->peakFall.advance (ovb::normToDb (t.ch->level, kBarSpanDb, kBarFloorDb),
                                    dt, kPeakDbPerSec);
            t.ch->peak = ovb::dbToNorm (t.ch->peakFall.shown, kBarSpanDb, kBarFloorDb);
        }
    }

    // The harmonic ring is a METER too: it must move like one.  Mapping the
    // raw value snapped it between readings — in the preview the source only
    // changes `harm` when a new block is measured, so the ring sat still and
    // then jumped.
    //
    // It also used to release in LINEAR AMPLITUDE (v -= v*step) while every
    // other meter released in dB, so the ring's tail had a different shape from
    // the bars beside it.  It is a dB quantity to begin with; it now goes
    // through the same ovb::Fall, in dB, at 48 dB in 0.45 s.
    {
        const double harmDb = std::clamp (double (m.harm), kHarmFloorDb, 0.0);
        m_harmFall.advance (harmDb, dt, kHarmDbPerSec, 0.045);
        m_harmFrac = ovb::dbToNorm (m_harmFall.shown, -kHarmFloorDb, kHarmFloorDb);
    }
    double f = m_harmFrac;
    // test hook: OVERTEON_HARM=0..1 pins the harmonic fraction so the ring's
    // colour ramp can be inspected at known values.
    if (const char* h = std::getenv ("OVERTEON_HARM"))
        f = std::clamp (std::atof (h), 0.0, 1.0);
    m_harmFrac = f;
}

// The design wraps the bars and the harmonic ring in
//     <filter id="neon"><feGaussianBlur stdDeviation="1.7"/><feMerge>
//         <feMergeNode in="b"/><feMergeNode in="SourceGraphic"/></feMerge></filter>
// i.e. a blurred copy drawn UNDER the sharp graphic.  QPainter has no filter
// pipeline, so the same result is produced by rendering the pod twice: once
// blur-glow-only and once sharp, with the glow underneath.
void OvMeterView::drawPod (QPainter* g, PodPass pass)
{
    const double frac = m_harmFrac;
    const QColor hc = harmColor (frac);
    const QRectF ring (240 - 28, 32 - 28, 56, 56);

    // ---- the ring:  <circle id="hArc" stroke-width="4" filter="url(#harmGlow)"/>
    // Drawn alone in GlowRing so its halo is built from the ring only.  The old
    // code faked this glow with 6 px and 9 px semi-transparent strokes, which is
    // why the halo had no falloff.
    if (pass != GlowBars)
    {
        g->setBrush (Qt::NoBrush);

        if (pass == Sharp)
        {
            QColor guide (QStringLiteral ("#3b4350"));
            guide.setAlphaF (0.07f);
            g->setPen (QPen (guide, 3.0));
            g->drawEllipse (ring);
        }

        g->setPen (QPen (hc, 4.0, Qt::SolidLine, Qt::RoundCap));
        g->drawArc (ring, 90 * 16, -int (frac * 360.0 * 16.0));
    }

    // ---- bars + cap dots:  <g filter="url(#neon)">
    if (pass != GlowRing)
    {
        // static hook guides — unfiltered in the design
        if (pass == Sharp)
        {
            g->setPen (QPen (kTrack, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            g->setBrush (Qt::NoBrush);
            for (const auto* p : { &m_outL.path, &m_outR.path, &m_inL.path, &m_inR.path })
                g->drawPath (*p);
        }

        struct Bar { const Channel* ch; QColor bright, trail; };
        const Bar bars[] { { &m_outL, kOutBright, kOutTrail }, { &m_outR, kOutBright, kOutTrail },
                           { &m_inL,  kInBright,  kInTrail  }, { &m_inR,  kInBright,  kInTrail  } };

        for (const auto& b : bars)
        {
            g->setBrush (Qt::NoBrush);

            // The trails are NOT filtered in the design (their <g> has no
            // filter), so they belong to the sharp pass only.  Including them
            // in the halo would widen it in the wrong colour.
            if (pass == Sharp && b.ch->peak > 0.002)
            {
                QColor trail = b.trail;
                trail.setAlphaF (0.72f);          // the design's opacity=".72"
                g->setPen (QPen (trail, 2.6, Qt::SolidLine, Qt::FlatCap, Qt::RoundJoin));
                g->drawPath (partial (b.ch->poly, b.ch->peak));
            }

            if (b.ch->level > 0.002)
            {
                g->setPen (QPen (b.bright, 1.8, Qt::SolidLine, Qt::FlatCap, Qt::RoundJoin));
                g->drawPath (partial (b.ch->poly, b.ch->level));
            }

            // cap dot at the trail's tip (design: #cOL/#cOR/#cIL/#cIR, r=1.7),
            // inside the filtered group so it glows too
            if (b.ch->peak > 0.002)
            {
                const int ti = std::clamp (int (b.ch->peak * double (b.ch->poly.size() - 1)),
                                           0, int (b.ch->poly.size()) - 1);
                QColor dot = b.bright;
                dot.setAlphaF (0.95f);
                g->setPen (Qt::NoPen);
                g->setBrush (dot);
                g->drawEllipse (b.ch->poly[(size_t) ti], 1.7, 1.7);
            }
        }
    }

    // ---- text -----------------------------------------------------------
    //  The SVG draws every text element unfiltered, so it exists only in the
    //  sharp pass.
    if (pass != Sharp) return;
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
    ovtick::markKind ("paint-meter", ovtick::TickKind::PaintMeter);
    if (ovEnvFlag ("OVERTEON_FPS"))
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

    // ---- the two halos ---------------------------------------------------
    //  <filter id="neon">     feGaussianBlur stdDeviation="1.7"  on the bars
    //  <filter id="harmGlow"> feGaussianBlur stdDeviation="2.5"  on the ring
    //  each merged as blurred-under-sharp.  OvFrost::dualGaussian does the
    //  blurring, so this tree has one blur implementation everywhere.
    //
    //  stdDeviation is in viewBox units, so it scales by sx like the art.  The
    //  bleed must exceed 3 sigma of the widest (2.5 * 1.146 * 3 = 8.6 px) and
    //  the item must be taller than the art, or a painted item clips the halo
    //  at its own bounds — the SVG uses overflow:visible for the same reason.
    if (! ovEnvFlag ("OVERTEON_NOGLOW"))
    {
        // Half device resolution for the halo.  A halo is only ever seen
        // through a 3 px Gaussian, so the extra pixels cannot be resolved;
        // at full logical resolution the blur of a 550x157 buffer with a
        // radius-12 kernel cost 9.4 ms a paint — three times the whole EQ plot.
        // Half of DEVICE keeps it crisp when the compositor upscales on HiDPI.
        const double dprV = window() != nullptr ? window()->devicePixelRatio() : 1.0;
        const double gscale = dprV * 0.5;
        constexpr double kBleed = 24.0;
        const int iw = std::max (1, int (std::lround (width() * gscale)));
        const int ih = std::max (1, int (std::lround ((64.0 * sx + 2 * kBleed) * gscale)));

        const auto qk = [] (double v) { return quint64 (std::clamp (v, 0.0, 1.0) * 127.0); };
        const quint64 key = qk (m_outL.peak)
                          | (qk (m_outR.peak) << 9)
                          | (qk (m_inL.peak)  << 18)
                          | (qk (m_inR.peak)  << 27)
                          | (qk (m_harmFrac)  << 36)
                          | (quint64 (iw * 31 + ih) << 48);

        auto build = [&] (QImage& layer, QImage& blurred, PodPass pass, double sigmaCss)
        {
            if (layer.size() != QSize (iw, ih))
                layer = QImage (iw, ih, QImage::Format_ARGB32_Premultiplied);
            layer.fill (Qt::transparent);

            QPainter gp (&layer);
            gp.setRenderHint (QPainter::Antialiasing, true);
            gp.translate (0.0, kBleed * gscale);
            gp.scale (sx * gscale, sx * gscale);
            drawPod (&gp, pass);
            gp.end();

            const double sigma = sigmaCss * sx * gscale;
            blurred = OvFrost::dualGaussian (layer, sigma);
        };

        // Rebuilding both halos is the most expensive thing in the pod, and the
        // level moves every poll.  The design transitions --light over 0.1 s, so
        // a halo refreshed at 30 Hz is indistinguishable from one refreshed at
        // 120 — and it costs a quarter as much.
        const auto nowT = std::chrono::steady_clock::now();
        const bool due = (nowT - m_glowBuiltAt)
                             > std::chrono::milliseconds (33);
        if ((m_glowKey != key && due) || m_barsBlur.size() != QSize (iw, ih))
        {
            m_glowBuiltAt = nowT;
            m_glowKey = key;
            build (m_barsLayer, m_barsBlur, GlowBars, 1.7);

            // The ring's filter is REPLACED in the hot zone.  The SVG carries
            //     filter="url(#harmGlow)"                       -> stdDeviation 2.5
            // but the script sets, above 97 %,
            //     hArc.style.filter = "drop-shadow(0 0 4px #ff2a44)"
            // and an inline style beats a presentation attribute, so harmGlow
            // is not applied there at all.
            //
            // Measured, rather than assuming the spec's radius/2: a 4 px rect
            // under drop-shadow(0 0 4px) has the same tail as the same rect
            // under feGaussianBlur stdDeviation="4" —
            //     drop-shadow  0 2 9 23 44 69 [255 255]  79 57 33 15 4 0
            //     sd=4 blur    0 2 9 23 44 69 [ 86  89]  79 57 33 15 4 0
            // so Chromium maps the blur radius to sigma 1:1.  The Schranz / N/A
            // branch uses 6 px, i.e. sigma 6.
            const double ringSigma = (m_harmFrac >= 0.97) ? 4.0 : 2.5;
            build (m_ringLayer, m_ringBlur, GlowRing, ringSigma);
        }

        g->save();
        g->setRenderHint (QPainter::SmoothPixmapTransform, true);
        const QRectF halo (0.0, (height() - 64.0 * sx) * 0.5 - kBleed,
                           iw / gscale, ih / gscale);
        g->drawImage (halo, m_barsBlur);
        g->drawImage (halo, m_ringBlur);
        g->restore();
    }

    g->translate (0.0, (height() - 64.0 * sx) * 0.5);
    g->scale (sx, sx);
    g->setRenderHint (QPainter::Antialiasing, true);

    drawPod (g, Sharp);
}

