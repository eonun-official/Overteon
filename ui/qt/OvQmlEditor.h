#pragma once
#include <chrono>
// ============================================================
//  OnyxTEON Qt/QML UI — JUCE editor that hosts the QML front end.
//
//  The plugin keeps JUCE for DSP / parameters / host integration; only the
//  editor is Qt.  A single QGuiApplication is created lazily for the whole
//  process and a QQuickView is reparented into the host's editor window.
//  Fallback: if Qt cannot be brought up, the editor paints a hint panel and
//  the processor can still be driven by the host.
// ============================================================
#include <juce_audio_processors/juce_audio_processors.h>

#include <QtGui/qwindowdefs.h>       // WId, used for the X11 parenting below

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

    // Qt does not dispatch X input for a foreign-parented window reliably, so
    // the Qt window is made input-transparent (empty X input shape) and JUCE
    // forwards the events into the QML scene instead.
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void timerCallback() override;
    void attachToHostWindow();
    void detach();
    void fitToHost (WId containerId);
    void forwardMouse (const juce::MouseEvent&, int qtType);
    void forwardWheel (const juce::MouseEvent&, const juce::MouseWheelDetails&);

    class Impl;
    std::unique_ptr<Impl> impl;
    InjectorProcessor& proc;
    int   attachAttempts = 0;
    /// When the attach retries give up.  A COUNT was wrong: at 120 Hz the 600
    /// attempts were spent in five seconds, so a host that handed us its window
    /// slightly later than that — jalv.gtk3 does, it wraps the widget in a
    /// GtkSocket and maps it afterwards — never got an editor at all, for the
    /// rest of the session.  That is the "the plug-in UI sometimes just does not
    /// appear in jalv" symptom.  Time is the honest budget.
    std::chrono::steady_clock::time_point attachDeadline {};
    int   ticksSinceAttach = 0;
    bool  attached = false;
    void* attachedXid = nullptr;

    // the QML design grid, from web_fx/index.html (#wrap is 1040x772)
    static constexpr int OvThemeWidth  = 1040;
    static constexpr int OvThemeHeight = 772;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OvQmlEditor)
};
