#include "OvFogSource.h"

#include <QSGGeometryNode>
#include <QSGVertexColorMaterial>
#include <QQuickWindow>
#include <QTimer>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include "OvEqView.h"
#include "OvEngine.h"

OvFogSource::OvFogSource (QQuickItem* parent) : QQuickItem (parent), m_tick (this)
{
    setFlag (QQuickItem::ItemHasContents, true);
    setAntialiasing (true);

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

    // Repaint WHENEVER THE PLOTS REPAINT.
    //
    // Driving this from the parameter signal alone made it update about twice a
    // second while everything around it ran at 60-80: a parameter revision is
    // not the same event as "the curve on screen changed".  contentChanged is
    // emitted at the end of every plot repaint, which is the cadence this has to
    // match; the signature check then decides whether anything really moved.
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

// ---------------------------------------------------------------------------
//  The geometry.
//
//  Every segment of the outgoing curve becomes a quad (two triangles), with the
//  colour carried per vertex.  QSGVertexColorMaterial is Qt's own material for
//  exactly this and works on every RHI backend, so nothing here knows or cares
//  whether the backend is GL.
//
//  Overlapping quads at the joins double-blend slightly; it is invisible on a
//  1-2 px line that a 6 px Gaussian is about to blur.
// ---------------------------------------------------------------------------
QSGNode* OvFogSource::updatePaintNode (QSGNode* old, UpdatePaintNodeData*)
{
    // This surface's own frame rate: this is called once per frame in which the
    // item is rendered.
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
            if (std::getenv ("ONYXTEON_FOGRATE") != nullptr)
                std::fprintf (stderr, "[fogr] geometry %.1f/s\n", m_paintFps);
        }
    }

    const quint64 key = signature();
    if (key == m_builtKey && old != nullptr)
        return old;

    QVector<OvEqView::FogSegment> segs;
    if (m_top != nullptr)
    {
        QVector<OvEqView::FogSegment> part;
        m_top->fogSegments (m_top->mapToItem (this, QPointF (0, 0)), part);
        segs += part;
    }
    if (m_bottom != nullptr)
    {
        QVector<OvEqView::FogSegment> part;
        m_bottom->fogSegments (m_bottom->mapToItem (this, QPointF (0, 0)), part);
        segs += part;
    }

    QSGGeometryNode* node = static_cast<QSGGeometryNode*> (old);
    if (node == nullptr)
    {
        node = new QSGGeometryNode;
        auto* geom = new QSGGeometry (QSGGeometry::defaultAttributes_ColoredPoint2D(),
                                      int (std::max<qsizetype> (6, segs.size() * 6)));
        geom->setDrawingMode (QSGGeometry::DrawTriangles);
        node->setGeometry (geom);
        node->setFlag (QSGNode::OwnsGeometry);
        node->setMaterial (new QSGVertexColorMaterial);
        node->setFlag (QSGNode::OwnsMaterial);
    }

    QSGGeometry* geom = node->geometry();
    if (geom->vertexCount() < segs.size() * 6)
        geom->allocate (int (std::max<qsizetype> (6, segs.size() * 6)));

    auto* v = geom->vertexDataAsColoredPoint2D();
    int n = 0;
    for (const auto& s : segs)
    {
        const double dx = s.b.x() - s.a.x();
        const double dy = s.b.y() - s.a.y();
        const double len = std::hypot (dx, dy);
        if (len < 1e-6) continue;
        const double hw = s.width * 0.5;
        const double nx = -dy / len * hw;
        const double ny =  dx / len * hw;

        const QPointF p0 (s.a.x() + nx, s.a.y() + ny);
        const QPointF p1 (s.b.x() + nx, s.b.y() + ny);
        const QPointF p2 (s.b.x() - nx, s.b.y() - ny);
        const QPointF p3 (s.a.x() - nx, s.a.y() - ny);

        const uchar a = uchar (std::clamp (int (s.alpha * 255.0f + 0.5f), 0, 255));
        auto put = [&] (const QPointF& p) {
            v[n].set (float (p.x()), float (p.y()), 255, 255, 255, a);
            ++n;
        };
        put (p0); put (p1); put (p2);
        put (p0); put (p2); put (p3);
    }
    geom->setVertexCount (n);
    node->markDirty (QSGNode::DirtyGeometry);

    if (std::getenv ("ONYXTEON_FOGDEBUG") != nullptr)
        std::fprintf (stderr, "[foggeo] segs %d verts %d item %.1fx%.1f key %llu\n",
                      int (segs.size()), n, width(), height(),
                      (unsigned long long) key);

    m_builtKey = key;
    ++m_built;
    if (m_built % 60 == 1)
        emit revisionChanged();
    return node;
}
