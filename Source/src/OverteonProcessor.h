#pragma once
// ============================================================
//  Harmonic Injector — standalone FX plugin.
//  v1.5: full 64-bit internal signal path (double buffers,
//  double oversampling, double biquads) + switchable EQ band
//  modes (bells->notch, shelves->resonant 12/24 dB cuts).
// ============================================================
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include "DSP.h"
#include "Limiter.h"
#include "OvBallistics.h"   // ovb::Rms — the level meters' detector

class OverteonProcessor : public juce::AudioProcessor
{
public:
    OverteonProcessor();
    ~OverteonProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& l) const override
    {
        return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
            && l.getMainInputChannelSet()  == juce::AudioChannelSet::stereo();
    }
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                         { return true; }
    const juce::String getName() const override             { return "Overteon"; }

    /// Hand the bypass parameter to the host so its own bypass control drives
    /// ours.  Without this, a host's bypass button either does nothing or
    /// suspends the plug-in entirely (which also stops the meters); with it,
    /// bypass is the one the UI shows and both stay in step.
    juce::AudioProcessorParameter* getBypassParameter() const override
    {
        return apvts.getParameter ("bypass");
    }
    bool acceptsMidi() const override                       { return false; }
    bool producesMidi() const override                      { return false; }
    bool isMidiEffect() const override                      { return false; }
    double getTailLengthSeconds() const override            { return 0.0; }
    int getNumPrograms() override                           { return 1; }
    int getCurrentProgram() override                        { return 0; }
    void setCurrentProgram (int) override                   {}
    const juce::String getProgramName (int) override        { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& dest) override
    {
        if (auto xml = apvts.copyState().createXml())
            copyXmlToBinary (*xml, dest);
    }
    void setStateInformation (const void* data, int size) override
    {
        if (auto xml = getXmlFromBinary (data, size))
            if (xml->hasTagName (apvts.state.getType()))
            {
                // Oversampling is a GLOBAL per-user preference, NOT preset
                // data: keep the current choice across preset/session loads.
                const float osKeep = par.osFactor != nullptr ? par.osFactor->load() : 1.0f;
                apvts.replaceState (juce::ValueTree::fromXml (*xml));
                apvts.getParameterAsValue ("osFactor") = juce::jlimit (0.0f, 4.0f, osKeep);
            }
    }

    void applyInit();

    // ---- editor taps ----
    static constexpr int fftOrder = 11;
    static constexpr int fftSize  = 1 << fftOrder;
    std::array<float, fftSize> fftData {};
    std::atomic<bool>  fftReady  { false };

    /// INPUT analyser: the signal entering the sidechain EQ, i.e. before any of
    /// this processor's own work.  The output tap above is filled at the end of
    /// the chain, so the two together are what the SC plot and the PQ plot
    /// show.  A second buffer rather than one shared flag, because the editor
    /// needs both pictures in the same frame.
    std::array<float, fftSize> fftInData {};
    std::atomic<bool>  fftInReady { false };
    std::atomic<float> meterInL  { 0.0f }, meterInR  { 0.0f };
    std::atomic<float> meterOutL { 0.0f }, meterOutR { 0.0f };
    std::atomic<float> meterHarm { -100.0f };   // harmonic/dry ratio, dB (smoothed)
    /// The same ratio WITHOUT the 131 ms smoothing: this block's measurement.
    ///
    /// The ring's Peak display needs a value that has not been averaged over
    /// time, or "peak" would be a mean wearing a peak's name.  There is no
    /// oversampling here and there should not be — the ratio is two energies
    /// measured over a block, not a waveform — so "peak" for this meter is the
    /// block's own maximum, held by the display with the usual release.
    std::atomic<float> meterHarmPk { -100.0f };
    double harmRatioS = 0.0;

    /// The RMS detectors behind the four level atomics.  Audio thread only; the
    /// atomics are what the editor reads.  Their integration constant is
    /// kMeterRmsTau, so the fall is 4.343/tau dB/s at any sample rate and any
    /// buffer size (ui/qt/OvBallistics.h explains why that matters).
    ovb::Rms rmsInL, rmsInR, rmsOutL, rmsOutR;

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void cacheParams();
    void updateReportedLatency();
    static juce::File getGlobalSettingsFile();
    int  loadGlobalOsFactor() const;
    void saveGlobalOsFactor (int idx) const;

    struct Params
    {
        std::atomic<float>* bypass    {};  // whole plug-in: pass the input through
        std::atomic<float>* drive     {};
        std::atomic<float>* mix       {};
        std::atomic<float>* type      {};
        std::atomic<float>* mode      {};
        std::atomic<float>* osFactor  {};
        std::atomic<float>* inGain    {};
        std::atomic<float>* outGain   {};
        std::atomic<float>* invAmt    {};
        std::atomic<float>* harmGain  {};
        std::atomic<float>* harmSolo  {};
        std::atomic<float>* airFreq   {};   // wet-only high cut: protects the dry's air band
        std::atomic<float>* evenOdd   {};   // -1 fully even .. +1 fully odd harmonic bias
        std::atomic<float>* msMode    {};   // 0 Stereo  1 Mid  2 Side  3 M/S
        std::atomic<float>* autoGain  {};   // loudness-matched bypass compensation
        std::atomic<float>* scF[5] {};
        std::atomic<float>* scG[5] {};
        std::atomic<float>* scQ[5] {};
        std::atomic<float>* pqF[5] {};
        std::atomic<float>* pqG[5] {};
        std::atomic<float>* pqQ[5] {};
        std::atomic<float>* scType[5] {};   // unified 8-way type, every band
        std::atomic<float>* pqType[5] {};   // unified 8-way type, every band
        std::atomic<float>* scOn[5]   {};   // band switched in (identity biquad when off)
        std::atomic<float>* pqOn[5]   {};
        std::atomic<float>* scRange  {};    // SC EQ display-range selector (0..3)
        std::atomic<float>* pqRange  {};    // Post EQ display-range selector (0..3)
        std::atomic<float>* protectMode {}; // 0 Off, 1 Clip, 2 Modern, 3 True Peak
        std::atomic<float>* limiterOs   {}; // 0 2x, 1 4x, 2 8x
    } par;

    std::unique_ptr<juce::dsp::Oversampling<double>> osN[5];
    std::unique_ptr<juce::dsp::Oversampling<double>> osD[5];   // idle clones for the dry path
    int osLat[5] { 0, 0, 0, 0, 0 };
    int lastOsIdx = 0, driveLat = 0, dryPos = 0, firGuardLat = 31;
    std::vector<double> dryFifoL, dryFifoR;
    juce::AudioBuffer<double> dbuf, dryBufD;
    omega::Biquad scEq[5][2], scEq2[5][2], scInv[5][2];
    omega::Biquad postEq[5][2], postEq2[5][2];
    omega::Biquad airLP[2];   // wet-signal high cut (AIR PROTECT)
    // shared linear-phase FIR guard (manual convolution, fully controlled),
    // applied to BOTH harmonics and dry paths so any residual leakage meets
    // the dry with identical phase everywhere.
    static constexpr int kGuardTaps = 63;
    std::array<double, kGuardTaps> guardCoef {};
    std::array<double, kGuardTaps> guardZH[2] {};   // harmonics-path delay lines
    std::array<double, kGuardTaps> guardZD[2] {};   // dry-path delay lines
    int guardZHpos[2] { 0, 0 }, guardZDpos[2] { 0, 0 };
    double gSmooth = 1.0;   // adaptive linear-null gain (Bussgang tracker)
    double scMultS = 1.0;   // smoothed SC EQ range multiplier (click-free switching)
    double pqMultS = 1.0;   // smoothed Post EQ range multiplier
    double agComp  = 1.0;   // smoothed auto-gain compensation factor
    OverteonLimiter limiter;
    juce::AudioBuffer<double> limBuf;
    int lastLimiterMode = 2, lastLimiterOs = 1;
    int lastDmode = 1;
    int fftIdx = 0;
    int fftInIdx = 0;
    double sr = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OverteonProcessor)
};
