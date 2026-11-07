#pragma once
// ============================================================
//  RBJ biquad magnitude response.
//
//  One implementation, several users:
//    OvEqView       draws the EQ curve from it
//    OvJackSource   shapes the "output" spectrum with it
//
//  They MUST agree: the glow bars show the input spectrum at the top and the
//  output spectrum at the bottom, and the output is the input shaped by exactly
//  the curve drawn above it.  Two copies of this maths would drift and the bars
//  would stop lining up with the curve, which is the whole point of having them.
//
//  Coefficients are Robert Bristow-Johnson's Audio EQ Cookbook, the same set the
//  DSP core uses.  The type table is shared with the DSP and the WebView editor:
//      0 Bell  1 Notch  2 LowShelf  3 HighShelf
//      4 LC12  5 LC24   6 HC12      7 HC24
// ============================================================
#include <algorithm>
#include <cmath>

namespace ovbq
{

struct Coefs { double b0, b1, b2, a1, a2; };

inline Coefs rbj (int type, double fs, double fIn, double qIn, double dB)
{
    const double w  = 2.0 * M_PI * std::clamp (fIn, 5.0, fs * 0.45) / fs;
    const double cw = std::cos (w), sw = std::sin (w);
    const double al = sw / (2.0 * std::max (0.1, qIn));
    Coefs c {};

    switch (type)
    {
        case 0: // peak
        {
            const double A  = std::pow (10.0, dB / 40.0);
            const double a0 = 1.0 + al / A;
            c = { (1.0 + al * A) / a0, (-2.0 * cw) / a0, (1.0 - al * A) / a0,
                  (-2.0 * cw) / a0, (1.0 - al / A) / a0 };
            break;
        }
        case 1: // notch
        {
            const double al2 = sw / (2.0 * std::max (0.05, qIn));
            const double a0  = 1.0 + al2;
            c = { 1.0 / a0, -2.0 * cw / a0, 1.0 / a0, -2.0 * cw / a0, (1.0 - al2) / a0 };
            break;
        }
        case 2: // low shelf
        {
            const double A  = std::pow (10.0, dB / 40.0);
            const double sq = 2.0 * std::sqrt (A) * al;
            const double a0 = (A + 1.0) + (A - 1.0) * cw + sq;
            c = { A * ((A + 1.0) - (A - 1.0) * cw + sq) / a0,
                  2.0 * A * ((A - 1.0) - (A + 1.0) * cw) / a0,
                  A * ((A + 1.0) - (A - 1.0) * cw - sq) / a0,
                  -2.0 * ((A - 1.0) + (A + 1.0) * cw) / a0,
                  ((A + 1.0) + (A - 1.0) * cw - sq) / a0 };
            break;
        }
        case 3: // high shelf
        {
            const double A  = std::pow (10.0, dB / 40.0);
            const double sq = 2.0 * std::sqrt (A) * al;
            const double a0 = (A + 1.0) - (A - 1.0) * cw + sq;
            c = { A * ((A + 1.0) + (A - 1.0) * cw + sq) / a0,
                  -2.0 * A * ((A - 1.0) + (A + 1.0) * cw) / a0,
                  A * ((A + 1.0) + (A - 1.0) * cw - sq) / a0,
                  2.0 * ((A - 1.0) - (A + 1.0) * cw) / a0,
                  ((A + 1.0) - (A - 1.0) * cw - sq) / a0 };
            break;
        }
        case 4: case 5: // 12 / 24 dB per octave, first stage
        {
            const double a0 = 1.0 + al;
            c = { (1.0 + cw) * 0.5 / a0, -(1.0 + cw) / a0, (1.0 + cw) * 0.5 / a0,
                  -2.0 * cw / a0, (1.0 - al) / a0 };
            break;
        }
        default: // 6 / 7
        {
            const double a0 = 1.0 + al;
            c = { (1.0 - cw) * 0.5 / a0, (1.0 - cw) / a0, (1.0 - cw) * 0.5 / a0,
                  -2.0 * cw / a0, (1.0 - al) / a0 };
            break;
        }
    }
    return c;
}

/// |H(e^jw)| in dB.  Same closed form the plot has always used.
inline double magDb (const Coefs& c, double w)
{
    const double cw = std::cos (w), c2 = std::cos (2.0 * w);
    const double num = c.b0 * c.b0 + c.b1 * c.b1 + c.b2 * c.b2
                     + 2.0 * (c.b0 * c.b1 + c.b1 * c.b2) * cw + 2.0 * c.b0 * c.b2 * c2;
    const double den = 1.0 + c.a1 * c.a1 + c.a2 * c.a2
                     + 2.0 * (c.a1 + c.a1 * c.a2) * cw + 2.0 * c.a2 * c2;
    return 10.0 * std::log10 (std::max (num, 1e-16) / std::max (den, 1e-16));
}

/// One band's response at `at` Hz, in dB.  5 and 7 are two cascaded stages.
inline double bandMagDb (int type, double fs, double f, double q, double dB, double at)
{
    const double w = 2.0 * M_PI * at / fs;
    switch (type)
    {
        case 0: return std::abs (dB) < 0.01 ? 0.0 : magDb (rbj (0, fs, f, q, dB), w);
        case 2: return std::abs (dB) < 0.01 ? 0.0 : magDb (rbj (2, fs, f, q, dB), w);
        case 3: return std::abs (dB) < 0.01 ? 0.0 : magDb (rbj (3, fs, f, q, dB), w);
        case 1: return magDb (rbj (1, fs, f, q, 0.0), w);
        case 4: return magDb (rbj (4, fs, f, q, 0.0), w);
        case 5: return magDb (rbj (4, fs, f, q, 0.0), w)
                     + magDb (rbj (4, fs, f, 0.707, 0.0), w);
        case 6: return magDb (rbj (6, fs, f, q, 0.0), w);
        case 7: return magDb (rbj (6, fs, f, q, 0.0), w)
                     + magDb (rbj (6, fs, f, 0.707, 0.0), w);
        default: return 0.0;
    }
}

} // namespace ovbq
