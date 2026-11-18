#pragma once
// ============================================================
//  The one high-quality oversampling recipe.
//
//  The saturation chain (osN/osD) and the limiter (os[4]) are separate
//  processors with separate state and separate rate factors — that does NOT
//  change here.  What used to be duplicated was the construction: both wrote
//  the same half-band FIR stage, the same integer-latency flag and the same
//  init call.  Two copies of a filter recipe drift; one copy cannot.
//
//  The stage is JUCE's linear-phase half-band FIR with an explicit transition
//  width.  The stock stages are short and start rolling off around 0.45 fs
//  (19.8 kHz at 44.1 kHz); 0.019 keeps the passband flat to ~0.49 fs
//  (21.6 kHz at 44.1 kHz, 23.5 kHz at 48 kHz) and puts the stopband by Nyquist.
//  Linear phase + integer latency keeps the dry/wet alignment trivial.
// ============================================================
#include <juce_dsp/juce_dsp.h>
#include <memory>

namespace omega
{
    inline constexpr float kOversamplingTransition = 0.019f;
    inline constexpr float kOversamplingStopbandDb = -100.0f;

    inline void initHighQualityOversampling (
        std::unique_ptr<juce::dsp::Oversampling<double>>& os,
        int stages, int maxBlockSize)
    {
        os = std::make_unique<juce::dsp::Oversampling<double>> (2);
        os->clearOversamplingStages();
        for (int i = 0; i < stages; ++i)
            os->addOversamplingStage (
                juce::dsp::Oversampling<double>::filterHalfBandFIREquiripple,
                kOversamplingTransition, kOversamplingStopbandDb,
                kOversamplingTransition, kOversamplingStopbandDb);
        os->setUsingIntegerLatency (true);
        os->initProcessing ((size_t) maxBlockSize);
    }
}
