#pragma once
// ============================================================
//  Harmonic Injector — standalone FX plugin.
//  v1.5: full 64-bit internal signal path (double buffers,
//  double oversampling, double biquads) + switchable EQ band
//  modes (bells->notch, shelves->resonant 12/24 dB cuts).
// ============================================================
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <cstdint>
#include "DSP.h"
#include "OvRuntimeLog.h"
#include "Limiter.h"
#include "OvBallistics.h"   // ovb::Rms — the level meters' detector

class OverteonProcessor : public juce::AudioProcessor
{
public:
    OverteonProcessor();
    ~OverteonProcessor() override;

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
        ovlog::info ("state restore: " + juce::String (size) + " bytes");
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

    /// Editor window size, persisted per USER (not per project) in the same
    /// settings file as the oversampling choice.  Closing the editor, removing
    /// the plug-in and adding a new instance must all reopen at the size the
    /// user left it at.
    juce::Point<int> loadEditorSize() const;
    void saveEditorSize (int w, int h) const;

    /// The DEFAULT preset written by the Settings panel's SAVE AS DEFAULT.
    /// Separate from the last-used size: the window always opens at the
    /// last-used size, and DEFAULT is only a one-click preset.
    juce::Point<int> loadDefaultEditorSize() const;
    void saveDefaultEditorSize (int w, int h) const;

    /// Meter MODE, per meter (0 = RMS, 1 = TRANSIENT), stored per user beside
    /// the window size.  Display settings — never parameters.
    int  loadMeterMode (const juce::String& key, int fallback) const;
    void saveMeterMode (const juce::String& key, int value) const;

    /// Any other integer display preference (the limiter ring's range).
    int  loadIntSetting (const juce::String& key, int fallback) const;
    void saveIntSetting (const juce::String& key, int value) const;

    // ---- editor taps ----
    //
    // 8192 samples is 170 ms at 48 kHz: the frequency bins are 5.9 Hz apart,
    // which is what turns the low end from a handful of plateaus into a curve
    // and lets the high end show the analyser's own per-bin structure instead
    // of a smoothed envelope.
    static constexpr int fftOrder = 13;
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

    /// Continuous analyser history, as a single-producer ring.
    ///
    /// The old taps published one fftSize block and then went quiet until the
    /// next block had filled (85 ms at 48 kHz, 170 ms at 8192).  The editor
    /// could only redraw on those boundaries, so the curve was a slideshow no
    /// matter how fast the UI painted.  Here the audio thread only appends
    /// samples; the editor takes the last fftSize samples on every frame and
    /// runs its own FFT, so the picture advances at the display rate.  The
    /// extra audio-thread work is one store per sample.
    static constexpr int specRingSize = 2 * fftSize;   // power of two
    std::array<float, specRingSize> specRingIn  {};
    std::array<float, specRingSize> specRingOut {};
    std::atomic<uint32_t> specInWritten  { 0 };        // free-running count
    std::atomic<uint32_t> specOutWritten { 0 };
    uint32_t specInW = 0, specOutW = 0;                // audio thread only

    std::atomic<float> meterInL  { 0.0f }, meterInR  { 0.0f };
    std::atomic<float> meterOutL { 0.0f }, meterOutR { 0.0f };
    /// Sample peak of the same blocks, linear gain.
    ///
    /// The RMS atomics above are what a level meter normally shows, but they
    /// integrate over 300 ms and therefore cannot drop the instant the signal
    /// stops.  The display's bright bar offers a true-instant mode, and this is
    /// the measurement that mode needs: max |x| over the block, no smoothing
    /// anywhere.  The peak is of the block, so it is still frame-rate
    /// independent — it is a measurement of the audio, not of the display.
    std::atomic<float> meterInPkL { 0.0f }, meterInPkR { 0.0f };
    std::atomic<float> meterOutPkL { 0.0f }, meterOutPkR { 0.0f };
    /// Limiter gain reduction, dB.  `meterGrPk` is this block's deepest
    /// reduction (the limiter's own gain, or the soft-clip delta in CLIP);
    /// `meterGr` is the same through the 131 ms one-pole that the harmonic
    /// ratio uses, so the ring's RMS/TRANSIENT modes have one measurement each.
    std::atomic<float> meterGr   { 0.0f };
    std::atomic<float> meterGrPk { 0.0f };
    double grSmooth = 0.0;

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
    int lastOsIdx = 0, driveLat = 0, dryPos = 0;
    std::vector<double> dryFifoL, dryFifoR;
    juce::AudioBuffer<double> dbuf, dryBufD;
    omega::Biquad scEq[5][2], scEq2[5][2], scInv[5][2];
    omega::Biquad postEq[5][2], postEq2[5][2];
    omega::Biquad airLP[2];   // wet-signal high cut (AIR PROTECT)
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
    // Last values written to the optional runtime log, so repeated
    // prepareToPlay calls from a host do not flood the file.
    double loggedSampleRate = -1.0;
    int    loggedBlockSize  = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OverteonProcessor)
};
