#include "OvEnv.h"
#include "OvMeterView.h"
#include "OvBallistics.h"
#include <QQuickWindow>
#include "OvFrost.h"
#include "OvEngine.h"
#include "OvQmlTypes.h"
#include "OvTick.h"

#include <QPainter>
#include <QConicalGradient>
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
    /// Peak-hold time before the trail starts falling.  Pro-Q 4's analyser
    /// holds noticeably longer than the old 1.1 s, and 2.0 s reads as "the
    /// peak stays put while I look at it" without turning into a memory.
    constexpr double kPeakHoldSeconds = 2.0;
    constexpr double kPeakNormPerSec = 0.18;

    // The ring shows a harmonic RATIO; this is where its display bottoms out.
    // RMS mode uses the DSP's 131 ms ratio, TRANSIENT the block's own — in
    // neither case does the display add a ballistics curve of its own.
    constexpr double kHarmFloorDb = -48.0;

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

    // Measurements are latched as they arrive; this display timer advances the
    // independent peak-hold release and any explicitly selected bar smoothing.
    // With smoothing OFF the bright bars use each measurement verbatim.
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

void OvMeterView::setSettingsHover (bool on)
{
    if (on == m_settingsHover) return;
    m_settingsHover = on;
    emit settingsHoverChanged();
    update();          // one repaint for the hover, not a running timer
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
    {
        connect (m_engine, &OvEngine::metersChanged, this,
                 [this] { m_target = m_engine->source().meters(); },
                 Qt::QueuedConnection);

        // The RANGE lives in Settings, not in the meter data.  Without this the
        // scale labels only changed on the next meter tick that happened to
        // move something — silent editors looked frozen until audio played.
        // Recompute both the labels' source and the arc length, then repaint.
        connect (m_engine, &OvEngine::settingsChanged, this, [this] {
            if (m_engine == nullptr) return;
            m_grRange = m_engine->grRange();
            const double grDb = (m_engine->harmMode() == 1)
                              ? double (m_target.grPeak) : double (m_target.gr);
            m_limitFrac = std::clamp (grDb / double (std::max (1, m_grRange)), 0.0, 1.0);
            update();
        });
    }

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
    // The limiter arc and its scale are NOT meter values, but they are part of
    // the picture: when the RANGE changes in Settings the labels must repaint
    // even if no audio is running (the state key is what gates update()).
    mix (q (m_limitFrac));
    mix (quint64 (m_grRange));
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

    struct T { Channel* ch; double pkDb; };
    const T list[] { { &m_outL, m.outPkL }, { &m_outR, m.outPkR },
                     { &m_inL, m.inPkL },   { &m_inR, m.inPkR } };

    // A peak is held for kPeakHoldSeconds and then falls linearly at 0.18/s.
    // The BRIGHT bar is driven by the block's SAMPLE PEAK rather than the
    // 300 ms RMS: RMS cannot drop the moment the audio stops, and
    // "instant" is exactly the mode where that matters.  Its own fall is a
    // display choice — INSTANT follows the measurement; VU uses the meter
    // standard's 300 ms response (99 % in 300 ms, i.e. tau 65 ms).
    const int releaseMode = (m_engine != nullptr) ? m_engine->meterRelease() : 0;
    const double vuAlpha = 1.0 - std::exp (-dt / 0.065);

    for (const auto& t : list)
    {
        const double instant = levelToFraction (t.pkDb);

        if (releaseMode == 0)
            t.ch->level = instant;
        else
            t.ch->level += (instant - t.ch->level) * vuAlpha;

        if (instant >= t.ch->peak)
        {
            t.ch->peak = instant;
            t.ch->holdUntil = m_clock + kPeakHoldSeconds;
        }
        else if (m_clock > t.ch->holdUntil)
        {
            t.ch->peak = std::max (instant, t.ch->peak - dt * kPeakNormPerSec);
        }
    }

    // The harmonic ring has no display ballistics any more: MODE picks the
    // MEASUREMENT and that measurement is what is drawn.
    //
    //   RMS        the DSP's ratio, already smoothed over 131 ms
    //   TRANSIENT  this block's own ratio (harmPeak), no time smoothing
    //
    // The old SMOOTH option (an ovb::Fall ramp on top) is gone with its
    // property, so there is no second place where the ring's motion is shaped.
    {
        m_harmMode = (m_engine != nullptr) ? m_engine->harmMode() : 1;
        const double raw = (m_harmMode == 1) ? double (m.harmPeak) : double (m.harm);
        const double harmDb = std::clamp (raw, kHarmFloorDb, 0.0);
        m_harmFrac = ovb::dbToNorm (harmDb, -kHarmFloorDb, kHarmFloorDb);
    }

    // ---- limiter ring ----------------------------------------------------
    //
    // The same MODE as the harmonic ring: TRANSIENT reads this block's deepest
    // gain reduction, RMS the 131 ms one-pole.  Full circle = the RANGE chosen
    // in Settings (6/12/24/48/96 dB), so the scale labels at 9 and 3 o'clock
    // are range/4 and 3*range/4.
    {
        m_grRange = (m_engine != nullptr) ? m_engine->grRange() : 12;
        const double grDb = (m_harmMode == 1) ? double (m.grPeak) : double (m.gr);
        m_limitFrac = std::clamp (grDb / double (std::max (1, m_grRange)), 0.0, 1.0);
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
    // Hover turns the ring green regardless of level: the colour IS the
    // affordance, and a ring that stayed red at high harmonic content while
    // advertising "settings" would read as an alarm rather than an invitation.
    const QColor hc = m_settingsHover ? QColor (QStringLiteral ("#33d17a"))
                                      : harmColor (frac);
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

        // ---- the ring: ONE continuous stroke ------------------------------
        //
        // The harmonic half and the limiter half are a SINGLE path: from the
        // limiter's tip, through 12 o'clock, to the harmonic tip.  Drawing them
        // as two strokes left two antialiased start caps meeting at 12 (plus a
        // separate joint disc), and those edges were visible as a seam even
        // when both halves had the same colour.  One path has one edge.
        //
        // Colour comes from a conical gradient: red on the counter-clockwise
        // side of 12, harmonic colour on the clockwise side, blended across
        // exactly one stroke diameter (4 px of arc length).  The limiter has a
        // second, hard colour boundary just past its own tip — outside the
        // path, so the tip's round cap stays red.
        const double harmLen  = std::clamp (frac, 0.0, 1.0);
        const double limitLen = std::clamp (m_limitFrac, 0.0, 1.0);
        const QColor limitColour = m_settingsHover
                                 ? QColor (QStringLiteral ("#33d17a"))
                                 : QColor (QStringLiteral ("#ff2a44"));

        if (limitLen <= 0.0005)
        {
            // No limiter: EXACTLY the pre-limiter drawing — a plain harmonic
            // arc, one stroke, no gradient anywhere.
            if (harmLen > 0.0005)
            {
                g->setPen (QPen (hc, 4.0, Qt::SolidLine, Qt::RoundCap));
                g->drawArc (ring, 90 * 16, -int (harmLen * 360.0 * 16.0));
            }
        }
        else if (harmLen <= 0.0005)
        {
            // Limiter only: one red arc; no harmonic colour to blend from.
            g->setPen (QPen (limitColour, 4.0, Qt::SolidLine, Qt::RoundCap));
            g->drawArc (ring, 90 * 16, int (limitLen * 360.0 * 16.0));
        }
        else
        {
            const double total    = harmLen + limitLen;
            const double startDeg = 90.0 + limitLen * 360.0;

            // One stroke diameter of arc length, converted to degrees on the
            // ring's centreline: 2 px each side of 12.
            constexpr double kPi = 3.14159265358979323846;
            const double halfDeg = (2.0 / (ring.width() * 0.5)) * (180.0 / kPi);
            const double t0 = (90.0 - halfDeg) / 360.0;
            const double t1 = (90.0 + halfDeg) / 360.0;
            // Hard switch back to the harmonic colour just past the limiter's
            // tip; the tip's cap is inside the red zone, and this point is not
            // on the path, so the switch itself is never seen.  One extra
            // degree clears the cap's inner edge (which spans a slightly wider
            // angle than the 28 px centreline used for the 12 o'clock blend).
            const double tip = std::fmod (90.0 + limitLen * 360.0 + halfDeg + 1.0,
                                          360.0) / 360.0;

            QConicalGradient grad (ring.center(), 0.0);
            if (tip >= t0 && tip <= t1)
            {
                // The limiter ends inside the 12 o'clock blend (it spans more
                // than 97 % of the ring).  There is no separate plateau left
                // to switch back from, so the simple two-stop form is exact
                // where the path is visible.
                grad.setColorAt (0.0, hc);
                grad.setColorAt (t0, hc);
                grad.setColorAt (t1, limitColour);
                grad.setColorAt (1.0, limitColour);
            }
            else if (tip > t1)
            {
                // Hard cut just past the limiter's cap.  Without the extra
                // stop QGradient interpolates red -> harmonic across the whole
                // limiter arc, which made the red fade into the harmonic
                // colour along its length.
                const double cut = std::max (0.0, tip - 0.0005);
                grad.setColorAt (0.0, hc);
                grad.setColorAt (t0, hc);
                grad.setColorAt (t1, limitColour);
                grad.setColorAt (cut, limitColour);
                grad.setColorAt (tip, hc);
                grad.setColorAt (1.0, hc);
            }
            else
            {
                // The limiter span wraps through 0 degrees: red from 0 to its
                // tip, then harmonic colour, then the 12 o'clock blend, then
                // red again.
                const double cut = std::max (0.0, tip - 0.0005);
                grad.setColorAt (0.0, limitColour);
                grad.setColorAt (cut, limitColour);
                grad.setColorAt (tip, hc);
                grad.setColorAt (t0, hc);
                grad.setColorAt (t1, limitColour);
                grad.setColorAt (1.0, limitColour);
            }

            QPainterPath p;
            if (total >= 0.9995)
            {
                // The halves overlap: their union is the whole ring.  The
                // white overlay below marks the shared part.
                p.addEllipse (ring);
            }
            else
            {
                p.arcMoveTo (ring, startDeg);
                p.arcTo (ring, startDeg, -total * 360.0);
            }

            g->setBrush (Qt::NoBrush);
            g->setPen (QPen (QBrush (grad), 4.0, Qt::SolidLine, Qt::RoundCap));
            g->drawPath (p);
        }

        // Where the two arcs occupy the SAME angle they cross; that part is
        // white.  They start together at 12 and travel in opposite directions,
        // so this only happens once their lengths add up to more than a full
        // turn.  Qt angles: harmonic covers [90 - H*360, 90] clockwise, the
        // limiter [90, 90 + L*360] counter-clockwise, so the shared run starts
        // at 450 - H*360 and is (H + L - 1)*360 long.
        const double overlap = frac + m_limitFrac - 1.0;
        if (overlap > 0.0005)
        {
            g->setPen (QPen (QColor (255, 255, 255), 4.0, Qt::SolidLine, Qt::RoundCap));
            g->drawArc (ring,
                        int ((450.0 - frac * 360.0) * 16.0),
                        int (overlap * 360.0 * 16.0));
        }
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
    if (m_settingsHover)
    {
        // Hover: the read-out becomes the entry's label.  Smaller than the
        // percentage because it is a word, not a measurement — at 15 px bold it
        // would not fit the 80 px slot the number lives in.
        QFont sf = f;
        sf.setPixelSize (9);
        sf.setBold (true);
        sf.setLetterSpacing (QFont::AbsoluteSpacing, 1.2);
        g->setFont (sf);
        g->setPen (QColor (QStringLiteral ("#33d17a")));
        g->drawText (QRectF (196, 20, 88, 20), Qt::AlignCenter, QStringLiteral ("SETTINGS"));
        g->setFont (f);
    }
    else if (schranz)
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

    // ---- limiter ring scale ----------------------------------------------
    //
    // The red arc travels counter-clockwise, so 9 o'clock is a quarter of the
    // range and 3 o'clock is three quarters (12 dB -> 3 on the left, 9 on the
    // right).  The labels sit in the gap between the ring and the upper/lower
    // meter hooks, in the same small grey type as HARM.  Always visible: the
    // scale belongs to the ring, it is not a tooltip.
    f.setPixelSize (7);
    f.setBold (false);
    f.setLetterSpacing (QFont::AbsoluteSpacing, 0);
    g->setFont (f);
    g->setPen (kLabel);
    const auto scaleText = [] (double v)
    {
        return (std::abs (v - std::round (v)) < 1.0e-6)
             ? QString::number (qRound (v))
             : QString::number (v, 'f', 1);
    };
    g->drawText (QRectF (186, 26, 22, 12), Qt::AlignRight | Qt::AlignVCenter,
                 scaleText (m_grRange / 4.0));
    g->drawText (QRectF (272, 26, 22, 12), Qt::AlignLeft | Qt::AlignVCenter,
                 scaleText (m_grRange * 0.75));

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
        quint64 key = 1469598103934665603ull;
        const auto mix = [&key] (quint64 v) { key = (key ^ v) * 1099511628211ull; };
        // What APPEARS or DISAPPEARS, hashed separately so those changes can
        // bypass the 30 Hz throttle (see m_glowStructKey).
        quint64 sk = 1469598103934665603ull;
        const auto mixAppearance = [&] (quint64 v) { mix (v); sk = (sk ^ v) * 1099511628211ull; };
        for (const auto* ch : { &m_outL, &m_outR, &m_inL, &m_inR })
        {
            mix (qk (ch->level));
            mix (qk (ch->peak));
            // qk() is 7-bit: anything below 1/127 quantises to zero, so the
            // halo key stopped changing while the bar / cap dot was still
            // drawn (the draw threshold is 0.002).  The cached halo then kept
            // the blurred dot at the bottom of the meter until some unrelated
            // repaint — a settings hover — rebuilt it.  These presence bits
            // change exactly when a bar or dot crosses the draw threshold.
            mixAppearance (quint64 (ch->level > 0.002 ? 1 : 0));
            mixAppearance (quint64 (ch->peak  > 0.002 ? 1 : 0));
        }
        mix (qk (m_harmFrac));
        // The limiter arc lives in the ring's HALO too.  Without these two the
        // halo key never changed when the limiter stopped, so the blurred red
        // arc stayed behind after the sharp one was gone.
        mix (qk (m_limitFrac));
        mixAppearance (quint64 (m_harmFrac  > 0.0005 ? 1 : 0));
        mixAppearance (quint64 (m_limitFrac > 0.0005 ? 1 : 0));
        mix (quint64 (m_grRange));
        mixAppearance (m_settingsHover ? 1u : 0u);
        mixAppearance (quint64 (iw * 31 + ih));

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
        const bool appearanceChanged = (sk != m_glowStructKey);
        if ((m_glowKey != key && (due || appearanceChanged))
            || m_barsBlur.size() != QSize (iw, ih))
        {
            m_glowBuiltAt = nowT;
            m_glowKey = key;
            m_glowStructKey = sk;
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

