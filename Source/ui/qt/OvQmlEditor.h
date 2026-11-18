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

class OverteonProcessor;
class QQuickView;
class QEvent;

namespace ovqt { class Component; }

class OvQmlEditor : public juce::AudioProcessorEditor,
                    private juce::Timer
{
public:
    explicit OvQmlEditor (OverteonProcessor&);
    ~OvQmlEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    /// Forward a key the QML scene did not consume to the HOST window.
    ///
    /// The embedded QWindow sits between the user and the host, so without
    /// this every key died in Qt and Space/transport never reached the DAW.
    /// The key is posted to the host window that had focus before this editor
    /// (WM_SETFOCUS lParam) or, failing that, to the plug-in window's root
    /// owner.  Nothing here changes the focus chain — the mouse wheel needs
    /// the editor focused on Windows.  Returns true when the event was
    /// consumed (i.e. posted).
    bool forwardKeyToHost (QEvent* event);

    /// The design size in CSS (logical) pixels.  The editor is this size unless
    /// the host gives it less.
    static constexpr int OvThemeWidth  = 1040;
    static constexpr int OvThemeHeight = 772;

    /// The QML scene, running the data poll and the light timer that feed it.
    class Impl;
    std::unique_ptr<Impl> impl;
    OverteonProcessor& proc;

    /// Hosts the QQuickView.  All open/close/geometry handling lives inside it
    /// (ComponentMovementWatcher), which is what makes this work in every host
    /// and every plug-in format without a per-platform code path.
    std::unique_ptr<ovqt::Component> qtComponent;

    /// Design size in the units expected by the host, clamped to the screen.
    juce::Point<int> targetSize() const;

    /// The fixed 1040x772 design, with the platform's DPI and the screen fit
    /// applied.  ORIGINAL always returns this, whatever is saved on disk.
    juce::Point<int> designSize() const;

    /// Apply the screen fit and the hard size clamps to an arbitrary size.
    juce::Point<int> clampSize (juce::Point<int>) const;

    /// How many timer ticks until the "did the size actually land" check runs.
    /// The host applies the resize asynchronously, so the size cannot be read
    /// back in the constructor — it has to be read after the window has been
    /// through a few frames.
    int sizeCheckIn = 0;
    bool sizeReported = false;

    /// Editor-size persistence.  `saveSizeIn` is a debounce: a drag calls
    /// resized() dozens of times, and the file is written once, half a second
    /// after the last one.  `lastSavedSize` starts at the value loaded from
    /// disk so opening and closing an untouched editor writes nothing.
    int saveSizeIn = 0;
    juce::Point<int> lastSavedSize;
};
