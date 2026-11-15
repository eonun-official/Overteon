#pragma once
// ============================================================
//  Overteon — how every meter moves.  ONE file, because it was five.
//
//  The bars, the peak trail, the harmonic ring, the accent light, the spectrum
//  fill and the 32 glow bands each carried their own copy of this maths, and
//  they had drifted: two worked in dB, one in linear amplitude, two in display
//  units, and the interpolation between samples was a different curve in every
//  one of them.  A single audio signal therefore fell at four different speeds
//  depending on which part of the UI you looked at.
//
//  Three things live here:
//
//    smootherstep(f)          the C2 ease (6f^5-15f^4+10f^3).  Used for the
//                             SHAPE of every movement: the release of a bar,
//                             the light, the ring, a spectrum bin.
//    catmullRom(f, p0..p3)    Catmull-Rom, the curve used along an AXIS —
//                             frequency for the glow bands and the analyser
//                             outline, where neighbouring points are separate
//                             measurements of the same thing.
//    Level / Fall             the ballistic state of one displayed value, in
//                             dB: instant attack, eased release, and a speed
//                             derived from dt, never from a frame count.
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
//  documented in OverteonProcessor.cpp and must not come back here.
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
