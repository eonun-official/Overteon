#pragma once
// ============================================================
//  Overteon Qt/QML UI — live JUCE source (plugin + standalone host).
// ============================================================
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "OvSource.h"

class OverteonProcessor;

class OvJuceSource : public OvSource
{
public:
    explicit OvJuceSource (OverteonProcessor& processor);
    ~OvJuceSource() override;

    int         count() const override { return (int) ids.size(); }
    OvParamSpec spec (int index) const override;

    double getNorm (int index) const override;
    void   setNorm (int index, double norm) override;
    double normForValue (int index, double value) const override;
    double valueForNorm (int index, double norm) const override;

    std::string text (int index) const override;

    void beginGesture (int index) override;
    void endGesture   (int index) override;

    void applyInit() override;

    std::string presetListJson() override;
    bool loadPresetNamed (const std::string& folder, const std::string& name) override;
    void openLoadDialog() override;
    void openSaveDialog() override;

    OvMeters meters() const override;
    bool     spectrum (float* dest, int count) override;
    bool     spectrumIn (float* dest, int count) override;
    double   sampleRate() const override;

    int driveModeIndex() const override;

    /// Called after a preset is loaded/saved through a file dialog.
    std::function<void (const juce::String&)> presetNameChanged;

private:
    juce::AudioProcessorParameter* param (int index) const;
    juce::File presetRoot() const;
    juce::File presetsDirectory() const;
    void applyPresetFile (const juce::File&);
    void ensureDefaultPreset (const juce::File& root) const;

    OverteonProcessor& proc;
    juce::StringArray  ids;
    std::vector<juce::AudioProcessorParameter*> params;

    std::unique_ptr<juce::FileChooser> chooser;

    static constexpr int kSpecBins = 128;
    juce::dsp::FFT fft { 11 };
    juce::dsp::WindowingFunction<float> window
        { 1 << 11, juce::dsp::WindowingFunction<float>::hann };
    std::array<float, (1 << 11) * 2> fftBuf {};

    /// Second analyser, for the input tap.  Its own FFT and window buffer so a
    /// poll can return both spectra — the two taps are single-shot by design.
    juce::dsp::FFT fftIn { 11 };
    std::array<float, (1 << 11) * 2> fftInBuf {};
    /// Shared body of spectrum()/spectrumIn(); `inTap` picks the buffer.
    bool analyse (float* dest, int count, bool inTap);
};
