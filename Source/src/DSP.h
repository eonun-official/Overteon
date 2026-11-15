#pragma once
// ============================================================
//  Supersaw Omega — core DSP primitives
//  PolyBLEP oscillators / analog-style ADSR / nonlinear TPT SVF
// ============================================================
#include <cmath>
#include <juce_core/juce_core.h>

namespace omega {

// Padé tanh approximation — cheap, smooth, bounded
inline float fastTanh (float x)
{
    if (x < -3.0f) return -1.0f;
    if (x >  3.0f) return  1.0f;
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// ------------------------------------------------------------
// Analog-style exponential ADSR (overshooting attack target,
// one-pole segments, like classic transistor envelope gens)
// ------------------------------------------------------------
class AnalogADSR
{
public:
    void setSampleRate (double sr)            { fs = (float) sr; recalc(); }
    void setParams (float a, float d, float s, float r)
    {
        if (a != at || d != de || s != su || r != re)
        { at = a; de = d; su = s; re = r; recalc(); }
    }
    void noteOn()        { state = State::attack; }
    void noteOff()       { if (state != State::idle) state = State::release; }
    void reset()         { state = State::idle; level = 0.0f; }
    bool isActive() const{ return state != State::idle; }

    inline float process()
    {
        switch (state)
        {
            case State::attack:
                level += (1.28f - level) * aC;          // overshoot => snappy
                if (level >= 1.0f) { level = 1.0f; state = State::decay; }
                break;
            case State::decay:
                level += (su - level) * dC;
                if (std::abs (level - su) < 1.0e-4f) state = State::sustain;
                break;
            case State::sustain:
                level = su;
                break;
            case State::release:
                level -= level * rC;
                if (level < 1.0e-4f) { level = 0.0f; state = State::idle; }
                break;
            case State::idle:
                break;
        }
        return level;
    }

private:
    enum class State { idle, attack, decay, sustain, release };
    State state = State::idle;
    float fs = 44100.0f;
    float at = 0.005f, de = 0.3f, su = 0.75f, re = 0.25f;
    float aC = 0.01f, dC = 0.01f, rC = 0.01f, level = 0.0f;

    float coef (float seconds) const
    {
        seconds = juce::jmax (0.001f, seconds);
        return 1.0f - std::exp (-3.0f / (seconds * fs));
    }
    void recalc() { aC = coef (at); dC = coef (de); rC = coef (re); }
};

// ------------------------------------------------------------
// PolyBLEP oscillator — band-limited saw/square, integrated
// triangle, pure sine. Per-unison-voice instance.
// ------------------------------------------------------------
struct PolyBlepOsc
{
    enum Wave { saw = 0, square = 1, triangle = 2, sine = 3, noise = 4 };

    void prepare (double sr)      { fs = (float) sr; }
    void resetPhase (float ph)
    {
        phase = ph - std::floor (ph);
        triState = 0.0f;
        rng = (0x9E3779B9u ^ (uint32_t) (ph * 1.0e9f)) | 1u;   // decorrelated seed
    }
    void setFreq (float hz)       { inc = juce::jlimit (0.0f, 0.45f, hz / fs); }

    inline float blep (float t) const
    {
        if (t < inc)               { t /= inc;            return t + t - t * t - 1.0f; }
        if (t > 1.0f - inc)        { t = (t - 1.0f) / inc; return t * t + t + t + 1.0f; }
        return 0.0f;
    }

    inline float process (int wave, float fm = 0.0f)
    {
        float out = 0.0f;
        switch (wave)
        {
            case saw:
                out = 2.0f * phase - 1.0f - blep (phase);
                break;
            case square:
            {
                out = phase < 0.5f ? 1.0f : -1.0f;
                out += blep (phase);
                float t2 = phase + 0.5f; if (t2 >= 1.0f) t2 -= 1.0f;
                out -= blep (t2);
                break;
            }
            case triangle:
            {
                float sq = phase < 0.5f ? 1.0f : -1.0f;
                sq += blep (phase);
                float t2 = phase + 0.5f; if (t2 >= 1.0f) t2 -= 1.0f;
                sq -= blep (t2);
                triState = triState * 0.9995f + sq * (4.0f * inc);   // leaky integrate
                out = triState;
                break;
            }
            case noise:
            {
                rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
                out = (float) (int32_t) rng * 4.6566129e-10f;
                break;
            }
            default:
                out = std::sin (phase * juce::MathConstants<float>::twoPi);
                break;
        }
        phase += inc + fm;
        phase -= std::floor (phase);
        return out;
    }

    float fs = 44100.0f, phase = 0.0f, inc = 0.0f, triState = 0.0f;
    uint32_t rng = 22222u;
};

// ------------------------------------------------------------
// TPT / ZDF state-variable filter (Simper form) with a gentle
// tanh nonlinearity on the band state — warm, self-oscillates
// at full resonance. Modes: 0 = LP, 1 = HP, 2 = BP.
// ------------------------------------------------------------
class TPTSVF
{
public:
    void prepare (double sr) { fs = (float) sr; reset(); }
    void reset()             { ic1 = ic2 = 0.0f; }

    void set (float cutoff, float res)
    {
        cutoff = juce::jlimit (20.0f, fs * 0.45f, cutoff);
        res    = juce::jlimit (0.0f, 1.0f, res);
        g  = std::tan (juce::MathConstants<float>::pi * cutoff / fs);
        k  = 2.0f - 1.99f * res;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    inline float process (float v0, int mode)
    {
        const float v3 = v0 - ic2;
        float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        v1 = fastTanh (v1 * 1.15f) * 0.8695652f;     // resonance-path warmth
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        if (mode == 0) return v2;                    // LP
        if (mode == 2) return v1;                    // BP
        return v0 - k * v1 - v2;                     // HP
    }

private:
    float fs = 44100.0f, g = 0.0f, k = 2.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
    float ic1 = 0.0f, ic2 = 0.0f;
};

// ------------------------------------------------------------
// RBJ cookbook biquad — master EQ bands (shelves + peak),
// plus closed-form magnitude response for the visualizer.
// ------------------------------------------------------------
struct Biquad
{
    // Coefficients, states and all trig math are DOUBLE precision.
    // Single-precision recursive filters put their cumulative rounding
    // noise near -120 dB (worst at LF where poles hug the unit circle);
    // double keeps the self-noise floor near -300 dB. The float process()
    // API is unchanged, processD() exposes the full-precision path.
    void reset() { x1 = x2 = y1 = y2 = 0.0; }
    void copyCoefsFrom (const Biquad& o)
    { b0 = o.b0; b1 = o.b1; b2 = o.b2; a1 = o.a1; a2 = o.a2; }

    void setLowpass (float fsIn, float fIn, float qIn)
    {
        const double fs = fsIn;
        const double w  = juce::MathConstants<double>::twoPi
                        * juce::jlimit (10.0, fs * 0.45, (double) fIn) / fs;
        const double cw = std::cos (w);
        const double al = std::sin (w) / (2.0 * juce::jmax (0.1, (double) qIn));
        const double a0 = 1.0 + al;
        b0 = (1.0 - cw) * 0.5 / a0;
        b1 = (1.0 - cw)       / a0;
        b2 = (1.0 - cw) * 0.5 / a0;
        a1 = -2.0 * cw        / a0;
        a2 = (1.0 - al)       / a0;
    }

    void setHighpass (float fsIn, float fIn, float qIn)
    {
        const double fs = fsIn;
        const double w  = juce::MathConstants<double>::twoPi
                        * juce::jlimit (10.0, fs * 0.45, (double) fIn) / fs;
        const double cw = std::cos (w);
        const double al = std::sin (w) / (2.0 * juce::jmax (0.1, (double) qIn));
        const double a0 = 1.0 + al;
        b0 =  (1.0 + cw) * 0.5 / a0;
        b1 = -(1.0 + cw)       / a0;
        b2 =  (1.0 + cw) * 0.5 / a0;
        a1 = -2.0 * cw         / a0;
        a2 = (1.0 - al)        / a0;
    }

    void setNotch (float fsIn, float fIn, float qIn)
    {
        const double fs = fsIn;
        const double w  = juce::MathConstants<double>::twoPi
                        * juce::jlimit (10.0, fs * 0.45, (double) fIn) / fs;
        const double cw = std::cos (w);
        const double al = std::sin (w) / (2.0 * juce::jmax (0.05, (double) qIn));
        const double a0 = 1.0 + al;
        b0 =  1.0      / a0;
        b1 = -2.0 * cw / a0;
        b2 =  1.0      / a0;
        a1 = -2.0 * cw / a0;
        a2 = (1.0 - al) / a0;
    }

    void setPeak (float fsIn, float fIn, float qIn, float dB)
    {
        const double fs = fsIn;
        const double A  = std::pow (10.0, (double) dB / 40.0);
        const double w  = juce::MathConstants<double>::twoPi
                        * juce::jlimit (10.0, fs * 0.45, (double) fIn) / fs;
        const double al = std::sin (w) / (2.0 * juce::jmax (0.1, (double) qIn));
        const double cw = std::cos (w);
        const double a0 = 1.0 + al / A;
        b0 = (1.0 + al * A) / a0;
        b1 = (-2.0 * cw)    / a0;
        b2 = (1.0 - al * A) / a0;
        a1 = (-2.0 * cw)    / a0;
        a2 = (1.0 - al / A) / a0;
    }

    void setLowShelf (float fsIn, float fIn, float qIn, float dB)
    {
        const double fs = fsIn;
        const double A  = std::pow (10.0, (double) dB / 40.0);
        const double w  = juce::MathConstants<double>::twoPi
                        * juce::jlimit (10.0, fs * 0.45, (double) fIn) / fs;
        const double cw = std::cos (w);
        const double al = std::sin (w) / (2.0 * juce::jmax (0.1, (double) qIn));
        const double sq = 2.0 * std::sqrt (A) * al;
        const double a0 = (A + 1.0) + (A - 1.0) * cw + sq;
        b0 =       A * ((A + 1.0) - (A - 1.0) * cw + sq) / a0;
        b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cw)      / a0;
        b2 =       A * ((A + 1.0) - (A - 1.0) * cw - sq) / a0;
        a1 =   -2.0 * ((A - 1.0) + (A + 1.0) * cw)       / a0;
        a2 =           ((A + 1.0) + (A - 1.0) * cw - sq) / a0;
    }

    void setHighShelf (float fsIn, float fIn, float qIn, float dB)
    {
        const double fs = fsIn;
        const double A  = std::pow (10.0, (double) dB / 40.0);
        const double w  = juce::MathConstants<double>::twoPi
                        * juce::jlimit (10.0, fs * 0.45, (double) fIn) / fs;
        const double cw = std::cos (w);
        const double al = std::sin (w) / (2.0 * juce::jmax (0.1, (double) qIn));
        const double sq = 2.0 * std::sqrt (A) * al;
        const double a0 = (A + 1.0) - (A - 1.0) * cw + sq;
        b0 =        A * ((A + 1.0) + (A - 1.0) * cw + sq) / a0;
        b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cw)      / a0;
        b2 =        A * ((A + 1.0) + (A - 1.0) * cw - sq) / a0;
        a1 =     2.0 * ((A - 1.0) - (A + 1.0) * cw)       / a0;
        a2 =            ((A + 1.0) - (A - 1.0) * cw - sq) / a0;
    }

    inline double processD (double in)
    {
        const double out = b0 * in + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = in;
        y2 = y1; y1 = out;
        return out;
    }

    inline float process (float in)
    {
        return (float) processD ((double) in);
    }

    float magDb (float wIn) const
    {
        const double w  = juce::jmax (1.0e-3f, wIn);
        const double cw = std::cos (w), c2 = std::cos (2.0 * w);
        const double num = b0*b0 + b1*b1 + b2*b2
                         + 2.0 * (b0*b1 + b1*b2) * cw + 2.0 * b0*b2 * c2;
        const double den = 1.0 + a1*a1 + a2*a2
                         + 2.0 * (a1 + a1*a2) * cw + 2.0 * a2 * c2;
        return (float) (10.0 * std::log10 (juce::jmax (num, 1.0e-16)
                                         / juce::jmax (den, 1.0e-16)));
    }

    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
};

} // namespace omega
