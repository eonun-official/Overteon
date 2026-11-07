#include "OvEqFog.h"

#include <QPainter>
#include <QTimer>

#include <cstdio>
#include <cstdlib>
#include "OvEqView.h"
#include "OvEngine.h"

OvEqFog::OvEqFog (QQuickItem* parent) : QQuickPaintedItem (parent), m_tick (this)
{
    // A timer as well as the signals.  The signals are the right trigger, but if
    // one is missed the fog silently stops updating — measured: one paint total,
    // ever, and thereafter nothing, because paramRevisionChanged has a change
    // threshold and a spectrum-driven plot repaint does not raise it.  The timer
    // costs one comparison when nothing moved.
    m_tick.setInterval (200);
    QObject::connect (&m_tick, &QTimer::timeout, this, [this] {
        if (m_top != nullptr && m_bottom != nullptr) update();
    });
    m_tick.start();
    // Transparent: everything outside the fog paths must stay see-through so
    // the plots underneath show.  (QQuickPaintedItem has setOpaquePainting, not
    // setOpaque — the latter is QWidget's.)
    setOpaquePainting (false);
    setAntialiasing (true);
}

void OvEqFog::setTopView (OvEqView* v)
{
    if (m_top == v) return;
    unsubscribe (m_top);
    m_top = v;
    subscribe (v);
    emit viewsChanged();
    update();
}

void OvEqFog::setBottomView (OvEqView* v)
{
    if (m_bottom == v) return;
    unsubscribe (m_bottom);
    m_bottom = v;
    subscribe (v);
    emit viewsChanged();
    update();
}

// The fog depends only on the curve, i.e. on the parameters — not on the
// spectrum — so it repaints when a parameter moved and not once per meter tick.
// Both views share one engine, so subscribing through the view covers both.
void OvEqFog::subscribe (OvEqView* v)
{
    if (v == nullptr) return;

    connect (v, &OvEqView::engineChanged, this, [this, v] {
        unsubscribe (v);
        subscribe (v);
        update();
    });

    if (auto* e = v->engine())
    {
        connect (e, &OvEngine::paramRevisionChanged, this,
                 [this] (bool, bool) { update(); });
        connect (e, &OvEngine::driveModeChanged, this, [this] { update(); });
    }
}

void OvEqFog::unsubscribe (OvEqView* v)
{
    if (v == nullptr) return;

    disconnect (v, nullptr, this, nullptr);
    if (auto* e = v->engine())
        disconnect (e, nullptr, this, nullptr);
}

void OvEqFog::paint (QPainter* g)
{
    g->setRenderHint (QPainter::Antialiasing, true);

    // Each view paints its own fog in its own coordinates; `origin` places it in
    // this surface, which spans both sections.
    if (m_top != nullptr)
        m_top->paintOverflowFog (g, m_top->mapToItem (this, QPointF (0, 0)));

    if (m_bottom != nullptr)
        m_bottom->paintOverflowFog (g, m_bottom->mapToItem (this, QPointF (0, 0)));
}
