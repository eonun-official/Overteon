#pragma once
// ============================================================
//  Overteon Qt/QML UI — JUCE editor that hosts the QML front end.
//
//  The plugin keeps JUCE for DSP / parameters / host integration; only the
//  editor is Qt.  A single QGuiApplication is created lazily for the whole
//  process and a QQuickView is reparented into the host's editor window.
//  Fallback: if Qt cannot be brought up, the editor paints a hint panel and
//  the processor can still be driven by the host.
// ============================================================
#include <juce_audio_processors/juce_audio_processors.h>

class InjectorProcessor;

class OvQmlEditor : public juce::AudioProcessorEditor,
                    private juce::Timer
{
public:
    explicit OvQmlEditor (InjectorProcessor&);
    ~OvQmlEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void parentHierarchyChanged() override;

private:
    void timerCallback() override;
    void attachToHostWindow();
    void detach();

    class Impl;
    std::unique_ptr<Impl> impl;
    InjectorProcessor& proc;
    int   attachAttempts = 0;
    bool  attached = false;
    void* attachedXid = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OvQmlEditor)
};
