#pragma once
// ============================================================
//  OnyxTEON — how every meter moves.  ONE file, and the reference is the WEB
//  UI (web_fx/index.html), not a convention and not a taste.
//
//  Read off the page, its meters have exactly TWO time constants:
//
//    IN/OUT bars   stroke-dashoffset = 100*(1-mdb(v))     no smoothing at all
//    peak trail    hold 1.1 s, then p -= dt*0.18          (dt clamped to 0.1 s)
//                  => 0.18 of the 60 dB scale per second = 10.8 dB/s
//    HARM ring     stroke-dashoffset = 100*(1-f)          no smoothing at all;
//                  the smoothing lives in the C++ (harmRatioS, 0.15 per block
//                  = a 131 ms one-pole)
//    --light       opacity = level^2, CSS                  transition .1s linear
//    spectrum      raw 128 bins into a polyline            no smoothing at all
//
//  So the ONLY smoothing a display may add is the peak trail's 10.8 dB/s and a
//  100 ms LINEAR transition on the light.  Everything else is drawn from the
//  measurement in the frame it arrives.  That is not a simplification — it is
//  what the page does, and the page is what the plugin is supposed to look and
//  feel like.
//
//  The consequence, measured rather than assumed: the analyser moves about 5 dB
//  per frame on steady noise, because an FFT magnitude of noise has a ~5.6 dB
//  standard deviation and the page draws it unfiltered.  That is the page's
//  appearance.  If it is not wanted, smoothing can be switched on per value
//  with the ONYXTEON_SPEC* knobs (see ovspec::SpectrumBallistics) instead of
//  being invented here.
//
//  What lives here:
//
//    smootherstep(f)      the C2 ease (6f^5-15f^4+10f^3), for movements that do
//                         have a duration; smoothstep is only C1 and its second
//                         derivative jumps at both ends, which reads as a facet
//                         under the pod's 3 px Gaussian.
//    catmullRom(f, ...)   the curve along an AXIS (frequency), where
//                         neighbouring points are separate measurements of the
//                         same thing.  Limited form: no overshoot, because an
//                         overshoot on a meter draws a level that never existed.
//    Fall                 a dB value with an instant attack and a constant
//                         dB/second release.  Used ONLY by the peak trail, whose
//                         10.8 dB/s is the one release the page has.
//    LinearGlide          the CSS `transition: opacity .1s linear` — a linear
//                         ramp to a new target that restarts whenever the target
//                         changes.  Used by the light and the glow bands, which
//                         is where the page's only display smoothing comes from.
//    Rms                  the true mean-square detector behind the level meters
//                         (tau 300 ms), so a fall has a rate that means
//                         something: 4.343/tau dB/s.
//
//  Why smootherstep and not smoothstep (3f^2-2f^3): smoothstep is only C1, so
//  its second derivative jumps at both ends of every movement.  Under a 3 px
//  Gaussian that discontinuity reads as a facet — the value visibly "hits" its
//  new level.  smootherstep's second derivative is zero at both ends too.
//
//  Why Catmull-Rom and not a natural cubic: it is local (p0..p3 only), so a new
//  measurement changes the curve over three intervals rather than globally, and
//  it needs no tridiagonal solve per frame.  The overshoot a plain Catmull-Rom
//  is famous for is not acceptable on a meter (it would draw a level the signal
//  never had), so catmullRomLimited() clamps the tangents the way Fritsch-Carlson
//  does in PCHIP: same curve, no overshoot.
//
//  All arithmetic is double and all time is SECONDS.  A coefficient per frame
//  would make the same build fall twice as fast at 60 Hz as at 30 — that bug is
//  documented in FxProcessor.cpp and must not come back here.
// ============================================================

#include <algorithm>
#include <cmath>

