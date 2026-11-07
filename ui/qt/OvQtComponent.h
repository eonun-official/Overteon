#pragma once
// ============================================================
//  OvQtComponent — a JUCE Component that hosts a QWindow.
//
//  This is the approach Qt's own article recommends ("JUCE x Qt", Tor Arne
//  Vestbø, 2023) and the one JUCE uses for its own embedding components
//  (NSViewComponent, HWNDComponent, XEmbedComponent): a Component with a
//  ComponentMovementWatcher that keeps the foreign window's geometry in step.
//
//  Ported from the reference implementation in github.com/torarnv/jucexqt
//  (BSD-3-Clause, Copyright (C) 2023 The Qt Company Ltd.), with the logging
//  removed and the comments written out.  It is deliberately CROSS-PLATFORM:
//  the only Qt calls it makes are QWindow::setParent and
//  QWindow::fromWinId, and the only JUCE calls are the Component API.  There is
//  no X11, no HWND and no NSView code anywhere in here.
//
//  Why not XReparentWindow / SetParent / addSubview by hand: those are three
//  different implementations of one idea, and every one of them hands ownership
//  of our window to the OS in a way the host's teardown does not expect.  The
//  measured result was a double destroy (host destroys the parent, X destroys
//  the child, then Qt destroys it again) and a SIGSEGV when the editor closed.
//  setParent() knows about that and detaches first.
//
//  The critical line is this pair, and the ORDER matters:
//
//      window->setParent (peer ? QWindow::fromWinId (WId (peer->getNativeHandle()))
//                              : nullptr);
//      foreignWindow.reset (window->parent());
//
//  `foreignWindow` owns the wrapper QWindow that fromWinId() created.  It is
//  reset AFTER setParent() so the old wrapper outlives the reparent — releasing
//  it first destroys the window we are about to re-parent into.
// ============================================================
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

class QWindow;

namespace ovqt
{

class Component : public juce::Component
{
public:
    Component();
    ~Component() override;

    /// Takes ownership of `window`.  Passing nullptr detaches and destroys the
    /// current one.
    void setWindow (QWindow* window);
    QWindow* getWindow() const;

private:
    class Pimpl;
    std::unique_ptr<Pimpl> pimpl;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Component)
};

} // namespace ovqt
