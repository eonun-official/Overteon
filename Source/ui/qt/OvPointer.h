#pragma once
// ============================================================
//  Window-level pointer service for the QML scene.
//
//  Every popup in this UI (the cascading preset menu, the EQ node editor)
//  has to answer one question: "is the pointer still on me?"  That question
//  cannot be answered reliably from QML hover events: hover is delivered to
//  the topmost item only, and the order in which one item loses hover while
//  a sibling gains it is not guaranteed.  A leave-timer started after the
//  enter of the popup then closes it under the user's hand.
//
//  This object owns the one place that cannot be wrong: the view's mouse
//  events.  It caches the global cursor position and whether the pointer is
//  inside the view at all, and emits `moved()` on every change.  Popups
//  subscribe to that signal and hit-test themselves with inside().
//
//  No polling, no timers: nothing runs while the mouse does not move.
// ============================================================
#include <QObject>
#include <QPoint>

class QQuickItem;

class OvPointer : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QPoint pos READ pos NOTIFY moved)
    Q_PROPERTY(bool insideWindow READ insideWindow NOTIFY moved)

public:
    /// `viewObject` is the QQuickView (plug-in) or QQuickWidget (preview); the
    /// service installs itself as an event filter on it.
    explicit OvPointer (QObject* viewObject, QObject* parent = nullptr);

    QPoint pos() const { return m_pos; }
    bool   insideWindow() const { return m_insideWindow; }

    /// True when the cached cursor position is inside `item` (a QQuickItem),
    /// in global screen coordinates, grown by `margin` px on every side.
    /// Works for items inside a QQuickView and a QQuickWidget alike.
    Q_INVOKABLE bool inside (QObject* item, int margin = 0) const;

    /// Hide the OS cursor for the duration of a value scrub and restore it
    /// afterwards.  A window-level override is required because the editor box
    /// moves with its node, so the pointer can be over the plot while the
    /// field still owns the drag.  Balanced internally.
    Q_INVOKABLE void setCursorHidden (bool hidden);

    /// Move the OS cursor to a point in `item`'s local coordinates.  Used to
    /// put the pointer back on the number that was being scrubbed once the
    /// box has settled at its node.
    Q_INVOKABLE void warpTo (QObject* item, qreal x, qreal y);

    /// Current global pointer position / warp to a global position.
    ///
    /// The numeric scrub pins the pointer at its press point: a fine-mode drag
    /// needs thousands of pixels of travel, and the screen edge would otherwise
    /// end the gesture long before the parameter reached the end of its range.
    /// The pointer is hidden for the whole gesture, so the pin is invisible.
    Q_INVOKABLE QPoint cursorPos() const;
    Q_INVOKABLE void warpGlobal (int x, int y);

signals:
    void moved();

protected:
    bool eventFilter (QObject* watched, QEvent* event) override;

private:
    void update (const QPoint& global, bool insideWindow);

    QObject* m_view = nullptr;
    QPoint   m_pos;
    bool     m_insideWindow = false;
    bool     m_cursorHidden = false;
};
