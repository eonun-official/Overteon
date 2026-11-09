// OvQtComponent.cpp — ported from github.com/torarnv/jucexqt (BSD-3-Clause,
// Copyright (C) 2023 The Qt Company Ltd.), as described in Qt's "JUCE x Qt"
// article.  See the header for why this shape and not a native reparent.
#include "OvQtComponent.h"

#include <QWindow>


namespace ovqt
{

class Component::Pimpl : public QObject,
                         public juce::ComponentMovementWatcher
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

        // ...but the peer works in DEVICE pixels while QWindow works in
        // device-INDEPENDENT pixels, and Qt multiplies by devicePixelRatio when
        // it creates the native window.  Passing the device rectangle straight
        // through made the Qt window DPR times too large: on a 1.302 desktop the
        // JUCE editor was 1040x772 and the QQuickView inside it came out
        // 1354x1005 -- bigger than its own parent, so the UI was drawn at the
        // wrong scale and clipped at the edges.
        const qreal dpr = window->devicePixelRatio();
        window->setGeometry (QRect (qRound (area.getX()      / dpr),
                                    qRound (area.getY()      / dpr),
                                    qRound (area.getWidth()  / dpr),
                                    qRound (area.getHeight() / dpr)));
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
                // geometry() is in device-INDEPENDENT pixels (see above), so it
                // is scaled UP to reach the peer's device space before it is
                // handed to a JUCE Component, which thinks in device pixels.
                const qreal dpr = window->devicePixelRatio();
                const auto g = window->geometry();
                juce::Rectangle<int> bounds (qRound (g.x()      * dpr),
                                             qRound (g.y()      * dpr),
                                             qRound (g.width()  * dpr),
                                             qRound (g.height() * dpr));

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
