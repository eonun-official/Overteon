#pragma once
// ============================================================
//  OnyxTEON Qt/QML UI — mock data source.
//
//  Mirrors the parameter layout of src/FxProcessor.cpp exactly (same ids,
//  ranges, defaults) and animates meters + spectrum, so the QML front end
//  can be developed and screenshotted without a plugin host.
//  Used by the `overteon-qml-preview` target only.
// ============================================================
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <complex>
#include <mutex>
#include <string>
#include <vector>

#include "OvSource.h"

class OvMockSource : public OvSource
{
public:
    // Set from the command line: freezes meters/spectrum so screenshot diffs
    // only show the effect of the interaction under test.
    static bool frozen;
    /// ONYXTEON_NOSIGNAL: generate silence, so every meter and every spectrum
    /// bar is empty.  The bars are drawn on top of the plots by the same
    /// canvas the glass samples, so with the mock running there is no region
    /// of the plot where the glass can be looked at on its own.
    static bool silent;

    OvMockSource() { buildTable(); }

    int count() const override { return (int) rows.size(); }

    OvParamSpec spec (int i) const override
    {
        const auto& r = rows[(size_t) i];
        OvParamSpec s;
        s.id = r.id;
        s.label = r.label;
        s.minValue = r.lo;
        s.maxValue = r.hi;
        s.defaultNorm = r.defNorm;
        s.isBool = r.isBool;
        for (const auto& c : r.choices) s.choices.push_back (c);
        return s;
    }

    double getNorm (int i) const override { return norms[(size_t) i]; }

    void setNorm (int i, double v) override
    {
        norms[(size_t) i] = std::clamp (v, 0.0, 1.0);
    }

    double normForValue (int i, double v) const override
    {
        const auto& r = rows[(size_t) i];
        if (! r.choices.empty())
        {
            const double n = (double) r.choices.size() - 1.0;
            return n > 0.0 ? std::clamp (v / n, 0.0, 1.0) : 0.0;
        }
        // JUCE convertTo0to1: pow(proportion, skew)
        const double lin = (v - r.lo) / (r.hi - r.lo);
        return std::clamp (std::pow (std::clamp (lin, 0.0, 1.0), r.skew), 0.0, 1.0);
    }

    double valueForNorm (int i, double n) const override
    {
        const auto& r = rows[(size_t) i];
        n = std::clamp (n, 0.0, 1.0);
        if (! r.choices.empty())
        {
            const double last = (double) r.choices.size() - 1.0;
            return std::clamp (std::round (n * last), 0.0, last);
        }
        if (r.isBool) return n > 0.5 ? 1.0 : 0.0;

        // JUCE convertFrom0to1: start + span * pow(proportion, 1/skew)
        const double prop = (r.skew != 1.0 && n > 0.0) ? std::exp (std::log (n) / r.skew) : n;
        return r.lo + (r.hi - r.lo) * prop;
    }

    std::string text (int i) const override
    {
        std::string s;
        textFor (i, s);
        return s;
    }

    void beginGesture (int) override {}
    void endGesture   (int) override {}

    void applyInit() override
    {
        for (size_t i = 0; i < rows.size(); ++i) norms[i] = rows[i].defNorm;
    }

    std::string presetListJson() override
    {
        return R"({"Factory":["default","Air Lift","Drum Glue","Vocal Sheen"],"User":[]})";
    }

    bool loadPresetNamed (const std::string&, const std::string&) override { return true; }
    void openLoadDialog() override {}
    void openSaveDialog() override {}

