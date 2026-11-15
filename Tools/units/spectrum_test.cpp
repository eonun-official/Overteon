// The analyser's smoothness, as a number instead of an opinion.
//
// "The spectrum is too fast and not smooth at all" cannot be settled by
// looking: a still frame of a shaky outline and a still frame of a smooth one
// are both still frames.  What can be measured is how far a bin MOVES between
// two frames and how far it moves between two NEIGHBOURING BINS, both in dB, on
// a signal whose true spectrum is constant.
//
// The input is built the way an FFT actually behaves on steady noise: a fixed
// smooth envelope plus per-bin, per-frame Rayleigh-like magnitude scatter (a
// magnitude estimate from one FFT frame has a standard deviation of about 5.6
// dB regardless of how much it is averaged over the block).  Anything the
// display does on top of that is its own doing.
//
// The old implementation is reproduced here verbatim in shape — 1/6-octave
// triangular average, 40 ms attack, 20 dB/0.35 s release via ovb::Fall — so the
// comparison is a measurement, not a memory.
#include "OvSpectrum.h"
#include "OvBallistics.h"

#include <cstdio>
#include <cmath>
#include <random>
#include <vector>
#include <algorithm>

static int failures = 0;
static void check (bool ok, const char* what, double got = 0.0, double want = 0.0)
{
    if (! ok) { ++failures; std::printf ("FAIL  %-62s got %.3f want %.3f\n", what, got, want); }
    else      std::printf ("ok    %-62s %.3f\n", what, got);
}

// ---------------------------------------------------------------------------
//  The old display chain, kept only as a reference for these numbers.
// ---------------------------------------------------------------------------
struct OldSpectrum
{
    float  shown[128] {};
    ovb::Fall fall[128];
    float  prevRaw[128] {};
    bool   started = false;

    void advance (const float* raw01, float* out, double dt)
    {
        float raw[128];
        std::memcpy (raw, raw01, sizeof raw);

        // 1/6-octave triangular average, on the dB display value
        static constexpr float kK[5] = { 1.0f, 2.0f, 3.0f, 2.0f, 1.0f };
        float sm[128];
        for (int i = 0; i < 128; ++i)
        {
            float acc = 0.0f, wsum = 0.0f;
            for (int k = -2; k <= 2; ++k)
            {
                const int j = std::clamp (i + k, 0, 127);
                acc  += raw[j] * kK[k + 2];
                wsum += kK[k + 2];
            }
            sm[i] = acc / wsum;
        }

        const double attackK = 1.0 - std::exp (-dt / 0.040);
        constexpr double kSpan = 85.0;
        constexpr double kDbPerSec = 85.0 / 0.30;

        for (int i = 0; i < 128; ++i)
        {
            const double shownN = shown[i];
            const double targetN = sm[i];
            if (targetN >= shownN)
            {
                const double next = shownN + (targetN - shownN) * attackK;
                shown[i] = float (next);
                fall[i].reset (next * kSpan);
            }
            else
            {
                fall[i].advance (targetN * kSpan, dt, kDbPerSec, 0.020);
                shown[i] = float (fall[i].shown / kSpan);
            }
            out[i] = shown[i];
        }
    }
};

// ---------------------------------------------------------------------------
//  A steady spectrum: fixed envelope + independent scatter per bin and frame.
// ---------------------------------------------------------------------------
struct Source
{
    std::mt19937 rng { 20261109u };
    std::normal_distribution<double> gauss { 0.0, 5.6 };   // dB, one-frame magnitude

    void frame (float* out01)
    {
        auto& r = rng;
        for (int i = 0; i < 128; ++i)
        {
            const double f = double (i) / 127.0;
            // a musical shape: low shelf, a broad dip, a presence rise
            const double env = -34.0 + 16.0 * std::exp (-f * 6.0)
                             - 8.0 * std::exp (-std::pow ((f - 0.42) / 0.12, 2))
                             + 6.0 * std::exp (-std::pow ((f - 0.72) / 0.10, 2));
            const double db = env + gauss (r);
            out01[i] = float (ovspec::dbToNorm (db));
        }
    }
};

static double percentile (std::vector<double>& v, double q)
{
    if (v.empty()) return 0.0;
    std::sort (v.begin(), v.end());
    const size_t k = std::min (v.size() - 1, size_t (q * double (v.size() - 1) + 0.5));
    return v[k];
}

