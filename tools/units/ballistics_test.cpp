// Headless proof that the meter interpolation is the curve it claims to be.
//
// The complaints this answers were all of the form "it does not move like a
// meter", which is impossible to settle by looking: the fall looked plausible
// at any single frame size.  So every property the ballistics claim is checked
// numerically here, and the old curve is printed beside the new one.
#include "OvBallistics.h"

#include <cstdio>
#include <cmath>
#include <random>
#include <vector>

static int failures = 0;
static void check (bool ok, const char* what, double got = 0.0, double want = 0.0)
{
    if (! ok) { ++failures; std::printf ("FAIL  %-58s got %.6f want %.6f\n", what, got, want); }
    else      std::printf ("ok    %-58s %.6f\n", what, got);
}

int main()
{
    // ---- smootherstep -----------------------------------------------------
    check (std::abs (ovb::smootherstep (0.0)) < 1e-12, "smootherstep(0) == 0", ovb::smootherstep (0.0));
    check (std::abs (ovb::smootherstep (1.0) - 1.0) < 1e-12, "smootherstep(1) == 1", ovb::smootherstep (1.0));
    check (std::abs (ovb::smootherstep (0.5) - 0.5) < 1e-12, "smootherstep(0.5) == 0.5", ovb::smootherstep (0.5));

    double worstSym = 0.0, worstMono = 0.0;
    for (int i = 0; i <= 1000; ++i)
    {
        const double f = i / 1000.0;
        worstSym = std::max (worstSym, std::abs (ovb::smootherstep (f)
                                               + ovb::smootherstep (1.0 - f) - 1.0));
        if (i > 0)
            worstMono = std::min (worstMono,
                                  ovb::smootherstep (f) - ovb::smootherstep (f - 0.001));
    }
    check (worstSym < 1e-9, "smootherstep is symmetric: S(f)+S(1-f) == 1", worstSym);
    check (worstMono >= 0.0, "smootherstep is monotone", worstMono);

    // C2 at both ends: the slope and the second derivative vanish.
    const double h = 1e-5;
    const double s0  = (ovb::smootherstep (h) - ovb::smootherstep (0.0)) / h;
    const double s1  = (ovb::smootherstep (1.0) - ovb::smootherstep (1.0 - h)) / h;
    const double s0b = ovb::smootherstepSlope (0.0);
    const double s1b = ovb::smootherstepSlope (1.0);
    check (std::abs (s0) < 1e-6 && std::abs (s1) < 1e-6,
           "smootherstep slope is 0 at both ends", std::max (std::abs (s0), std::abs (s1)));
    check (std::abs (s0b) < 1e-12 && std::abs (s1b) < 1e-12,
           "smootherstep analytic slope 0 at both ends", std::max (std::abs (s0b), std::abs (s1b)));
    // smoothstep, for the record: its slope at the ends is also 0, so the
    // visible difference is the SECOND derivative.  Measured below.
    auto smoothstep = [] (double f) { f = std::clamp (f, 0.0, 1.0); return f * f * (3.0 - 2.0 * f); };
    auto secondDiff = [] (auto fn, double f)
    {
        const double e = 1e-4;
        return (fn (f + e) - 2.0 * fn (f) + fn (f - e)) / (e * e);
    };
    std::printf ("      S''(0) smootherstep %.6f   smoothstep %.6f\n",
                 secondDiff (ovb::smootherstep, 0.0), secondDiff (smoothstep, 0.0));

    // ---- Catmull-Rom ------------------------------------------------------
    check (std::abs (ovb::catmullRom (1, 5, 9, 13, 0.0) - 5.0) < 1e-12,
           "catmullRom starts at p1", ovb::catmullRom (1, 5, 9, 13, 0.0));
    check (std::abs (ovb::catmullRom (1, 5, 9, 13, 1.0) - 9.0) < 1e-12,
           "catmullRom ends at p2", ovb::catmullRom (1, 5, 9, 13, 1.0));

    // A straight line must stay a straight line: CR reproduces linear data.
    {
        double worst = 0.0;
        for (int i = 0; i <= 100; ++i)
        {
            const double f = i / 100.0;
            worst = std::max (worst, std::abs (ovb::catmullRom (2, 5, 8, 11, f) - (5.0 + 3.0 * f)));
        }
        check (worst < 1e-12, "catmullRom reproduces a straight line", worst);
    }

    // The limited form must never invent a value the data does not contain.
    {
        std::mt19937 rng (1234);
        std::uniform_real_distribution<double> u (0.0, 1.0);
        double worst = 0.0;
        for (int n = 0; n < 2000; ++n)
        {
            const double p0 = u (rng), p1 = u (rng), p2 = u (rng), p3 = u (rng);
            const double lo = std::min (p1, p2), hi = std::max (p1, p2);
            for (int i = 0; i <= 20; ++i)
            {
                const double v = ovb::catmullRomLimited (p0, p1, p2, p3, i / 20.0);
                worst = std::max (worst, std::max (lo - v, v - hi));
            }
        }
        check (worst <= 1e-12, "catmullRomLimited never overshoots its segment", worst);
    }

    // Plain Catmull-Rom, for contrast: it does overshoot (which is why the
    // limited form is the one the meter uses).
    {
        const double v = ovb::catmullRom (0.0, 0.0, 1.0, 1.0, 0.5);
        std::printf ("      plain CR overshoot check  cr(0,0,1,1,.5)=%.4f (in [0,1]=%s)\n",
                     v, (v >= 0.0 && v <= 1.0) ? "yes" : "NO");
        const double w = ovb::catmullRom (0.0, 0.0, 1.0, 0.0, 0.5);
        std::printf ("      plain CR with a reversing neighbour cr(0,0,1,0,.5)=%.4f "
                     "(outside [0,1] = %s)\n", w, (w < 0.0) ? "overshoots below" : "ok");
    }

    // ---- the fall ---------------------------------------------------------
    // 60 dB at 120 dB/s, started from full scale.
    constexpr double kSpan = 60.0, kRate = 120.0;

    for (double fps : { 15.0, 30.0, 60.0, 120.0 })
    {
        ovb::Fall f; f.reset (0.0);
        const double dt = 1.0 / fps;
        int steps = 0;
        double t = 0.0, prev = f.shown;
        bool monotone = true, overshot = false;
        while (f.shown > -kSpan && steps < 100000)
        {
            f.advance (-kSpan, dt, kRate);
            if (f.shown > prev + 1e-12) monotone = false;
            if (f.shown < -kSpan)       overshot = true;
            prev = f.shown;
            t += dt; ++steps;
        }
        std::printf ("      fall at %5.0f fps: %5.3f s, %4d steps%s\n",
                     fps, t, steps, (f.shown == -kSpan) ? " (landed)" : " (DID NOT LAND)");
        char label[96];
        std::snprintf (label, sizeof label, "fall at %.0f fps is monotone", fps);
        check (monotone, label);
        std::snprintf (label, sizeof label, "fall at %.0f fps never overshoots", fps);
        check (! overshot, label);
        std::snprintf (label, sizeof label, "fall at %.0f fps lands exactly", fps);
        check (f.shown == -kSpan, label, f.shown, -kSpan);
        // 60 dB at 120 dB/s is 0.5 s; the eased ends cost less than one frame.
        std::snprintf (label, sizeof label, "fall at %.0f fps takes 0.5 s +- 0.08", fps);
        check (std::abs (t - 0.5) < 0.08, label, t, 0.5);
    }

    // Rate independence: the two extremes must agree on WHEN, not just on how.
    {
        auto timeTo = [&] (double fps)
        {
            ovb::Fall f; f.reset (0.0);
            const double dt = 1.0 / fps;
            double t = 0.0;
            while (f.shown > -kSpan && t < 10.0) { f.advance (-kSpan, dt, kRate); t += dt; }
            return t;
        };
        const double a = timeTo (15.0), b = timeTo (120.0);
        check (std::abs (a - b) < 0.08, "fall time does not depend on the frame rate", a - b, 0.0);
    }

    // Attack is instant, including from silence.
    {
        ovb::Fall f; f.reset (-60.0);
        f.advance (0.0, 1.0 / 60.0, kRate);
        check (f.shown == 0.0, "attack is instant", f.shown, 0.0);
    }

    // ---- the shape, printed, old vs new -----------------------------------
    // The old release was  step = 1 - (target/start)^(1/n_frames)  in the 0..1
    // domain.  Its speed is highest on the FIRST frame of a fall and its tail
    // is infinite.  Printed as dB per frame at 60 fps so the two curves can be
    // compared without a screenshot.
    std::printf ("\n      speed per frame, 60 fps, falling 60 dB (dB/frame)\n");
    std::printf ("      %6s %10s %10s\n", "frame", "old", "new");
    {
        const double dt = 1.0 / 60.0;
        const double nFrames = 60.0 * 0.30;                 // the old 0.30 s
        double oldShown = 0.0;
        ovb::Fall f; f.reset (0.0);
        for (int i = 1; i <= 8; ++i)
        {
            const double startN = (oldShown + 60.0) / 60.0;
            const double goalN  = 0.0;
            const double ratio  = std::clamp (goalN / std::max (1e-9, startN), 0.0, 1.0);
            const double st     = std::clamp (1.0 - std::pow (ratio, 1.0 / nFrames), 0.0, 1.0);
            const double oldNext = oldShown - (oldShown + 60.0) * st;
            const double oldStep = oldShown - oldNext;
            oldShown = oldNext;

            const double before = f.shown;
            f.advance (-60.0, dt, 200.0);           // 60 dB in 0.30 s
            std::printf ("      %6d %10.3f %10.3f\n", i, oldStep, before - f.shown);
        }
    }

    // ---- the RMS detector --------------------------------------------------
    // The meters are RMS now, and "RMS" is a claim that can be checked without
    // a screenshot: a sine of amplitude A must read A/sqrt(2) whatever its
    // frequency, a square must read A, and once the signal stops the reading
    // must fall at 4.343/tau dB per second — at ANY block size and ANY sample
    // rate, which is the bug that made the old peak meter's fall speed depend
    // on the host's buffer.

    auto dbOf = [] (double a) { return 20.0 * std::log10 (std::max (a, 1e-9)); };

    {
        // 0.5 full-scale sine: -6.02 dB peak, -9.03 dB RMS.
        std::vector<double> sine (48000);
        for (size_t i = 0; i < sine.size(); ++i)
            sine[i] = 0.5 * std::sin (2.0 * M_PI * 1000.0 * double (i) / 48000.0);

        ovb::Rms r; r.tau = 0.3;
        // 2 s of signal is >6 tau: settled.
        for (int k = 0; k < 94; ++k) r.process (sine.data(), 1024, 48000.0);
        const double got = dbOf (r.amplitude());
        check (std::abs (got - (-9.03)) < 0.05, "RMS of a 0.5 sine reads -9.03 dBFS", got, -9.03);
        std::printf ("      (a PEAK meter would have read %.2f dBFS here)\n", dbOf (0.5));

        // 0.5 full-scale square: 0 dB peak, -6.02 dB RMS.
        std::vector<double> sq (1024);
        for (size_t i = 0; i < sq.size(); ++i) sq[i] = (i % 2) ? 0.5 : -0.5;
        ovb::Rms r2; r2.tau = 0.3;
        for (int k = 0; k < 94; ++k) r2.process (sq.data(), 1024, 48000.0);
        const double g2 = dbOf (r2.amplitude());
        check (std::abs (g2 - (-6.02)) < 0.05, "RMS of a 0.5 square reads -6.02 dBFS", g2, -6.02);
    }

    // The fall: 4.343/tau dB/s, and NOT a function of the block size.
    //
    // Fitted rather than measured by a threshold crossing: the detector updates
    // once per BLOCK, so with a 4096-sample buffer each published value is 85 ms
    // old and a "when does it cross -20 dB" question is quantised to that step
    // (which is how 14.4 dB/s came out as 13.8 on the first attempt).  A least
    // squares straight line through log(mean square) vs time has no such bias —
    // the decay IS a straight line in dB by construction.
    {
        auto fittedRate = [] (double fs, int block, double tau)
        {
            ovb::Rms r; r.tau = tau;
            std::vector<double> sine (size_t (block), 0.0);
            for (size_t i = 0; i < sine.size(); ++i)
                sine[i] = 0.5 * std::sin (2.0 * M_PI * 1000.0 * double (i) / fs);
            for (int k = 0; k < int (2.0 * fs / block); ++k) r.process (sine.data(), block, fs);

            std::vector<double> zero (size_t (block), 0.0);
            const double dt = double (block) / fs;
            double t = 0.0, st = 0.0, sd = 0.0, stt = 0.0, std_ = 0.0;
            int n = 0;
            for (int k = 0; k < int (1.5 / dt); ++k)
            {
                r.process (zero.data(), block, fs);
                t += dt;
                const double d = r.db();
                st += t; sd += d; stt += t * t; std_ += t * d; ++n;
            }
            const double slope = (n * std_ - st * sd) / (n * stt - st * st);   // dB/s
            return -slope;
        };

        const double want = 4.343 / 0.3;
        for (double fs : { 44100.0, 48000.0, 96000.0 })
            for (int block : { 64, 256, 1024, 4096 })
            {
                const double rate = fittedRate (fs, block, 0.3);
                char label[96];
                std::snprintf (label, sizeof label,
                               "RMS fall %.0f Hz / %4d samples = %.2f dB/s", fs, block, rate);
                check (std::abs (rate - want) / want < 0.01, label, rate, want);
            }
    }

    // The harmonic ratio's smoothing must be a time constant, not a per-block
    // coefficient: 0.15 per block was 131 ms at 1024 and 33 ms at 256.
    {
        auto fittedTau = [] (int block, double fs)
        {
            double v = 1.0, t = 0.0;
            double st = 0.0, sy = 0.0, stt = 0.0, sty = 0.0;
            int n = 0;
            for (int k = 0; k < int (1.5 * fs / block); ++k)
            {
                v -= ovb::onePoleCoef (double (block) / fs, 0.131) * v;
                t += double (block) / fs;
                const double y = std::log (v);
                st += t; sy += y; stt += t * t; sty += t * y; ++n;
            }
            const double slope = (n * sty - st * sy) / (n * stt - st * st);
            return -1.0 / slope;
        };
        const double a = fittedTau (64, 48000.0), b = fittedTau (4096, 48000.0);
        check (std::abs (a - b) / a < 0.01, "harm smoothing does not depend on block size", a, b);
        check (std::abs (a - 0.131) / 0.131 < 0.01, "harm smoothing is the 131 ms it replaced", a, 0.131);
    }

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "PASS" : "FAIL",
                 failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