    OvMeters meters() const override
    {
        renderTo (frozen ? 1.0 : now());

        OvMeters m;
        // Peak of the last block, not a fabricated number: the bar length is
        // driven by the samples that were actually generated.
        auto peakDb = [&] (int offset)
        {
            float pk = 0.0f;
            for (int i = 0; i < kBlock; ++i)
            {
                const int idx = (m_ringPos - kBlock + i + offset + kFft * 2) % kFft;
                pk = std::max (pk, std::abs (m_ring[(size_t) idx]));
            }
            return 20.0f * std::log10 (std::max (pk, 1e-6f));
        };

        m.inL  = peakDb (0);
        m.inR  = peakDb (0);        // mono mock: L and R carry the same signal
        m.outL = peakDb (0);
        m.outR = peakDb (0);

        // Stand-in for the plug-in's harmonic/dry ratio: the real one comes out
        // of the DSP, which the preview does not link.  Swept with the same LFO
        // as the signal so the ring ramps along with the level.
        //
        // The LFO is evaluated from the absolute time, exactly as the page's
        // mock does.  Reading the sample-phase LFO instead made the ring
        // disagree with the page at any pinned instant: the generator primes
        // its ring from t - 2048/48000, so at t = 1.0 its phase has advanced
        // 0.011 of a cycle while the page's has advanced 0.25, and the ring
        // showed amber where the page showed the hot red of a 100 % reading.
        const double tt = frozen ? 1.0 : now ();
        const double lfoAbs = 0.5 + 0.5 * std::sin (2.0 * kPi * kLfoHz * tt);
        m.harm = (float) (-40.0 + 40.0 * lfoAbs);
        return m;
    }

    void spectrum (float* dst, int n) override
    {
        renderTo (frozen ? 1.0 : now());

        // A real FFT of the generated audio, mapped to log bins exactly as
        // OvJuceSource::spectrum() does, so the spectrum shows the square
        // wave's own harmonic series (60 Hz, 180, 300, 420 ...) instead of a
        // drawn-on curve.
        for (int i = 0; i < kFft; ++i)
        {
            const int idx = (m_ringPos + i) % kFft;
            const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * i / (kFft - 1));  // Hann
            m_fft[(size_t) i] = std::complex<float> ((float) (m_ring[(size_t) idx] * w), 0.0f);
        }
        fft (m_fft);

        const float fsr = (float) sampleRate();
        for (int i = 0; i < n; ++i)
        {
            const float frac = (float) i / (float) std::max (1, n - 1);
            const float fr   = 20.0f * std::pow (1000.0f, frac);      // 20 Hz .. 20 kHz
            // Bin 0 is DC: with a Hann window it holds the window sum, which
            // swings as the waveform slides through the buffer.  That is what
            // made the left edge of the spectrum twitch — 20 Hz is below one
            // bin of a 2048-point FFT at 48 kHz, so the lowest real bin is 1.
            const int   bin  = std::clamp ((int) (fr / fsr * (float) kFft), 1, kFft / 2);

            const float re = m_fft[(size_t) bin].real();
            const float im = m_fft[(size_t) bin].imag();
            const float mag = 2.0f * std::sqrt (re * re + im * im) / (float) kFft;
            const float db  = 20.0f * std::log10 (std::max (mag, 1e-7f));
            float v = std::clamp ((db + 85.0f) / 85.0f, 0.0f, 1.0f);

            // The OUTPUT tap carries what the processor did to the signal.
            // The preview does not link the DSP, so this stands in for it: a
            // broadband tilt, as if the Post EQ lifted the top and the
            // processor's drive thickened the low end.  It only has to be
            // visibly the same audio and a different picture, which is what the
            // two glow bars and the two spectrum fills are for.
            const double f01 = (double) i / std::max (1.0, (double) (n - 1));
            const double tilt = 1.0 + 0.55 * (f01 - 0.35);
            v = (float) std::clamp ((double) v * tilt, 0.0, 1.0);
            dst[i] = v;
        }
    }

    void spectrumIn (float* dst, int n) override
    {
        spectrum (dst, n);
    }

    double sampleRate() const override { return 48000.0; }
    int driveModeIndex() const override { return 1; }

