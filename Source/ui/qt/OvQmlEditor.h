#pragma once
// ============================================================
//  Overteon Qt/QML UI — JUCE editor that hosts the QML front end.
//
//  The plug-in keeps JUCE for DSP / parameters / host integration; only the
//  editor is Qt.  One QGuiApplication is created lazily for the whole process,
//  and a QQuickView is embedded into this editor through ovqt::Component —
//  the portable arrangement from Qt's own "JUCE x Qt" article (see
//  OvQtComponent.h).  There is no platform-specific window code here, so the
//  same source builds and runs on Linux, Windows and macOS.
//
//  Fallback: if Qt cannot be brought up, the editor paints a hint panel and the
//  processor can still be driven by the host.
// ============================================================
#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>

class InjectorProcessor;
class QQuickView;

namespace ovqt { class Component; }

class OvQmlEditor : public juce::AudioProcessorEditor,
                    private juce::Timer
{
public:
    explicit OvQmlEditor (InjectorProcessor&);
    ~OvQmlEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    /// The design size in CSS (logical) pixels.  The editor is this size unless
    /// the host gives it less.
    static constexpr int OvThemeWidth  = 1040;
    static constexpr int OvThemeHeight = 772;

    /// The QML scene, running the data poll and the light timer that feed it.
    class Impl;
    std::unique_ptr<Impl> impl;
    InjectorProcessor& proc;

    /// Hosts the QQuickView.  All open/close/geometry handling lives inside it
    /// (ComponentMovementWatcher), which is what makes this work in every host
    /// and every plug-in format without a per-platform code path.
    std::unique_ptr<ovqt::Component> qtComponent;

    /// Design size in DEVICE pixels (design x devicePixelRatio), clamped to the
    /// screen so a scaled editor never asks for more than it can show.
    juce::Point<int> targetSize() const;

    /// How many timer ticks until the "did the size actually land" check runs.
    /// The host applies the resize asynchronously, so the size cannot be read
    /// back in the constructor — it has to be read after the window has been
    /// through a few frames.
    int sizeCheckIn = 0;
    bool sizeReported = false;
};
