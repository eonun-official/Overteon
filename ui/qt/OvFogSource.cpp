#include "OvFogSource.h"

#include <QPainter>
#include <QQuickWindow>
#include <QTimer>

#include <cstdio>
#include <cstdlib>
#include "OvEqView.h"
#include "OvEngine.h"

OvFogSource::OvFogSource (QQuickItem* parent) : QQuickPaintedItem (parent), m_tick (this)
{
    // Safety net only.  The real trigger is the parameter signal below; this
    // catches the case it does not cover (a revision that did not cross
    // OvEngine's change threshold).  It only marks the surface dirty.
    m_tick.setInterval (1000);
    QObject::connect (&m_tick, &QTimer::timeout, this, [this] { update(); });
    m_tick.start();

    // Transparent: everything the strokes do not cover must stay see-through,
    // and the blur samples beyond them.
    setOpaquePainting (false);
    setAntialiasing (true);

    QObject::connect (this, &QQuickItem::widthChanged,  this, [this] { update(); });
    QObject::connect (this, &QQuickItem::heightChanged, this, [this] { update(); });
}

void OvFogSource::setTopView (OvEqView* v)
{
    if (m_top == v) return;
    unsubscribe (m_top);
    m_top = v;
    subscribe (v);
    emit viewChanged();
    update();
}

void OvFogSource::setBottomView (OvEqView* v)
{
    if (m_bottom == v) return;
    unsubscribe (m_bottom);
    m_bottom = v;
    subscribe (v);
    emit viewChanged();
    update();
}

void OvFogSource::subscribe (OvEqView* v)
{
    if (v == nullptr) return;

    connect (v, &OvEqView::engineChanged, this, [this, v] {
        unsubscribe (v);
        subscribe (v);
        update();
    });
    // The plots move when the geometry they are laid out in changes.
    connect (v, &OvEqView::fogGeometryChanged, this, [this] { update(); });

    if (auto* e = v->engine())
    {
        // Any parameter can move either curve: both halves are in one surface
        // now, so there is nothing to gain from splitting the signal.
        connect (e, &OvEngine::paramRevisionChanged, this,
                 [this] (bool, bool) { update(); });
        connect (e, &OvEngine::driveModeChanged, this, [this] { update(); });
    }
}

void OvFogSource::unsubscribe (OvEqView* v)
{
    if (v == nullptr) return;

    disconnect (v, nullptr, this, nullptr);
    if (auto* e = v->engine())
        disconnect (e, nullptr, this, nullptr);
}

void OvFogSource::paint (QPainter* g)
{
    if (m_top == nullptr && m_bottom == nullptr) return;

    const double dpr = window() != nullptr ? window()->devicePixelRatio() : 1.0;
    const int pw = std::max (1, int (std::lround (width()  * dpr)));
    const int ph = std::max (1, int (std::lround (height() * dpr)));

    // Rebuild only when the PICTURE changed.
    //
    // Qt calls paint() once per rendered frame while this item sits inside a
    // subtree something else captures, whether or not update() was called.  The
    // strokes cost ~1000 antialiased path segments plus the curve evaluation, so
    // painting them unconditionally made the fog the most expensive surface in
    // the window for a picture that usually had not changed at all.  The key is
    // what the strokes are drawn from: both halves' bands, their geometry, the
    // size, and the scale they are drawn at.
    quint64 key = 1469598103934665603ull;
    auto mix = [&key] (quint64 v) { key = (key ^ v) * 1099511628211ull; };
    if (m_top != nullptr)    mix (m_top->curveSignature());
    if (m_bottom != nullptr) mix (m_bottom->curveSignature());
    mix (quint64 (pw));
    mix (quint64 (ph));
    if (m_top != nullptr)    mix (quint64 (qint64 (std::llround (m_top->displayScale() * 4096.0))));

    if (key == m_cacheKey && ! m_cache.isNull())
    {
        // The painter already carries the device-pixel-ratio transform, so the
        // destination rect is in item pixels.
        g->drawImage (QRectF (0, 0, width(), height()), m_cache,
                      QRectF (0, 0, m_cache.width(), m_cache.height()));
        return;
    }

    m_cache = QImage (pw, ph, QImage::Format_ARGB32_Premultiplied);
    m_cache.fill (Qt::transparent);
    {
        QPainter cg (&m_cache);
        cg.setRenderHint (QPainter::Antialiasing, true);
        cg.scale (dpr, dpr);

        // Both halves into ONE surface, in one paint call.  This is what makes
        // the fog atomic: two items could be a frame apart from each other, and
        // during a resize that showed the upper half new and the lower stale.
        if (m_top != nullptr)
            m_top->paintOverflowFog (&cg, m_top->mapToItem (this, QPointF (0, 0)));
        if (m_bottom != nullptr)
            m_bottom->paintOverflowFog (&cg, m_bottom->mapToItem (this, QPointF (0, 0)));

        if (std::getenv ("ONYXTEON_FOGDEBUG") != nullptr)
        {
            int nz = 0;
            for (int y = 0; y < m_cache.height(); y += 2)
            {
                const auto* ln = reinterpret_cast<const QRgb*> (m_cache.constScanLine (y));
                for (int x = 0; x < m_cache.width(); x += 2)
                    if (qAlpha (ln[x])) ++nz;
            }
            std::fprintf (stderr,
                "[fogsrc] %dx%d  top origin (%.1f,%.1f) edge %.1f  "
                "bottom origin (%.1f,%.1f) edge %.1f  nonzero %d\n",
                m_cache.width(), m_cache.height(),
                m_top ? m_top->mapToItem (this, QPointF (0, 0)).x() : 0.0,
                m_top ? m_top->mapToItem (this, QPointF (0, 0)).y() : 0.0,
                m_top ? m_top->fogEdge() : 0.0,
                m_bottom ? m_bottom->mapToItem (this, QPointF (0, 0)).x() : 0.0,
                m_bottom ? m_bottom->mapToItem (this, QPointF (0, 0)).y() : 0.0,
                m_bottom ? m_bottom->fogEdge() : 0.0,
                nz);
        }
    }
    m_cacheKey = key;

    // ONYXTEON_FOGDUMP=<path>: write the strokes out, so they can be looked at
    // directly instead of inferred from the composited window.
    if (const char* dump = std::getenv ("ONYXTEON_FOGDUMP"))
        m_cache.save (QString::fromLocal8Bit (dump));

    ++m_revision;
    emit revisionChanged();

    g->drawImage (QRectF (0, 0, width(), height()), m_cache,
                  QRectF (0, 0, m_cache.width(), m_cache.height()));
}