private:
    // ----------------------------------------------------------------
    //  The mock SIGNAL.
    //
    //  A 60 Hz square-wave bass whose amplitude is swept by a 0.25 Hz LFO,
    //  generated as real samples and then analysed — the meters read the peak
    //  of what was generated and the spectrum is an FFT of it.  Nothing is
    //  drawn from a formula, so the spectrum shows the square wave's actual
    //  harmonic series and the bars respond to the same signal a real input
    //  would give them.
    //
    //  Why a square: it steps between two exact levels instead of gliding, so
    //  the peak-hold trail and its cap dot can be seen holding on a value and
    //  then jumping, which is exactly what they do with real program material.
    // ----------------------------------------------------------------
    static constexpr int    kFft   = 2048;
    static constexpr int    kBlock = 1024;
    static constexpr double kPi    = 3.14159265358979323846;
    static constexpr double kSigHz = 60.0;      // the bass
    static constexpr double kLfoHz = 0.25;      // 4 s sweep

    mutable std::vector<float>            m_ring = std::vector<float> (kFft, 0.0f);
    mutable int                           m_ringPos = 0;
    mutable double                        m_phase = 0.0;   // sample phase, 0..1
    mutable double                        m_lfoPhase = 0.0;
    mutable double                        m_lfo = 0.5;
    mutable double                        m_generatedUntil = -1.0;
    mutable bool                          m_wasSilent = false;
    mutable std::vector<std::complex<float>> m_fft = std::vector<std::complex<float>> (kFft);

    /// Generate every sample between the last call and `t` into the ring
    /// buffer.  Called from both meters() and spectrum(), so whichever the
    /// engine polls first advances the signal and the other sees the result.
    void renderTo (double t) const
    {
        // First call, or the clock went backwards (a new --freeze / onyxteon_t
        // pin): fill the whole ring so there is always a full block to measure.
        // Returning early on the first call left every sample at zero, which
        // read as -120 dB — and with the clock pinned (a comparison run) it
        // never advanced, so the bars stayed at nothing for the whole run.
        // A change of `silent` must also restart: with the clock pinned
        // (--freeze) renderTo returns early on every call, so a ring filled
        // before the flag was set keeps its old samples forever and
        // ONYXTEON_NOSIGNAL appeared to do nothing.
        bool restart = (m_generatedUntil < 0.0) || (t < m_generatedUntil);
        if (silent != m_wasSilent) { m_wasSilent = silent; restart = true; }
        if (restart)
        {
            m_ring.assign (kFft, 0.0f);
            m_ringPos = 0;
            m_phase = 0.0;
            m_lfoPhase = 0.0;
            m_generatedUntil = t - double (kFft) / sampleRate();
        }
        if (t <= m_generatedUntil) return;

        const double dt = std::min (t - m_generatedUntil, 0.25);   // cap a stall
        m_generatedUntil = t;

        const double fs = sampleRate();
        const int n = std::min (int (dt * fs), kFft);
        const double dPhase = kSigHz / fs;
        const double dLfo   = kLfoHz / fs;

        for (int i = 0; i < n; ++i)
        {
            m_phase += dPhase;
            if (m_phase >= 1.0) m_phase -= 1.0;
            m_lfoPhase += dLfo;
            if (m_lfoPhase >= 1.0) m_lfoPhase -= 1.0;

            m_lfo = 0.5 + 0.5 * std::sin (2.0 * kPi * m_lfoPhase);

            // amplitude in the audio domain: -42 dB .. -6 dB peak
            const double db   = -42.0 + 36.0 * m_lfo;
            const double amp  = std::pow (10.0, db / 20.0);
            const double sq   = m_phase < 0.5 ? 1.0 : -1.0;    // 50 % duty square
            m_ring[(size_t) m_ringPos] = silent ? 0.0f : (float) (amp * sq);
            m_ringPos = (m_ringPos + 1) % kFft;
        }
    }

    /// In-place iterative radix-2 FFT (Cooley-Tukey).
    static void fft (std::vector<std::complex<float>>& a)
    {
        const int n = (int) a.size();
        for (int i = 1, j = 0; i < n; ++i)
        {
            int bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap (a[(size_t) i], a[(size_t) j]);
        }
        for (int len = 2; len <= n; len <<= 1)
        {
            const double ang = -2.0 * kPi / len;
            const std::complex<float> wl (float (std::cos (ang)), float (std::sin (ang)));
            for (int i = 0; i < n; i += len)
            {
                std::complex<float> w (1.0f, 0.0f);
                for (int k = 0; k < len / 2; ++k)
                {
                    const auto u = a[(size_t) (i + k)];
                    const auto v = a[(size_t) (i + k + len / 2)] * w;
                    a[(size_t) (i + k)] = u + v;
                    a[(size_t) (i + k + len / 2)] = u - v;
                    w *= wl;
                }
            }
        }
    }

    struct Row
    {
        std::string id, label;
        double lo = 0.0, hi = 1.0, skew = 1.0, defNorm = 0.0;
        std::vector<std::string> choices;
        bool isBool = false;
    };

    // Wall-clock seconds, NOT std::clock(): that returns process CPU time,
    // so the animation speed depended on how much CPU the process happened to
    // use.  On a real GPU (1.5 % cpu) "time" advanced ~60x slower than reality
    // and the UI looked frozen; on a software rasteriser (120 % cpu) it ran
    // faster than real time and reported 125 fps.  Same build, same code.
    /// Time used by meters()/spectrum().  --freeze pins it to 1.0, matching the
    /// page's ?freeze=1; ONYXTEON_T=<s> pins it anywhere so a comparison can
    /// pick a moment where the bar is mid-scale.
    static double now()
    {
        if (const char* pin = std::getenv ("ONYXTEON_T"))
            return std::atof (pin);
        using clock_type = std::chrono::steady_clock;
        static const auto t0 = clock_type::now();
        return std::chrono::duration<double> (clock_type::now() - t0).count();
    }

    static double skewFromCentre (double lo, double hi, double centre)
    {
        const double lin = (centre - lo) / (hi - lo);
        return std::log (0.5) / std::log (lin);
    }

    static double normFromValue (double lo, double hi, double skew, double v)
    {
        const double lin = (v - lo) / (hi - lo);
        return std::clamp (std::pow (std::clamp (lin, 0.0, 1.0), skew), 0.0, 1.0);
    }

    void add (const std::string& id, const std::string& label, double lo, double hi,
              double skew, double defValue, std::vector<std::string> choices = {},
              bool isBool = false)
    {
        Row r;
        r.id = id;
        r.label = label;
        r.lo = lo; r.hi = hi; r.skew = skew;
        r.choices = std::move (choices);
        r.isBool = isBool;
        if (! r.choices.empty())
            r.defNorm = r.choices.size() > 1 ? defValue / (double) (r.choices.size() - 1) : 0.0;
        else
            r.defNorm = normFromValue (lo, hi, skew, defValue);
        rows.push_back (std::move (r));
        norms.push_back (rows.back().defNorm);
    }

    void buildTable()
    {
        const double driveSkew = skewFromCentre (0.0, 10.0, 2.0);
        const double freqSkew  = skewFromCentre (5.0, 20000.0, 800.0);
        const double qSkew     = skewFromCentre (0.1, 6.0, 0.71);
        const double airSkew   = skewFromCentre (2000.0, 20000.0, 9000.0);

        add ("drive",      "Drive",           0.0, 10.0, driveSkew, 0.0);
        add ("mix",        "Harmonics Mix",   0.0, 1.0, 1.0, 1.0);
        add ("driveType",  "Drive Type",      0, 2, 1.0, 0, { "Tanh", "Tube", "Tape" });
        add ("driveMode",  "Drive Mode",      0, 2, 1.0, 1, { "Classic", "Harmonic", "Schranz" });
        add ("osFactor",   "Oversampling",    0, 4, 1.0, 1, { "2x", "4x", "8x", "16x", "32x" });
        add ("inGain",     "Input Gain",    -24.0, 24.0, 1.0, 0.0);
        add ("outGain",    "Output Gain",   -24.0, 24.0, 1.0, 0.0);
        add ("deEmph",     "De-Emphasis",     0.0, 1.0, 1.0, 1.0);
        add ("harmGain",   "Harmonics Gain", -12.0, 24.0, 1.0, 0.0);
        add ("airProtect", "Air Protect",  2000.0, 20000.0, airSkew, 20000.0);
        add ("evenOdd",    "Even / Odd",     -1.0, 1.0, 1.0, 0.0);
        add ("msMode",     "Stereo Mode",     0, 3, 1.0, 0, { "Stereo", "Mid", "Side", "M/S" });
        add ("autoGain",   "Auto Gain",       0, 1, 1.0, 0, {}, true);
        add ("harmSolo",   "Solo Harmonics",  0, 1, 1.0, 0, {}, true);

        const double defF[5] = { 100.0, 250.0, 1200.0, 4000.0, 8500.0 };
        const int    defT[5] = { 2, 0, 0, 0, 3 };
        const std::vector<std::string> eqTypes {
            "Bell", "Notch", "Low Shelf", "High Shelf",
            "Low Cut 12", "Low Cut 24", "High Cut 12", "High Cut 24" };

        for (const char* pre : { "sc", "pq" })
        {
            const bool sc = (std::string (pre) == "sc");
            for (int b = 0; b < 5; ++b)
            {
                const std::string id (pre);
                const std::string nm = id + std::to_string (b + 1);
                const bool bump = (! sc && b == 3);
                add (nm + "Freq", "EQ " + nm + " Freq", 5.0, 20000.0, freqSkew,
                     bump ? 10000.0 : defF[b]);
                add (nm + "Gain", "EQ " + nm + " Gain", -12.0, 12.0, 1.0, 0.0);
                add (nm + "Q",    "EQ " + nm + " Q", 0.1, 6.0, qSkew, bump ? 0.9 : 0.707);
                add (nm + "Type", "EQ " + nm + " Type", 0, 7, 1.0, (double) defT[b], eqTypes);
            }
        }

        add ("scRange", "SC EQ Range", 0, 3, 1.0, 0,
             { "+/-12 dB", "+/-24 dB", "+/-48 dB", "+/-90 dB" });
        add ("pqRange", "Post EQ Range", 0, 3, 1.0, 2,
             { "+/-3 dB", "+/-6 dB", "+/-12 dB", "+/-30 dB" });
        add ("protectMode", "Limiter Mode", 0, 3, 1.0, 2,
             { "Off", "Clip", "Modern", "True Peak" });
        add ("limiterOs", "Limiter Oversampling", 0, 3, 1.0, 1,
             { "2x", "4x", "8x", "16x" });
    }

    void textFor (int i, std::string& out) const
    {
        const auto& r = rows[(size_t) i];
        const double n = norms[(size_t) i];
        char buf[64];
        if (! r.choices.empty() || r.isBool) { out.clear(); return; }

        const double v = r.lo + (r.hi - r.lo) * n;
        if (r.id == "mix" || r.id == "deEmph")
            std::snprintf (buf, sizeof buf, "%d%%", (int) std::lround (v * 100.0));
        else if (r.id == "drive")
            std::snprintf (buf, sizeof buf, "%.2f", v);
        else if (r.id == "evenOdd")
        {
            if (std::abs (v) < 0.05) std::snprintf (buf, sizeof buf, "BAL");
            else if (v < 0) std::snprintf (buf, sizeof buf, "E %d", (int) std::lround (-v * 100.0));
            else std::snprintf (buf, sizeof buf, "O %d", (int) std::lround (v * 100.0));
        }
        else if (r.id == "airProtect")
        {
            if (v >= 19500.0) std::snprintf (buf, sizeof buf, "OFF");
            else if (v >= 1000.0) std::snprintf (buf, sizeof buf, "%.1fk", v / 1000.0);
            else std::snprintf (buf, sizeof buf, "%d", (int) std::lround (v));
        }
        else
            std::snprintf (buf, sizeof buf, "%+.1f", v);
        out = buf;
    }

    std::vector<Row> rows;
    std::vector<double> norms;
};

inline bool OvMockSource::frozen = false;
inline bool OvMockSource::silent = false;
