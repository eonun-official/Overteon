#pragma once
// ============================================================
//  A small radix-2 FFT and the analyser maths around it.
//
//  The analyser maths for the preview's JACK source (OvJackSource) and the EQ
//  curve's response (OvBiquad).  Both the spectrum fill and the glow bars read
//  the 128-bin table this produces, so there is exactly one place that turns
//  samples into bins.
// ============================================================
#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

namespace ovfft
{

/// The FFT window in SECONDS, not in samples.
///
/// A fixed 2048-sample window is 42.7 ms at 48 kHz and 21.3 ms at 96 kHz: the
/// same code measures a different amount of the signal depending on the
/// interface's rate, so every derived level (and every meter built on it) is
/// wrong at anything but 48 kHz.  This is the "96 kHz treated as 48 kHz" problem
/// and the fix is to state the window in time and convert.
constexpr double kWindowSeconds = 2048.0 / 48000.0;      // 42.67 ms
constexpr double kPi   = 3.14159265358979323846;

/// FFT size for a given rate: the same DURATION, snapped to a power of two
/// because the transform is radix-2.  2048 at 48 kHz, 4096 at 96 kHz,
/// 1024 at 24 kHz.
inline int fftSizeFor (double sampleRate)
{
    const int want = (int) std::lround (kWindowSeconds * (sampleRate > 0.0 ? sampleRate : 48000.0));
    int n = 256;
    while (n < want && n < 32768) n <<= 1;
    return n;
}

/// Samples in a metering block of the same duration as the design's 1024 at
/// 48 kHz (21.3 ms).
inline int blockSizeFor (double sampleRate)
{
    return (int) std::lround (1024.0 / 48000.0 * (sampleRate > 0.0 ? sampleRate : 48000.0));
}

/// In-place iterative radix-2 FFT (Cooley-Tukey).
inline void fft (std::vector<std::complex<float>>& a)
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

/// One FFT bin per display column, log-spaced 20 Hz .. 20 kHz, mapped to 0..1
/// over an 85 dB range.  Window is Hann; bin 0 is skipped because with a Hann
/// window it holds the window sum and twitches as the waveform slides through
/// the buffer (20 Hz is below one bin at 48 kHz anyway).
inline void spectrumFromRing (const std::vector<float>& ring, int ringPos,
                              double sampleRate, float* dst, int n,
                              std::vector<std::complex<float>>& work)
{
    // The ring's length IS the window: it is sized by fftSizeFor(rate), so the
    // transform always covers the same duration whatever the interface runs at.
    const int kFft = (int) ring.size();
    work.resize ((size_t) kFft);

    for (int i = 0; i < kFft; ++i)
    {
        const int idx = (ringPos + i) % kFft;
        const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * i / (kFft - 1));   // Hann
        work[(size_t) i] = std::complex<float> ((float) (ring[(size_t) idx] * w), 0.0f);
    }
    fft (work);

    const float fsr = (float) sampleRate;
    for (int i = 0; i < n; ++i)
    {
        const float frac = (float) i / (float) std::max (1, n - 1);
        const float fr   = 20.0f * std::pow (1000.0f, frac);
        const int   bin  = std::clamp ((int) (fr / fsr * (float) kFft), 1, kFft / 2);

        const float re  = work[(size_t) bin].real();
        const float im  = work[(size_t) bin].imag();
        const float mag = 2.0f * std::sqrt (re * re + im * im) / (float) kFft;
        const float db  = 20.0f * std::log10 (std::max (mag, 1e-7f));
        dst[i] = std::clamp ((db + 85.0f) / 85.0f, 0.0f, 1.0f);
    }
}

/// Peak of the last `block` samples, in dB.
inline float peakDbOfRing (const std::vector<float>& ring, int ringPos, int block)
{
    const int kFft = (int) ring.size();
    block = std::clamp (block, 1, kFft);
    float pk = 0.0f;
    for (int i = 0; i < block; ++i)
    {
        const int idx = (ringPos - block + i + kFft * 2) % kFft;
        pk = std::max (pk, std::abs (ring[(size_t) idx]));
    }
    return 20.0f * std::log10 (std::max (pk, 1e-6f));
}

} // namespace ovfft