namespace ovb
{

// ---------------------------------------------------------------------------
//  smootherstep — 6f^5 - 15f^4 + 10f^3, clamped to [0,1].
//
//  S(0)=0, S(1)=1, S'(0)=S'(1)=0, S''(0)=S''(1)=0.
// ---------------------------------------------------------------------------
inline double smootherstep (double f)
{
    f = std::clamp (f, 0.0, 1.0);
    return f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
}

// The derivative, for the tests and for anything that needs the slope.
inline double smootherstepSlope (double f)
{
    f = std::clamp (f, 0.0, 1.0);
    return 30.0 * f * f * (f - 1.0) * (f - 1.0);
}

// ---------------------------------------------------------------------------
//  Catmull-Rom through p1 -> p2, with p0 and p3 as the neighbours that set the
//  tangents.  f in [0,1] runs from p1 to p2.
// ---------------------------------------------------------------------------
inline double catmullRom (double p0, double p1, double p2, double p3, double f)
{
    const double f2 = f * f;
    const double f3 = f2 * f;
    return 0.5 * ((2.0 * p1)
                + (-p0 + p2) * f
                + (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * f2
                + (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * f3);
}

// ---------------------------------------------------------------------------
//  Catmull-Rom whose segment cannot leave [min(p1,p2), max(p1,p2)].
//
//  The tangents are Catmull-Rom's central differences (p2-p0)/2 and (p3-p1)/2,
//  limited so that a segment cannot overshoot: a monotone triple gets tangents
//  of at most 3x its slope, and a flat or reversing triple gets a zero tangent
//  at that point.  A flat tangent at a plateau is CORRECT (the value really is
//  constant there); the "beads on a string" failure was smootherstep, which
//  flattens at every point including the ones in the middle of a ramp.
// ---------------------------------------------------------------------------
inline double catmullRomLimited (double p0, double p1, double p2, double p3, double f)
{
    const double d  = p2 - p1;                 // this segment's slope
    auto tangent = [] (double central, double seg)
    {
        if (seg == 0.0 || central * seg <= 0.0) return 0.0;   // flat or reversing
        const double lim = 3.0 * std::abs (seg);
        return std::clamp (central, -lim, lim);
    };

    const double m1 = tangent (0.5 * (p2 - p0), d);
    const double m2 = tangent (0.5 * (p3 - p1), d);

    // Hermite form with those tangents.
    const double f2 = f * f;
    const double f3 = f2 * f;
    const double h00 =  2.0 * f3 - 3.0 * f2 + 1.0;
    const double h10 =        f3 - 2.0 * f2 + f;
    const double h01 = -2.0 * f3 + 3.0 * f2;
    const double h11 =        f3 -       f2;
    const double v = h00 * p1 + h10 * m1 + h01 * p2 + h11 * m2;

    // Belt and braces: the limit above is what prevents overshoot, this makes
    // it impossible even if a caller passes non-finite control points.
    const double lo = std::min (p1, p2), hi = std::max (p1, p2);
    return std::isfinite (v) ? std::clamp (v, lo, hi) : p1;
}

// ---------------------------------------------------------------------------
//  The ballistics of one displayed level, in dB.
//
//  attack    instant.  A transient has to show in the frame it happens; any
//            easing here is a lie about the signal.
//  release   a constant number of dB per second, entered through smootherstep
//            over `rampInSec`: the fall starts from rest instead of slamming
//            into full speed on its first frame.
//
//  The exponential form this replaces, step = 1-(target/start)^(1/n_frames),
//  was worst at exactly the wrong moment: its speed is MAXIMAL on the first
//  frame of a fall (so the bar jumps and then crawls) and it never reaches the
//  target at all — it needed a `max(next, target)` clamp to stop, which is a
//  discontinuity dressed up as an asymptote.  Constant dB/s lands exactly.
//
//  Why the fall is not eased OUT as well as in: a position-shaped brake
//  (speed multiplied by smootherstep(remaining/brake)) cannot land.  With the
//  remaining distance as the argument, smootherstep behaves like 6q^5 and the
//  last half decibel takes minutes.  A time-shaped brake would have to know the
//  end in advance, which a meter fed one measurement at a time does not.  The
//  fall therefore ends by reaching the target, which is what "release" means;
//  the ease-in is what removes the visible snap.
// ---------------------------------------------------------------------------
struct Fall
{
    double shown      = -120.0;   // dB, what is on screen
    double fallingFor = 0.0;      // seconds since this fall began

    void reset (double db) { shown = db; fallingFor = 0.0; }

    /// Advance towards `targetDb`.  Returns the new shown value.
    ///
    /// `dBPerSecond` is the release rate; the caller derives it from the
    /// meter convention (20 dB in 0.35 s, 60 dB in 0.30 s, ...), never from a
    /// per-frame coefficient.
    double advance (double targetDb, double dt,
                    double dBPerSecond, double rampInSec = 0.030)
    {
        if (! (dt > 0.0)) dt = 0.0;

        if (targetDb >= shown)
        {
            shown = targetDb;                 // instant attack
            fallingFor = 0.0;
            return shown;
        }

        fallingFor += dt;

        const double ease = smootherstep (fallingFor / std::max (1e-6, rampInSec));
        shown = std::max (targetDb, shown - dBPerSecond * dt * ease);
        return shown;
    }

    /// True while the value still has somewhere to fall to, so a caller can
    /// keep asking for frames after the audio went quiet.
    bool falling (double targetDb) const { return shown > targetDb + 1e-9; }
};

// ---------------------------------------------------------------------------
//  LinearGlide — CSS `transition: <prop> 0.1s linear`.
//
//  This is the ONLY smoothing the page puts on a display value, so it is what
//  the light and the glow bands use here:
//
//      .controlpit::before{opacity:var(--light,0);transition:opacity .1s linear}
//
//  CSS semantics, reproduced rather than approximated: when the target changes,
//  a LINEAR ramp starts from wherever the value currently is and reaches the new
//  target in `duration` seconds, whatever the distance.  A new target during a
//  ramp starts a new ramp from the current value.  Setting the same target again
//  does nothing (CSS does not restart an unchanged transition), which matters
//  because the light is set on every meter event — restarting every time would
//  make it crawl and never arrive.
// ---------------------------------------------------------------------------
struct LinearGlide
{
    double value   = 0.0;     // what is on screen
    double from    = 0.0;     // where the current ramp started
    double target  = 0.0;
    double elapsed = 0.0;
    double duration = 0.100;  // the page's 0.1 s

    void reset (double v)
    {
        value = from = target = v;
        elapsed = duration;
    }

    /// A new measurement arrived: begin a ramp from here to `t`.
    void set (double t)
    {
        if (t == target) return;            // unchanged: CSS would not restart
        from    = value;
        target  = t;
        elapsed = 0.0;
    }

    /// Advance the ramp.  Returns the current value.
    double advance (double dt)
    {
        if (! (dt > 0.0)) dt = 0.0;
        if (duration <= 0.0) { value = target; return value; }

        elapsed += dt;
        // A tolerance, not an exact compare: dt is accumulated from a clock, so
        // ten 10 ms frames land on 0.09999999999999999 and the value would sit
        // at 0.999999999 while the state still claimed to be ramping.
        if (elapsed + 1e-9 >= duration) { elapsed = duration; value = target; return value; }

        value = from + (target - from) * (elapsed / duration);
        return value;
    }

    bool ramping() const { return elapsed + 1e-9 < duration; }
};

// ---------------------------------------------------------------------------
//  Rms — a TRUE mean-square detector, per sample, one-pole.
//
//  The meters used to be peak meters: max(|x|) held, then multiplied by a
//  decaying coefficient.  That is not RMS, and it is not what the numbers on
//  screen are supposed to be — the manual describes the harmonic ring as an RMS
//  ratio and the level bars as levels, and a peak detector reads the crest
//  factor, so a transient shows several dB high and then falls at whatever
//  coefficient the release happens to have.
//
//  Here the state is MEAN SQUARE, updated once per sample:
//
//      ms += (x*x - ms) * a,   a = 1 - exp(-1 / (fs * tau))
//
//  so the amplitude is sqrt(ms) and the level is 10*log10(ms).  Three things
//  follow for free, and all three are what "RMS" means:
//
//    * a full-scale sine reads -3.01 dBFS and a full-scale square reads 0 dBFS;
//    * a signal that stops decays at 4.343/tau dB per second — for tau = 300 ms
//      that is 14.5 dB/s, four times slower than the 57 dB/s a peak meter with
//      a 141 ms coefficient produced;
//    * the decay is tied to TIME, not to blocks: 256 samples per block and 4096
//      samples per block below the same signal must agree, and they do because
//      `a` is derived from tau and fs.
//
//  The old code had exactly this bug in one place and fixed it in another: the
//  level meters were converted to a time-derived coefficient and the harmonic
//  ratio kept `+= 0.15 * (x - v)` per BLOCK, i.e. 131 ms at 48 kHz / 1024 and
//  33 ms at 48 kHz / 256.  onePoleCoef() below is the fix for that shape of bug.
// ---------------------------------------------------------------------------
struct Rms
{
    double ms  = 0.0;      // mean square
    double tau = 0.300;    // integration time constant, seconds

    static double coefPerSample (double sampleRate, double tauSeconds)
    {
        if (! (sampleRate > 0.0)) return 1.0;
        return 1.0 - std::exp (-1.0 / (sampleRate * std::max (1e-5, tauSeconds)));
    }

    void reset() { ms = 0.0; }

    void process (const double* x, int n, double sampleRate)
    {
        const double a = coefPerSample (sampleRate, tau);
        double m = ms;
        for (int i = 0; i < n; ++i)
            m += (x[i] * x[i] - m) * a;
        ms = m;
    }

    double amplitude() const { return std::sqrt (std::max (0.0, ms)); }
    double db() const        { return 10.0 * std::log10 (std::max (ms, 1e-18)); }

    /// The rate this detector falls at once the signal stops, dB/s.  Used to
    /// size the display's release so it can never run ahead of the measurement.
    double decayDbPerSec() const { return 4.343 / std::max (1e-5, tau); }
};

/// Coefficient for "move this much of the way over dt seconds", i.e. a block
/// rate-independent one-pole.  0.15 per block at 21.3 ms is not 0.15 at 5.3 ms.
inline double onePoleCoef (double dt, double tauSeconds)
{
    return 1.0 - std::exp (-std::max (0.0, dt) / std::max (1e-6, tauSeconds));
}

// ---------------------------------------------------------------------------
//  Conversions between a meter's 0..1 display scale and dB.
//  The scale is linear in dB (a meter's scale always is), so these are the
//  only two places the range appears.
// ---------------------------------------------------------------------------
inline double normToDb (double norm, double spanDb, double floorDb = 0.0)
{
    return std::clamp (norm, 0.0, 1.0) * spanDb + floorDb;
}

inline double dbToNorm (double db, double spanDb, double floorDb = 0.0)
{
    return std::clamp ((db - floorDb) / spanDb, 0.0, 1.0);
}

}   // namespace ovb
