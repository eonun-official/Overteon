#pragma once
// ============================================================
//  Overteon output limiter
//  Four modes:
//    OFF       : bypass, no peak protection
//    CLIP      : 0-latency soft-knee clipper (sample peak)
//    MODERN    : 1.5 ms lookahead, program-dependent release
//    TRUE PEAK : 2.5 ms lookahead, -0.1 dBFS ceiling
//  The limiter's 2x / 4x / 8x oversampling is independent from
//  the saturation path's global 2x..32x setting.
// ============================================================
#include "Oversampling.h"
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

class OverteonLimiter
{
public:
    enum class Mode { off = 0, clip = 1, modern = 2, truePeak = 3 };

    OverteonLimiter() = default;

    void prepare (double sampleRate, int maxBlockSize)
    {
        sr = sampleRate;
        maxBlock = juce::jmax (1, maxBlockSize);
        work.setSize (2, maxBlock, false, false, true);

        // Same high-quality half-band recipe as the saturation chain (one copy
        // in Oversampling.h); the limiter's own 2x/4x/8x/16x objects are still
        // independent of the main chain's.
        for (int i = 0; i < 4; ++i)
            omega::initHighQualityOversampling (os[i], i + 1, maxBlock);

        // 2.5 ms at 16x is the longest lookahead this limiter ever needs.
        maxLook = (int) std::ceil (0.0025 * sr * 16.0) + 2;
        delayL.assign ((size_t) maxLook + 2, 0.0);
        delayR.assign ((size_t) maxLook + 2, 0.0);
        dqCap = maxLook + 4;
        dqIdx.assign ((size_t) dqCap, 0);
        dqVal.assign ((size_t) dqCap, 0.0);

        for (auto& o : os)
            o->reset();
        reset();
    }

    void setMode (Mode m)
    {
        if (m != mode)
        {
            mode = m;
            if (os[osIndex] != nullptr) os[osIndex]->reset();
            reset();
        }
    }

    void setOversampling (int index)
    {
        index = juce::jlimit (0, 3, index);
        if (index != osIndex)
        {
            osIndex = index;
            if (os[osIndex] != nullptr) os[osIndex]->reset();
            reset();
        }
    }

    Mode getMode() const noexcept { return mode; }
    int  getOversampling() const noexcept { return osIndex; }

    /// Deepest gain reduction applied in the last process() call, in dB.
    ///
    /// This is the limiter's OWN gain (or the soft-clip delta in CLIP mode),
    /// not a difference between input and output peaks: lookahead means the
    /// peak that caused a reduction is not in the same output sample, so a
    /// peak-to-peak measurement under-reads exactly the transients the ring is
    /// meant to show.  Zero in OFF mode, and reset on every block.
    double getLastGainReductionDb() const noexcept { return lastGainReductionDb; }

    int getLatencySamples() const
    {
        if (mode == Mode::off || mode == Mode::clip) return 0;
        return lookaheadBaseSamples()
             + (int) std::lround (os[osIndex]->getLatencyInSamples());
    }

    void process (double* L, double* R, int n)
    {
        lastGainReductionDb = 0.0;
        if (n <= 0 || mode == Mode::off) return;

        if (mode == Mode::clip)
        {
            // CLIP has no lookahead gain: the reduction is the per-sample
            // difference between the input and the soft-knee output.  The
            // below-knee region is untouched, so it contributes 0 dB.
            constexpr double knee = 0.8912509381;
            double maxGr = 0.0;
            for (int i = 0; i < n; ++i)
            {
                for (double* ch : { &L[i], &R[i] })
                {
                    const double a = std::abs (*ch);
                    if (a <= knee) continue;
                    const double y = std::abs (softClip (*ch));
                    if (y > 1.0e-12)
                        maxGr = juce::jmax (maxGr, 20.0 * std::log10 (a / y));
                }
                L[i] = softClip (L[i]);
                R[i] = softClip (R[i]);
            }
            lastGainReductionDb = maxGr;
            return;
        }

        processLimited (L, R, n);
    }

    void reset()
    {
        std::fill (delayL.begin(), delayL.end(), 0.0);
        std::fill (delayR.begin(), delayR.end(), 0.0);
        dqHead = dqCount = 0;
        pos = 0;
        delayPos = 0;
        g = 1.0;
        lookCount = 0;
        delayLen = 1;
        lastGainReductionDb = 0.0;
    }

private:
    // Soft-knee clipper: linear below -1 dBFS, then a smooth tanh knee that
    // asymptotes to 0 dBFS. Purely a 0-latency fallback.
    static double softClip (double x)
    {
        constexpr double knee  = 0.8912509381;   // -1 dBFS
        constexpr double range = 1.0 - knee;
        const double a = std::abs (x);
        if (a <= knee) return x;
        const double y = knee + range * std::tanh ((a - knee) / range);
        return std::copysign (juce::jmin (1.0, y), x);
    }

    int factor() const noexcept { return 1 << (osIndex + 1); }   // 2, 4, 8, 16

