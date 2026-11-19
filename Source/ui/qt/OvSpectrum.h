#pragma once
// ============================================================
//  Overteon — the analyser's averaging, in ONE testable place.
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
//    2. Along FREQUENCY: a half-octave Gaussian.  The 1024 bins are log-spaced
//       at 25.6 per octave, so half an octave is sigma 5 bins.  This is what a
//       fractional-octave analyser does and it is the only step that removes
//       the bin-to-bin sawtooth.
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
//  And on real audio (OVERTEON_SPECDBG=all, one frame's 128 bins per line):
//  frame-to-frame change has a median of 0.43 dB and a 95th percentile of
//  1.6-2.0 dB, the tail above that being the music itself.
// ============================================================

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ovspec
{

constexpr int    kBins     = 1024;
constexpr double kSpanDb   = 96.0;      // -90 .. +6 dBFS
constexpr double kFloorDb  = -90.0;     // table value 0

/// Half-width of the frequency kernel, in bins.  14 covers the sigma 5 used
/// below out to 2.8 sigma, where the tail is ~2 % of the peak — beyond that the
/// truncation would start to show as a flat floor at both ends.  At 1024 log
/// bins (25.6 per octave) this is the same half-octave, peak band as the
/// 7-bin / sigma-2.5 kernel was at 256 bins.
constexpr int kFreqHalf = 14;

inline double normToDb (double v)  { return std::clamp (v, 0.0, 1.0) * kSpanDb + kFloorDb; }
inline double dbToNorm (double db) { return std::clamp ((db - kFloorDb) / kSpanDb, 0.0, 1.0); }
inline double powerOfDb (double db) { return std::pow (10.0, db * 0.1); }
inline double dbOfPower (double p)  { return 10.0 * std::log10 (std::max (p, 1e-30)); }

struct SpectrumBallistics
{
    /// How the band levels are detected.
    ///
    /// Rms is the power-domain one-pole below: a mean of energy, which is what
    /// an analyser normally shows and what made the outline stable (the mean of
    /// dB is not the dB of the mean — see the notes at the top of the file).
    ///
    /// Peak takes the MAX bin in the band instead of the mean, and holds it in
    /// time: a new maximum lands instantly, only the fall follows the release
    /// constant.  Both halves matter — the mean across a half-octave averages
    /// away a narrow tone, and the time mean averages away a transient, so a
    /// "peak" that kept either would not be showing a peak.  It uses the same
    /// release as Rms, so the two traces decay together instead of drifting.
    enum class Mode { Rms, Peak };
    Mode mode = Mode::Rms;

    /// Interpolate towards each measurement (true) or take it verbatim (false).
    /// Verbatim is for judging the analyser rather than watching it: a ramp
    /// between two measurements asserts values nothing measured.
    bool interp = true;

    // ---- tunables, all in real units --------------------------------------
    /// Power-domain one-pole constants.  The fall is 4.343/tau dB/s, so
    /// 0.12 s is 36 dB/s and a full 85 dB sweep takes 2.4 s — the value that
    /// replaced "85 dB in 0.3 s", which measured 283 dB/s and read as a snap.
    double attackTau  = 0.096;      // 120 ms x 1.25
    double releaseTau = 0.208;      // 260 ms x 1.25  ->  4.343/0.208 = 20.9 dB/s

    /// Half an octave.  sigma in bins: 25.6 bins/octave / 2 / 2.5 = 5.12,
    /// rounded to 5.0.  Chosen from a sweep of (attack, release, sigma) against
    /// a steady signal — the combination that gets the frame-to-frame change
    /// under 1 dB while still moving 20 dB in a quarter of a second.
    double freqSigma  = 5.0;        // half an octave at 1024 bins

    void reset()
    {
        for (int i = 0; i < kBins; ++i) { m_pow[i] = 0.0; m_out[i] = 0.0f; }
    }

    /// raw01: the engine's table, 0..1 over the plot's dB span.  out01 receives
    /// the same scale.  dt in seconds; a caller that runs at 60 Hz passes 0.0167.
    void advance (const float* raw01, float* out01, double dt)
    {
        if (! (dt > 0.0)) dt = 1.0 / 60.0;

        // ---- RAW: no averaging at all ------------------------------------
        //
        // This is the MiniMeters-style outline: one point per log bin, taken
        // straight from the FFT.  Smoothing along frequency would flatten the
        // narrow peaks that carry the detail, and smoothing along time would
        // hold them for frames.  With the analyser now running on every frame
        // the raw data is already continuous, so bypassing both is what makes
        // the curve look fine AND move smoothly.
        //
        // The filter state is re-seeded from the current measurement when the
        // eased display comes back, so it resumes from what is on screen
        // instead of from wherever it was left.
        if (! interp)
        {
            m_lastInterp = false;
            for (int i = 0; i < kBins; ++i)
            {
                m_out[i] = raw01[i];
                out01[i] = raw01[i];
            }
            return;
        }

        // Coming back from RAW: seed the time filter with the current
        // measurement, or the eased display would ease from wherever the
        // filter state was left instead of from what is on screen now.
        if (! m_lastInterp)
        {
            for (int i = 0; i < kBins; ++i)
                m_pow[i] = powerOfDb (normToDb (raw01[i]));
            m_lastInterp = true;
        }

        buildKernel();

        // ---- 1+2. dB -> power, then the 1/3-octave mean -------------------
        double p[kBins];
        for (int i = 0; i < kBins; ++i)
            p[i] = powerOfDb (normToDb (raw01[i]));

        double sm[kBins];
        for (int i = 0; i < kBins; ++i)
        {
            if (mode == Mode::Peak)
            {
                // Loudest bin in the band, not the band's mean: a narrow tone
                // averaged over half an octave reads far below its own level,
                // which is the opposite of what a peak display is for.
                double mx = 0.0;
                for (int k = -kFreqHalf; k <= kFreqHalf; ++k)
                    mx = std::max (mx, p[std::clamp (i + k, 0, kBins - 1)]);
                sm[i] = mx;
            }
            else
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
        }

        // ---- 3. detect in time -------------------------------------------
        const double aUp = 1.0 - std::exp (-dt / std::max (1e-5, attackTau));
        const double aDn = 1.0 - std::exp (-dt / std::max (1e-5, releaseTau));
        for (int i = 0; i < kBins; ++i)
        {
            const double target = sm[i];

            if (mode == Mode::Rms)
            {
                const double a = (target >= m_pow[i]) ? aUp : aDn;
                m_pow[i] += (target - m_pow[i]) * a;
            }
            else
            {
                // Max-hold.  No attack smoothing at all: the whole point of a
                // peak reading is that the first frame containing the transient
                // already shows it.  Only the fall is ballisted, with the same
                // release constant the RMS mode uses, so the two traces decay
                // together instead of drifting apart.
                if (target >= m_pow[i]) m_pow[i] = target;
                else                    m_pow[i] += (target - m_pow[i]) * aDn;
            }

            if (! std::isfinite (m_pow[i])) m_pow[i] = 0.0;
            // Raw bypasses m_pow entirely for the DISPLAYED value: the filter
            // state still follows, so switching back resumes from the right
            // place rather than from wherever the display was pinned.
            m_out[i] = float (dbToNorm (dbOfPower (interp ? m_pow[i] : target)));
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
    bool   m_lastInterp = true;      // was the previous frame smoothed?

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
