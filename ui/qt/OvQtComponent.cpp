// OvQtComponent.cpp — ported from github.com/torarnv/jucexqt (BSD-3-Clause,
// Copyright (C) 2023 The Qt Company Ltd.), as described in Qt's "JUCE x Qt"
// article.  See the header for why this shape and not a native reparent.
#include "OvQtComponent.h"

#include <QWindow>

#include <cstdio>
#include <cstdlib>

namespace ovqt
{

class Component::Pimpl : public QObject,
                         public juce::ComponentMovementWatcher,
                         private juce::Timer
{
public:
    Pimpl (QWindow* w, juce::Component& c)
        : juce::ComponentMovementWatcher (&c)
        , window (w)
    {
        // Watch the window as well as the component: a resize that comes from
        // the Qt side (a QML layout, a size hint) has to reach the Component,
        // otherwise the two disagree and the content is clipped or the geometry
        // feedback loop never settles.
        window->installEventFilter (this);

        // Pick up the state we start in — the peer and the visibility already
        // exist by the time this is constructed, so neither callback would
        // otherwise fire until something changed.
        componentPeerChanged();
        componentVisibilityChanged();

        // Re-check the geometry a few times a second.
        //
        // A host can rescale the editor after it is mapped: jalv does it with
        // -S, Ardour does it whenever the UI scale changes.  JUCE then has no
        // reason to call componentMovedOrResized again -- measured, its own
        // idea of the bounds stayed at the pre-scale value for ever -- so the
        // geometry computed in that callback is read at the one moment the
        // value is still stale.
        //
        // The poll watches the two numbers JUCE does report (the peer's scale
        // factor and the component's size) and only resyncs on a real change,
        // so an idle editor costs one comparison per tick and nothing else.
        startTimerHz (4);
    }

    ~Pimpl() override
    {
        // DETACH FIRST.  Members are destroyed in reverse declaration order, so
        // `window` (declared last) would go before `foreignWindow` — and it is
        // still parented to that foreign window at that moment.  Qt then
        // destroys a native window its parent still owns, and the parent's own
        // destruction touches the freed window: measured as
        // "malloc(): mismatching next->prev_size" and a core dump when the
        // editor closed in Carla.
        //
        // setParent(nullptr) is the detachment that knows how to do this
        // correctly, which is the whole reason this class exists.
        window->setParent (nullptr);
        window.reset();
        foreignWindow.reset();
    }

    void componentMovedOrResized (bool, bool) override
    {
        auto* component = getComponent();

        if (component->getWidth() == 0 || component->getHeight() == 0)
        {
            // Nothing was set on the component yet: take the window's own size.
            const auto size = window->size();
            component->setSize (size.width(), size.height());
        }

        auto* peer = component->getPeer();
        if (peer == nullptr)
            return;

        // The component's rectangle in the PEER's coordinate space.  This is
        // the API that makes the whole thing work on every platform: it is what
        // NSViewComponent and HWNDComponent use, and it needs no raw window
        // handles at all.
        const auto area = peer->getAreaCoveredBy (*component);

        // ---- the HOST'S scale factor ------------------------------------
        //
        // A host that scales a plug-in UI does it with a TRANSFORM on the
        // editor (LV2 ui:scaleFactor -> AudioProcessorEditor::setScaleFactor
        // -> setTransform).  The editor's logical size stays the design size --
        // 1040x772 -- while what is on screen is design * scale, and the peer
        // rectangle above is measured in the editor's LOCAL space, which the
        // transform does not enter:
        //
        //     convertCoordinate() walks the component chain applying each
        //     parent's transform, but it stops the moment `source == target`,
        //     so the target's own transform is never applied.
        //
        // Measured with jalv -S 1.25 (same ui:scaleFactor path Ardour uses):
        // the host window was 1300x965 and the Qt window inside it stayed
        // 1040x772, i.e. 80 % of the frame, anchored at the top left.  The
        // plug-in "worked" and looked wrong.
        //
        // So the area is scaled by the component's total transform into the
        // peer.  The scale is taken from the transform chain rather than from
        // Desktop::getGlobalScaleFactor(): the host's factor is per-editor, and
        // the desktop's own factor is already accounted for by devicePixelRatio.
        // (Unused now that the parent window is measured directly; kept out.)

        // ...but the peer works in DEVICE pixels while QWindow works in
        // device-INDEPENDENT pixels, and Qt multiplies by devicePixelRatio when
        // it creates the native window.  Passing the device rectangle straight
        // through made the Qt window DPR times too large: on a 1.302 desktop the
        // JUCE editor was 1040x772 and the QQuickView inside it came out
        // 1354x1005 -- bigger than its own parent, so the UI was drawn at the
        // wrong scale and clipped at the edges.
        const qreal dpr = window->devicePixelRatio();

        if (std::getenv ("OVERTEON_EMSIZEDBG") != nullptr)
        {
            // Every scale in play, because they are four different quantities
            // and picking the wrong one is what left the Qt window at the
            // design size inside a host-scaled frame:
            //   platform  - the screen/DPI scale JUCE's peer was made with
            //   custom    - the factor a host set for THIS editor (LV2
            //               ui:scaleFactor -> setCustomPlatformScaleFactor)
            //   desktop   - what getDesktopScaleFactor() reports
            //   qtDpr     - Qt's own ratio for the embedded QWindow
            //
            // localToScreen is the ground truth: it is the peer's own
            // logical->physical mapping, so the ratio between a one-pixel step
            // in local space and the same step in screen space IS the factor
            // that turns `area` into the pixels the parent window is made of.
            const auto s0 = peer->localToGlobal (juce::Point<int> (0, 0));
            const auto s1 = peer->localToGlobal (juce::Point<int> (area.getWidth(),
                                                                  area.getHeight()));
            const auto pb = peer->getBounds();

            std::fprintf (stderr,
                          "[emsize] comp %dx%d  area %dx%d  platform %.4f  "
                          "custom %.4f  desktop %.4f  qtDpr %.4f\n"
                          "[emsize]   localToGlobal (0,0)->(%d,%d)  (w,h)->(%d,%d)  "
                          "peerBounds %dx%d\n",
                          component->getWidth(), component->getHeight(),
                          area.getWidth(), area.getHeight(),
                          (double) peer->getPlatformScaleFactor(),
                          (double) peer->getCustomPlatformScaleFactor().value_or (-1.0),
                          (double) component->getDesktopScaleFactor(),
                          (double) window->devicePixelRatio(),
                          s0.x, s0.y, s1.x, s1.y, pb.getWidth(), pb.getHeight());

            if (auto* pw = window->parent())
                std::fprintf (stderr, "[emsize]   parentWindow %dx%d  (dpr %.4f)\n",
                              pw->width(), pw->height(), (double) pw->devicePixelRatio());
            else
                std::fprintf (stderr, "[emsize]   parentWindow: none\n");
        }

        // ---- the size to actually use ----------------------------------
        //
        // A host may scale a plug-in UI: `jalv -S 1.25` turned a 1040x772
        // design into a 1300x965 frame, and Ardour's UI scale does the same.
        //
        // getAreaCoveredBy() does NOT reflect that.  It is
        // `localArea * getDesktopScaleFactor()`, and the desktop factor stays
        // 1.0 when the scale is the host's rather than the display's --
        // measured with jalv -S 1.25 on a 1.302 display, it returned
        // 1354x1005 while the editor was really occupying 1692x1256.
        //
        // localToGlobal() is the honest mapping: it walks the component and
        // peer transforms, so a one-pixel step in local space becomes the
        // number of PHYSICAL pixels it really covers on screen.  Subtracting
        // the two corners gives the component's true physical extent without
        // knowing, or caring, whether the factor came from the host, the
        // display, or both -- and it is a plain JUCE call, so the same code
        // runs on Windows, macOS and Linux.
        const auto origin = peer->localToGlobal (juce::Point<int> (area.getX(),
                                                                   area.getY()));
        const auto far    = peer->localToGlobal (juce::Point<int> (area.getRight(),
                                                                   area.getBottom()));

        // physical pixels -> device-independent ones, which is the unit Qt
        // multiplies by devicePixelRatio to build the native window.
        const int physW = far.x - origin.x;
        const int physH = far.y - origin.y;

        // The host's own factor, for the inverse conversion in eventFilter():
        // physical / logical.  Kept as a ratio rather than assuming it equals
        // the peer's platform scale factor, because the peer reports the
        // PRODUCT of host and display scales and only this ratio is what the
        // two directions actually have to agree on.
        if (area.getWidth() > 0 && area.getHeight() > 0)
            lastHostScale = (double) physW / (double) area.getWidth();

        const int useW = qRound (physW / dpr);
        const int useH = qRound (physH / dpr);

        // POSITION comes from the peer rectangle, not from localToGlobal:
        // the latter is in GLOBAL screen coordinates, and setGeometry() wants
        // an offset inside the parent window.  Using the global value put the
        // embedded window at (3,132) inside its parent instead of (0,0) --
        // measured.  The editor fills its frame, so this is the peer rectangle,
        // converted to the same unit as the size.
        // Remember what we asked for.  The Resize that follows is OUR doing and
        // must not be converted back into a component resize: the two
        // conversions round independently, so feeding our own output back in
        // drifts the component by a pixel every pass (measured: 1005 -> 1008
        // and climbing).  Only a geometry that differs from this one is a real
        // resize and worth acting on.
        expectedGeometry = QRect (qRound (area.getX() / dpr),
                                  qRound (area.getY() / dpr),
                                  useW, useH);
        window->setGeometry (expectedGeometry);
    }

    /// Re-sync after the host has rescaled us.
    ///
    /// Polls the two things JUCE itself reports -- the peer's platform scale
    /// factor and the component's size -- and resyncs only when one of them
    /// actually changed.  No window-system call, so this is the same code on
    /// Windows, macOS and Linux; the scale a host applies is always visible in
    /// one of these two numbers, whichever platform the host does it on.
    void timerCallback() override
    {
        auto* component = getComponent();
        auto* peer = component->getPeer();
        if (peer == nullptr) return;

        const float scale = peer->getPlatformScaleFactor();
        const int   w     = component->getWidth();
        const int   h     = component->getHeight();

        if (juce::approximatelyEqual (scale, lastScale) && w == lastW && h == lastH)
            return;

        lastScale = scale;
        lastW     = w;
        lastH     = h;

        if (std::getenv ("OVERTEON_EMSIZEDBG") != nullptr)
            std::fprintf (stderr, "[emsize] timer: scale %.4f size %dx%d -> resync\n",
                          (double) scale, w, h);

        componentMovedOrResized (true, true);
    }

    bool eventFilter (QObject* object, QEvent* event) override
    {
        if (object != window.get())
            return false;

        switch (event->type())
        {
            case QEvent::Resize:
            case QEvent::Move:
            {
                // geometry() is in device-INDEPENDENT pixels, and the JUCE
                // Component is in its own logical pixels.  Converting between
                // them needs BOTH factors:
                //
                //     component = dip * dpr / hostScale
                //
                // Leaving out hostScale is a positive-feedback loop, not just a
                // wrong number: the resync sets the window to
                // (component * hostScale / dpr), this handler reads it back and
                // multiplies by dpr alone, so every pass grows the component by
                // hostScale.  Measured with jalv -S 1.25: 1354 -> 19701 in a
                // couple of seconds.
                const auto g = window->geometry();

                // Our own geometry arriving back: not a resize, see the note
                // where expectedGeometry is set.
                if (g == expectedGeometry)
                    break;

                const qreal dpr = window->devicePixelRatio();
                const qreal invHost = lastHostScale > 0.0 ? 1.0 / lastHostScale : 1.0;
                juce::Rectangle<int> bounds (qRound (g.x()      * dpr * invHost),
                                             qRound (g.y()      * dpr * invHost),
                                             qRound (g.width()  * dpr * invHost),
                                             qRound (g.height() * dpr * invHost));

                auto* component = getComponent();
                if (auto* peer = component->getPeer())
                    bounds = component->getLocalArea (&peer->getComponent(), bounds);

                component->setBounds (bounds);
                break;
            }
            default:
                break;
        }
        return false;
    }

    void componentPeerChanged() override
    {
        auto* component = getComponent();
        auto* peer = component->getPeer();


        // Re-parent before releasing the old wrapper, so the wrapper does not
        // destroy the window it is wrapping.  nullptr when there is no peer yet
        // — that detaches the window, which is correct and is what makes
        // closing the editor safe.
        window->setParent (peer != nullptr
                               ? QWindow::fromWinId (WId (peer->getNativeHandle()))
                               : nullptr);
        foreignWindow.reset (window->parent());

        // ComponentMovementWatcher::componentParentHierarchyChanged() reaches
        // componentMovedOrResized through the Component overload, which returns
        // early when the bounds did not change — but they may not have changed
        // even though our peer did, so the geometry is forced here.
        componentMovedOrResized (true, true);
    }

    void componentVisibilityChanged() override
    {
        window->setVisible (getComponent()->isShowing());
    }

    std::unique_ptr<QWindow> foreignWindow;
    std::unique_ptr<QWindow> window;

    /// What the poll saw last, so the timer only resyncs on a real change
    /// instead of every tick.  Pure JUCE quantities: the peer's scale factor
    /// and the component's size.
    float lastScale = 0.0f;
    int   lastW = 0;
    int   lastH = 0;

    /// physical / logical pixels, i.e. everything the host and the display
    /// together applied to the component.  Used to invert the window geometry
    /// in eventFilter(); see the note there for why leaving it out is a loop.
    double lastHostScale = 1.0;

    /// The geometry most recently written to the QWindow, so the resize it
    /// causes can be told apart from one the host performed.
    QRect expectedGeometry;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Pimpl)
};

Component::Component() = default;
Component::~Component() = default;

void Component::setWindow (QWindow* window)
{
    if (window != getWindow())
        pimpl.reset (window != nullptr ? new Pimpl (window, *this) : nullptr);
}

QWindow* Component::getWindow() const
{
    return pimpl != nullptr ? pimpl->window.get() : nullptr;
}

} // namespace ovqt