    int lookaheadBaseSamples() const noexcept
    {
        const double ms = (mode == Mode::truePeak) ? 2.5 : 1.5;
        return (int) std::lround (sr * ms * 0.001);
    }

    void processLimited (double* L, double* R, int n)
    {
        if (work.getNumSamples() < n) return;   // prepare() is authoritative

        const int fac = factor();
        const int baseLook = lookaheadBaseSamples();
        lookCount = baseLook * fac;
        // Read-before-write ring: exactly lookCount samples of delay.
        delayLen = juce::jmax (1, lookCount);
        if (delayLen > (int) delayL.size())
            delayLen = (int) delayL.size();

        const double osRate   = sr * (double) fac;
        const double attackMs = (mode == Mode::truePeak) ? 0.35 : 0.25;
        const double relFast  = (mode == Mode::truePeak) ? 150.0 : 80.0;
        const double relSlow  = (mode == Mode::truePeak) ? 450.0 : 320.0;
        attackCoef  = 1.0 - std::exp (-1.0 / (attackMs * 0.001 * osRate));
        relFastCoef = 1.0 - std::exp (-1.0 / (relFast  * 0.001 * osRate));
        relSlowCoef = 1.0 - std::exp (-1.0 / (relSlow  * 0.001 * osRate));

        const double ceiling = (mode == Mode::truePeak) ? 0.9885530947 : 1.0;

        for (int i = 0; i < n; ++i)
        {
            work.getWritePointer (0)[i] = L[i];
            work.getWritePointer (1)[i] = R[i];
        }

        juce::dsp::AudioBlock<double> block (work);
        auto base = block.getSubBlock (0, (size_t) n);
        auto up   = os[osIndex]->processSamplesUp (base);
        const int m = (int) up.getNumSamples();
        auto* ul = up.getChannelPointer (0);
        auto* ur = up.getChannelPointer (1);

        const int windowLen = lookCount + 1;
        double minG = 1.0;

        for (int i = 0; i < m; ++i)
        {
            const double l  = ul[i];
            const double r  = ur[i];
            const double pk = juce::jmax (std::abs (l), std::abs (r));

            // Monotonic deque: linked max over the last `windowLen` OS samples.
            while (dqCount > 0)
            {
                const int back = (dqHead + dqCount - 1) % dqCap;
                if (dqVal[(size_t) back] <= pk) --dqCount;
                else break;
            }
            const int tail = (dqHead + dqCount) % dqCap;
            dqIdx[(size_t) tail] = pos;
            dqVal[(size_t) tail] = pk;
            ++dqCount;

            while (dqCount > 0 && dqIdx[(size_t) dqHead] <= pos - windowLen)
            {
                dqHead = (dqHead + 1) % dqCap;
                --dqCount;
            }

            const double mx = juce::jmax (dqVal[(size_t) dqHead], 1.0e-12);
            double target = ceiling / mx;
            if (target > 1.0) target = 1.0;

            if (target < g)
                g += (target - g) * attackCoef;
            else
            {
                // Program-dependent release: dense/deep limiting releases
                // more slowly, isolated peaks recover quickly.
                const double gr  = 1.0 - g;
                const double rel = relFastCoef + (relSlowCoef - relFastCoef)
                                 * juce::jlimit (0.0, 1.0, gr * 3.0);
                g += (target - g) * rel;
            }

            minG = juce::jmin (minG, g);

            const int di = delayPos;
            const double dl = delayL[(size_t) di];
            const double dr = delayR[(size_t) di];
            delayL[(size_t) di] = l;
            delayR[(size_t) di] = r;
            delayPos = (delayPos + 1) % delayLen;

            // The window max already anticipates the delayed peak; the clamp
            // is a final numeric guard, not the primary limiting mechanism.
            ul[i] = juce::jlimit (-ceiling, ceiling, dl * g);
            ur[i] = juce::jlimit (-ceiling, ceiling, dr * g);
            ++pos;
        }

        os[osIndex]->processSamplesDown (base);

        for (int i = 0; i < n; ++i)
        {
            L[i] = juce::jlimit (-ceiling, ceiling, work.getSample (0, i));
            R[i] = juce::jlimit (-ceiling, ceiling, work.getSample (1, i));
        }

        lastGainReductionDb = (minG < 0.9999)
                            ? -20.0 * std::log10 (juce::jmax (1.0e-9, minG))
                            : 0.0;
    }

    double sr = 48000.0;
    int maxBlock = 512;
    int maxLook = 0;
    int lookCount = 0;
    int delayLen = 1;
    int delayPos = 0;
    int dqCap = 0, dqHead = 0, dqCount = 0;
    std::int64_t pos = 0;
    double g = 1.0;
    double lastGainReductionDb = 0.0;
    double attackCoef = 0.0, relFastCoef = 0.0, relSlowCoef = 0.0;

    Mode mode = Mode::modern;
    int osIndex = 1;

    juce::AudioBuffer<double> work;
    std::vector<double> delayL, delayR;
    std::vector<std::int64_t> dqIdx;
    std::vector<double> dqVal;
    std::unique_ptr<juce::dsp::Oversampling<double>> os[4];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OverteonLimiter)
};
