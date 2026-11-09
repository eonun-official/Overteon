#pragma once
// ============================================================
//  OnyxTEON — the analyser's averaging, in ONE testable place.
//
//  The spectrum fill is the one display that is fed raw FFT magnitudes, and an
//  FFT magnitude of noise is Rayleigh-distributed: consecutive bins and
//  consecutive measurements of the SAME bin differ by several dB even when the
//  signal is perfectly steady.  Drawing that directly is what "the spectrum is
//  not smooth, it shakes" describes, and it cannot be fixed by shaping the
//  existing dB values — a random walk in dB is still a random walk.
//
//  So the averaging happens where the physics is:
//
//    1. dB -> POWER.  Every smoothing below is a mean of ENERGY, because that
//       is the quantity an analyser measures.  Averaging the 0..1 dB display
//       value (which is what this used to do) averages decibels, and the mean
//       of a Rayleigh variable's dB is not the dB of its mean: the outline sits
//       low and still twitches.
//
//    2. Along FREQUENCY: a 1/3-octave Gaussian.  The 128 bins are log-spaced
//       at 12.8 per octave, so 1/3 octave is 4.27 bins wide (sigma 1.7).  This
//       is what a fractional-octave analyser does and it is the only step that
//       removes the bin-to-bin sawtooth.
//
//    3. Along TIME: a one-pole mean in the power domain, with separate attack
//       and release constants.  A one-pole in power IS the exponential mean
//       that defines a running RMS, so this is the spectrum equivalent of what
//       ovb::Rms does for the level meters, and the fall it produces,
//       4.343/tau dB/s, is the same relationship.
//
//  Why not ovb::Fall (the dB-domain constant-rate release the bars use): a bar
//  shows one number and a constant dB/s fall is exactly right for it.  A
//  spectrum shows 128 numbers whose differences are what the eye reads, and a
//  dB-domain ramp applied per bin preserves every one of those differences — the
//  outline falls smoothly and still shakes.  Power averaging is what kills the
//  shake.
//
//  Measured (tools/units/spectrum_test.cpp, which builds the input as a comb of
//  Rayleigh-like bins and runs the OLD chain beside the new one on the same
//  data, so the two numbers are a comparison and not a memory):
//
//      frame-to-frame change, 95th percentile, steady noise
//          old (dB domain, 1/6 oct, 85 dB/0.3 s)   5.47 dB
//          new (power domain, 1/2 oct, 0.26 s)     1.05 dB
//      bin-to-bin sawtooth, 95th percentile
//          old 2.56 dB    new 0.90 dB
//      release, once the signal stops
//          old 283 dB/s   new 16.6 dB/s
//
//  And on real audio (ONYXTEON_SPECDBG=all, one frame's 128 bins per line):
//  frame-to-frame change has a median of 0.43 dB and a 95th percentile of
//  1.6-2.0 dB, the tail above that being the music itself.
// ============================================================

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ovspec
{

constexpr int    kBins     = 128;
constexpr double kSpanDb   = 85.0;      // the table's 0..1 covers 85 dB
constexpr double kFloorDb  = -85.0;     // table value 0

/// Half-width of the frequency kernel, in bins.  7 covers the sigma 2.5 used
/// below out to 2.8 sigma, where the tail is ~2 % of the peak — beyond that the
/// truncation would start to show as a flat floor at both ends.
constexpr int kFreqHalf = 7;

inline double normToDb (double v)  { return std::clamp (v, 0.0, 1.0) * kSpanDb + kFloorDb; }
inline double dbToNorm (double db) { return std::clamp ((db - kFloorDb) / kSpanDb, 0.0, 1.0); }
inline double powerOfDb (double db) { return std::pow (10.0, db * 0.1); }
inline double dbOfPower (double p)  { return 10.0 * std::log10 (std::max (p, 1e-30)); }

struct SpectrumBallistics
{
    // ---- tunables, all in real units --------------------------------------
    /// Power-domain one-pole constants.  The fall is 4.343/tau dB/s, so
    /// 0.12 s is 36 dB/s and a full 85 dB sweep takes 2.4 s — the value that
    /// replaced "85 dB in 0.3 s", which measured 283 dB/s and read as a snap.
    double attackTau  = 0.096;      // 120 ms x 1.25
    double releaseTau = 0.208;      // 260 ms x 1.25  ->  4.343/0.208 = 20.9 dB/s

    /// Half an octave.  sigma in bins: 12.8 bins/octave * (1/2) / 2.5 = 2.56,
    /// rounded to 2.5.  Chosen from a sweep of (attack, release, sigma) against
    /// a steady signal — the combination that gets the frame-to-frame change
    /// under 1 dB while still moving 20 dB in a quarter of a second.
    double freqSigma  = 2.5;

    void reset()
    {
        for (int i = 0; i < kBins; ++i) { m_pow[i] = 0.0; m_out[i] = 0.0f; }
    }

    /// raw01: the engine's table, 0..1 over 85 dB.  out01 receives the same
    /// scale.  dt in seconds; a caller that runs at 30 Hz passes 0.033.
    void advance (const float* raw01, float* out01, double dt)
    {
        if (! (dt > 0.0)) dt = 1.0 / 30.0;
        buildKernel();

        // ---- 1+2. dB -> power, then the 1/3-octave mean -------------------
        double p[kBins];
        for (int i = 0; i < kBins; ++i)
            p[i] = powerOfDb (normToDb (raw01[i]));

        double sm[kBins];
        for (int i = 0; i < kBins; ++i)
        {
            double acc = 0.0, wsum = 0.0;
            for (int k = -kFreqHalf; k <= kFreqHalf; ++k)
            {
                const int j = std::clamp (i + k, 0, kBins - 1);
                const double w = m_kernel[k + kFreqHalf];
                acc  += p[j] * w;
                wsum += w;
            }
            sm[i] = (wsum > 0.0) ? acc / wsum : 0.0;
        }

        // ---- 3. the power-domain one-pole --------------------------------
        const double aUp = 1.0 - std::exp (-dt / std::max (1e-5, attackTau));
        const double aDn = 1.0 - std::exp (-dt / std::max (1e-5, releaseTau));
        for (int i = 0; i < kBins; ++i)
        {
            const double target = sm[i];
            const double a = (target >= m_pow[i]) ? aUp : aDn;
            m_pow[i] += (target - m_pow[i]) * a;
            if (! std::isfinite (m_pow[i])) m_pow[i] = 0.0;
            m_out[i] = float (dbToNorm (dbOfPower (m_pow[i])));
            out01[i] = m_out[i];
        }
    }

    /// The state, for a cache key or a debug trace.
    const float* shown() const { return m_out; }

    /// dB/s the outline falls at once the signal stops (the same relation as
    /// ovb::Rms::decayDbPerSec).
    double releaseDbPerSec() const { return 4.343 / std::max (1e-5, releaseTau); }

private:
    double m_pow[kBins] {};
    float  m_out[kBins] {};
    double m_kernel[2 * kFreqHalf + 1] {};
    double m_kernelSigma = -1.0;

    void buildKernel()
    {
        if (m_kernelSigma == freqSigma) return;
        double sum = 0.0;
        for (int k = -kFreqHalf; k <= kFreqHalf; ++k)
        {
            const double x = double (k) / std::max (1e-3, freqSigma);
            m_kernel[k + kFreqHalf] = std::exp (-0.5 * x * x);
            sum += m_kernel[k + kFreqHalf];
        }
        for (auto& w : m_kernel) w /= sum;
        m_kernelSigma = freqSigma;
    }
};

}   // namespace ovspec