int main()
{
    constexpr int    kFrames = 180;         // 6 s at 30 Hz
    constexpr double dt     = 1.0 / 30.0;

    // settle for 60 frames before measuring: the one-pole has to reach the
    // envelope, and a startup transient is not what "smooth" is about.
    constexpr int kSettle = 60;

    Source src;
    OldSpectrum oldS;
    ovspec::SpectrumBallistics newS;

    std::vector<double> oldFrame, newFrame, oldBin, newBin;
    float raw[128];
    float prevOld[128] {}, prevNew[128] {};

    for (int n = 0; n < kFrames; ++n)
    {
        src.frame (raw);

        float a[128], b[128];
        oldS.advance (raw, a, dt);
        newS.advance (raw, b, dt);

        if (n < kSettle) continue;

        for (int i = 0; i < 128; ++i)
        {
            // dB change between two frames of the same bin
            oldFrame.push_back (std::abs (ovspec::normToDb (a[i]) - ovspec::normToDb (prevOld[i])));
            newFrame.push_back (std::abs (ovspec::normToDb (b[i]) - ovspec::normToDb (prevNew[i])));
            // dB change between two neighbouring bins in the same frame
            if (i + 1 < 128)
            {
                oldBin.push_back (std::abs (ovspec::normToDb (a[i + 1]) - ovspec::normToDb (a[i])));
                newBin.push_back (std::abs (ovspec::normToDb (b[i + 1]) - ovspec::normToDb (b[i])));
            }
        }
        std::memcpy (prevOld, a, sizeof a);
        std::memcpy (prevNew, b, sizeof b);
    }

    const double oldF95 = percentile (oldFrame, 0.95);
    const double newF95 = percentile (newFrame, 0.95);
    const double oldB95 = percentile (oldBin,   0.95);
    const double newB95 = percentile (newBin,   0.95);

    std::printf ("\n      frame-to-frame change, 95th percentile\n");
    std::printf ("        old (dB domain, 1/6 oct, 0.3 s)      %6.2f dB\n", oldF95);
    std::printf ("        new (power domain, 1/2 oct, 0.208 s)  %6.2f dB\n", newF95);
    std::printf ("      bin-to-bin sawtooth, 95th percentile\n");
    std::printf ("        old %6.2f dB    new %6.2f dB\n", oldB95, newB95);

    // The absolute bound moved with the tuning: every display was asked to run
    // 25 % faster, and a release that is 25 % faster necessarily lets a little
    // more of the frame-to-frame scatter through (1.05 -> 1.28 dB).  What must
    // NOT change is the relationship to the old chain, so that is the assertion
    // with teeth: at least four times stiller.
    check (newF95 < 1.5, "steady noise moves the outline <1.5 dB a frame", newF95, 1.5);
    check (newF95 < oldF95 * 0.25, "and is at least four times as still as before", newF95, oldF95 * 0.25);
    check (newB95 < 1.2, "the bin-to-bin sawtooth is under 1.2 dB", newB95, 1.2);

    // ---- the fall, once the signal stops --------------------------------
    {
        Source s2;
        ovspec::SpectrumBallistics b;
        float v[128], o[128];
        for (int n = 0; n < 120; ++n) { s2.frame (v); b.advance (v, o, dt); }   // settled

        // average level before the silence
        double before = 0.0;
        for (int i = 20; i < 100; ++i) before += ovspec::normToDb (b.shown()[i]);
        before /= 80.0;

        // silence
        float zero[128] = {};
        double t = 0.0;
        std::vector<double> levels;
        for (int n = 0; n < 300; ++n)
        {
            b.advance (zero, o, dt);
            t += dt;
            double m = 0.0;
            for (int i = 20; i < 100; ++i) m += ovspec::normToDb (b.shown()[i]);
            levels.push_back (m / 80.0);
        }

        // the fall is exponential in power, so a straight line fit in dB over
        // the middle 40 dB is the rate — the same way ovb::Rms is verified.
        size_t a = 0, c = levels.size() - 1;
        while (a < levels.size() && levels[a] > before - 5.0) ++a;
        while (c > 0 && levels[c] < before - 45.0) --c;
        double st = 0, sy = 0, stt = 0, sty = 0;
        int n = 0;
        for (size_t k = a; k <= c; ++k, ++n)
        {
            const double tt = double (k) * dt, y = levels[k];
            st += tt; sy += y; stt += tt * tt; sty += tt * y;
        }
        const double slope = (n * sty - st * sy) / (n * stt - st * st);
        const double rate = -slope;
        std::printf ("\n      release rate           %6.2f dB/s   (4.343/tau)\n",
                     rate, b.releaseDbPerSec());
        check (std::abs (rate - b.releaseDbPerSec()) / b.releaseDbPerSec() < 0.10,
               "release is 4.343/tau dB/s", rate, b.releaseDbPerSec());
        check (rate < 60.0, "and it is slower than the old 283 dB/s", rate, 60.0);
        std::printf ("        after 1.0 s the outline is %.1f dB down, after 2.0 s %.1f dB\n",
                     before - levels[30], before - levels[60]);
    }

    // ---- attack still reaches the target -------------------------------
    {
        ovspec::SpectrumBallistics b;
        float v[128], o[128];
        float flat[128];
        for (int i = 0; i < 128; ++i) flat[i] = float (ovspec::dbToNorm (-60.0));
        for (int n = 0; n < 30; ++n) b.advance (flat, o, dt);      // settle at -60

        for (int i = 0; i < 128; ++i) v[i] = float (ovspec::dbToNorm (-20.0));
        int frames = 0;
        double t = 0.0;
        while (frames < 400)
        {
            b.advance (v, o, dt);
            t += dt; ++frames;
            if (ovspec::normToDb (b.shown()[64]) > -22.0) break;
        }
        std::printf ("\n      attack -60 -> -22 dB takes %.3f s\n", t);
        check (t < 0.30, "attack reaches a new level within 0.3 s", t, 0.30);
    }

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "PASS" : "FAIL",
                 failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
