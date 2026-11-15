#include "OvEnv.h"
#include "OvFogSource.h"

#include <QPainter>
#include <QQuickWindow>
#include <QSGImageNode>
#include <QTimer>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include "OvEqView.h"
#include "OvEngine.h"

OvFogSource::OvFogSource (QQuickItem* parent) : QQuickItem (parent), m_tick (this)
{
    setFlag (QQuickItem::ItemHasContents, true);

    // Safety net only.  The real trigger is the plots' contentChanged; this
    // catches a revision that did not cross OvEngine's change threshold.
    m_tick.setInterval (1000);
    QObject::connect (&m_tick, &QTimer::timeout, this, [this] { markDirty(); });
    m_tick.start();

    QObject::connect (this, &QQuickItem::widthChanged,  this, [this] { markDirty(); });
    QObject::connect (this, &QQuickItem::heightChanged, this, [this] { markDirty(); });
}

void OvFogSource::setTopView (OvEqView* v)
{
    if (m_top == v) return;
    unsubscribe (m_top);
    m_top = v;
    subscribe (v);
    emit viewChanged();
    markDirty();
}

void OvFogSource::setBottomView (OvEqView* v)
{
    if (m_bottom == v) return;
    unsubscribe (m_bottom);
    m_bottom = v;
    subscribe (v);
    emit viewChanged();
    markDirty();
}

void OvFogSource::subscribe (OvEqView* v)
{
    if (v == nullptr) return;

    connect (v, &OvEqView::engineChanged, this, [this, v] {
        unsubscribe (v);
        subscribe (v);
        markDirty();
    });
    connect (v, &OvEqView::fogGeometryChanged, this, [this] { markDirty(); });

    // Repaint WHENEVER THE PLOTS REPAINT.  The parameter signal alone made this
    // update about twice a second while everything around it ran at 60-80: a
    // parameter revision is not the same event as "the curve on screen changed".
    connect (v, &OvEqView::contentChanged, this, [this] { markDirty(); });

    if (auto* e = v->engine())
        connect (e, &OvEngine::driveModeChanged, this, [this] { markDirty(); });
}

void OvFogSource::unsubscribe (OvEqView* v)
{
    if (v == nullptr) return;
    disconnect (v, nullptr, this, nullptr);
    if (auto* e = v->engine())
        disconnect (e, nullptr, this, nullptr);
}

void OvFogSource::markDirty()
{
    m_dirty = true;
    update();
}

quint64 OvFogSource::signature() const
{
    quint64 h = 1469598103934665603ull;
    auto mix = [&h] (quint64 v) { h = (h ^ v) * 1099511628211ull; };
    if (m_top != nullptr)    mix (m_top->curveSignature());
    if (m_bottom != nullptr) mix (m_bottom->curveSignature());
    mix (quint64 (std::llround (width()  * 16.0)));
    mix (quint64 (std::llround (height() * 16.0)));
    if (m_top != nullptr)
        mix (quint64 (std::llround (m_top->displayScale() * 4096.0)));
    return h;
}

QSGNode* OvFogSource::updatePaintNode (QSGNode* old, UpdatePaintNodeData*)
{
    // Free what the previous frame retired: a texture replaced during the sync
    // phase may still be referenced by the batch the renderer is finishing, so it
    // gets one frame of grace.  QSGImageNode does not own its texture.
    for (QSGTexture* t : m_retired) delete t;
    m_retired.clear();

    if (window() == nullptr) return old;

    // Count this surface's own frames.
    {
        static int frames = 0;
        static auto t0 = std::chrono::steady_clock::now();
        ++frames;
        const double dt = std::chrono::duration<double> (
            std::chrono::steady_clock::now() - t0).count();
        if (dt >= 1.0)
        {
            m_paintFps = frames / dt;
            frames = 0; t0 = std::chrono::steady_clock::now();
            if (ovEnvFlag ("OVERTEON_FOGRATE"))
                std::fprintf (stderr, "[fogr] repaint %.1f/s\n", m_paintFps);
        }
    }

    if (old == nullptr)
    {
        m_node = nullptr;
        m_texKey = QUuid();
        m_builtKey = 0;
    }

    if (m_node == nullptr)
    {
        m_node = window()->createImageNode();
        m_node->setFiltering (QSGTexture::Linear);
        m_node->setTextureCoordinatesTransform (QSGImageNode::NoTransform);
    }

    const quint64 key = signature();
    if (key != m_builtKey || m_img.isNull())
    {
        const double dpr = window()->devicePixelRatio();
        const int pw = std::max (1, int (std::lround (width()  * dpr)));
        const int ph = std::max (1, int (std::lround (height() * dpr)));

        if (m_img.width() != pw || m_img.height() != ph)
            m_img = QImage (pw, ph, QImage::Format_ARGB32_Premultiplied);
        m_img.fill (Qt::transparent);

        {
            QPainter g (&m_img);
            g.setRenderHint (QPainter::Antialiasing, true);
            g.scale (dpr, dpr);
            // Both halves into ONE surface, so the fog cannot be a frame apart
            // from itself while a window is dragged.
            if (m_top != nullptr)
                m_top->paintOverflowFog (&g, m_top->mapToItem (this, QPointF (0, 0)));
            if (m_bottom != nullptr)
                m_bottom->paintOverflowFog (&g, m_bottom->mapToItem (this, QPointF (0, 0)));
        }

        // Upload.  The image is kept as a member because
        // createTextureFromImage commits the pixels during the render pass.
        if (QSGTexture* old = m_node->texture()) m_retired.push_back (old);
        m_keep = m_img;
        m_node->setTexture (window()->createTextureFromImage (m_keep));
        m_builtKey = key;

        ++m_revision;
        emit revisionChanged();
    }

    m_node->setRect (QRectF (0, 0, width(), height()));
    return old != nullptr ? old : m_node;
}
