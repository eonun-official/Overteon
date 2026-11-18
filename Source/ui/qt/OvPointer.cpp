#include "OvPointer.h"

#include <QCursor>
#include <QEvent>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QRect>
#include <QQuickItem>
#include <QWindow>
// The plug-in links Qt Quick only; the preview additionally links Widgets and
// uses a QQuickWidget there.  QT_WIDGETS_LIB is defined exactly in the builds
// that have it.
#if defined(QT_WIDGETS_LIB)
 #include <QWidget>
#endif

OvPointer::OvPointer (QObject* viewObject, QObject* parent)
    : QObject (parent), m_view (viewObject)
{
    if (m_view != nullptr)
        m_view->installEventFilter (this);
}

void OvPointer::update (const QPoint& global, bool insideWindow)
{
    if (global == m_pos && insideWindow == m_insideWindow)
        return;

    m_pos = global;
    m_insideWindow = insideWindow;
    emit moved();
}

void OvPointer::setCursorHidden (bool hidden)
{
    if (hidden == m_cursorHidden)
        return;

    m_cursorHidden = hidden;
    if (hidden)
        QGuiApplication::setOverrideCursor (Qt::BlankCursor);
    else
        QGuiApplication::restoreOverrideCursor();
}

void OvPointer::warpTo (QObject* item, qreal x, qreal y)
{
    auto* qi = qobject_cast<QQuickItem*> (item);
    if (qi == nullptr || qi->window() == nullptr)
        return;

    QCursor::setPos (qi->mapToGlobal (QPointF (x, y)).toPoint());
}

QPoint OvPointer::cursorPos() const
{
    return QCursor::pos();
}

void OvPointer::warpGlobal (int x, int y)
{
    QCursor::setPos (x, y);
}

bool OvPointer::eventFilter (QObject* watched, QEvent* event)
{
    if (watched != m_view)
        return QObject::eventFilter (watched, event);

    switch (event->type())
    {
        case QEvent::MouseMove:
        case QEvent::HoverMove:
        {
            QPointF local;
            if (event->type() == QEvent::MouseMove)
                local = static_cast<QMouseEvent*> (event)->position();
            else
                local = static_cast<QHoverEvent*> (event)->position();

            QPoint global;
#if defined(QT_WIDGETS_LIB)
            if (auto* w = qobject_cast<QWidget*> (m_view))
                global = w->mapToGlobal (local.toPoint());
            else
#endif
            if (auto* win = qobject_cast<QWindow*> (m_view))
                global = win->mapToGlobal (local.toPoint());
            else
                return false;

            update (global, true);
            break;
        }

        case QEvent::Enter:
            // Enter carries no position; one QCursor query per entry is free.
            update (QCursor::pos(), true);
            break;

        case QEvent::Leave:
            // The last position is on the window edge; remember it but mark it
            // as outside so every popup stops considering itself hovered.
            update (m_pos, false);
            break;

        default:
            break;
    }

    return QObject::eventFilter (watched, event);
}

bool OvPointer::inside (QObject* item, int margin) const
{
    auto* qi = qobject_cast<QQuickItem*> (item);
    if (qi == nullptr || ! qi->isVisible() || ! m_insideWindow)
        return false;

    // mapToGlobal() already includes the scene scale and the QQuickWidget's
    // offscreen-window offset, so no window arithmetic is needed here.
    const QPointF topLeft = qi->mapToGlobal (QPointF (0.0, 0.0));
    const QRect rect (topLeft.toPoint(),
                      QSize (qRound (qi->width()), qRound (qi->height())));
    return rect.adjusted (-margin, -margin, margin, margin).contains (m_pos);
}
