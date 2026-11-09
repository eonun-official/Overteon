#pragma once
// ============================================================
//  OnyxTEON — the analyser's averaging.
//
//  THE PAGE HAS NONE.  web_fx/index.html does this and only this:
//
//      window.__JUCE__.backend.addEventListener ("spectrum", arr => {
//        for (let i=0;i<128&&i<arr.length;i++){ pqPanel.spectrum[i]=arr[i];
//                                              scPanel.spectrum[i]=arr[i]*0.85; }
//        pqPanel.drawSpec();   // a polyline through the raw 128 bins
//      });
//
//  so what the page shows is the measurement, frame by frame, and that is what
//  this class does by default: `advance()` copies the table through unchanged.
//  Anything else on top of it is a display filter the page does not have, and
//  the reference for this UI is the page.
//
//  The consequence is real and is not hidden: an FFT magnitude of steady noise
//  has a ~5.6 dB standard deviation per frame, so the outline moves about 5 dB
//  per frame on noise and the fill reads as "shaking".  That IS the page.  Where
//  it is not wanted (a 128-bin outline on a 640 px canvas is not the same
//  picture as an FFT of 2048 samples in a browser), the smoothing below can be
//  switched on with three environment variables, and it was tuned by
//  measurement rather than by taste:
//
//      ONYXTEON_SPECREL=0.26    release, seconds (4.343/tau dB/s)
//      ONYXTEON_SPECATT=0.12    attack, seconds
//      ONYXTEON_SPECSIGMA=2.5   frequency kernel, bins (1.7 = 1/3 octave,
//                               2.5 = 1/2 octave at 12.8 bins per octave)
//
//  With those on, tools/units/spectrum_test.cpp measures, on a steady signal:
//
//      frame-to-frame change, 95th pct   5.47 dB (page)  ->  1.05 dB (smoothed)
//      bin-to-bin sawtooth, 95th pct     2.56 dB          ->  0.90 dB
//      release after silence             none (page)      ->  16.6 dB/s
//
//  and the averaging is done where the physics is: dB -> POWER first, because
//  the mean of decibels is not the decibel of the mean, then a Gaussian along
//  frequency (that is the only step that removes bin-to-bin sawtooth), then a
//  one-pole along time in the same power domain (which IS a running RMS).
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
    /// Off by default: the page's behaviour is a straight copy of the table.
    /// Turned on by OvEqView when any ONYXTEON_SPEC* is set.
    bool   smooth = false;

    // ---- tunables, all in real units --------------------------------------
    /// Power-domain one-pole constants.  The fall is 4.343/tau dB/s, so 0.26 s
    /// is 16.6 dB/s and a full 85 dB sweep takes 5 s.
    double attackTau  = 0.120;
    double releaseTau = 0.260;

    /// 1/2 octave.  sigma in bins: 12.8 bins/octave * (1/2) / 2.5 = 2.56.
    double freqSigma  = 2.5;

    void reset()
    {
        for (int i = 0; i < kBins; ++i) { m_pow[i] = 0.0; m_out[i] = 0.0f; }
        m_primed = false;
    }

    /// raw01: the engine's table, 0..1 over 85 dB.  out01 receives the same
    /// scale.  dt in seconds; a caller that runs at 30 Hz passes 0.033.
    void advance (const float* raw01, float* out01, double dt)
    {
        if (! smooth)
        {
            // THE PAGE: the table, as it arrived, nothing added.
            std::memcpy (out01, raw01, sizeof (float) * kBins);
            std::memcpy (m_out, raw01, sizeof (float) * kBins);
            return;
        }

        if (! (dt > 0.0)) dt = 1.0 / 30.0;
        buildKernel();

        // ---- 1+2. dB -> power, then the fractional-octave mean -----------
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
        //
        // Seeded on the first frame from the measurement itself: starting at
        // zero power means the first second after opening the editor ramps up
        // from silence, which is a visible artefact and not smoothing.
        if (! m_primed)
        {
            for (int i = 0; i < kBins; ++i) m_pow[i] = sm[i];
            m_primed = true;
        }

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
    /// ovb::Rms::decayDbPerSec).  Zero when smoothing is off, because then the
    /// outline follows the measurement and has no rate of its own.
    double releaseDbPerSec() const { return smooth ? 4.343 / std::max (1e-5, releaseTau) : 0.0; }

private:
    double m_pow[kBins] {};
    float  m_out[kBins] {};
    bool   m_primed = false;
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
