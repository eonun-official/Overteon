#include "OvSeamFrost.h"
#include "OvEqView.h"
#include "OvFrost.h"

#include <QPainter>
#include <QQuickWindow>
#include <algorithm>
#include <cmath>

namespace
{
    constexpr double kStripW = 550.0;    // #hdr '#hpod' box in the design
    constexpr double kStripH = 68.0;
}

OvSeamFrost::OvSeamFrost (QQuickItem* parent) : QQuickPaintedItem (parent)
{
    setAntialiasing (true);
    setOpaquePainting (false);
}

void OvSeamFrost::setTopView (OvEqView* v)
{
    if (v == m_top) return;
    m_top = v;
    if (m_top != nullptr)
        connect (m_top, &QQuickItem::widthChanged, this, [this] { update(); });
    emit viewsChanged();
    update();
}

void OvSeamFrost::setBottomView (OvEqView* v)
{
    if (v == m_bottom) return;
    m_bottom = v;
    if (m_bottom != nullptr)
        connect (m_bottom, &QQuickItem::widthChanged, this, [this] { update(); });
    emit viewsChanged();
    update();
}

// The seam glass is #hdr::before in the design:
//
//     width:550px; height:68px; left:50%; top:50%  (centred on the seam)
//     backdrop-filter: blur(3px) saturate(.85)
//     background: rgba(255,255,255,.018)
//     mask-image: radial-gradient(ellipse at center,
//                 #000 0%, #000 45%, transparent 78%)
//
// and in the page's FrostEngine it is the same Glass pipeline as the control
// pits, only with shape:"ellipse", no accent glow and light:()=>0.  It is
// reproduced here through OvFrost so both surfaces go through identical code:
// half-resolution capture, 7-tap dual Gaussian, saturate, bilinear upscale,
// veil, then the radial mask.
void OvSeamFrost::paint (QPainter* g)
{
    if (m_top == nullptr || m_bottom == nullptr) return;

    const QImage top    = m_top->contentImage();
    const QImage bottom = m_bottom->contentImage();
    if (top.isNull() && bottom.isNull()) return;

    const double w = width(), h = height();
    if (w < 2.0 || h < 2.0) return;

    const qreal dpr = window() != nullptr ? window()->devicePixelRatio() : 1.0;

    // ---- 1. half-resolution capture of both halves ----------------------
    QImage capture = OvFrost::beginCapture (w, h);
    const double scale = capture.width() / w;      // buffer px per item px

    auto blit = [&] (OvEqView* view, const QImage& img)
    {
        if (view == nullptr || img.isNull()) return;

        // where this view sits in the glass's own coordinates...
        const QPointF origin = view->mapToItem (this, QPointF (0, 0));
        const QRectF self (0, 0, w, h);
        const QRectF target (origin, QSizeF (view->width(), view->height()));
        if (! target.intersects (self)) return;

        // ...then in capture pixels, and clipped to this view like the page
        // does with ctx.save()/clip per source canvas.
        const QRectF dst (origin * scale,
                          QSizeF (view->width() * scale, view->height() * scale));
        QPainter cp (&capture);
        cp.setRenderHint (QPainter::SmoothPixmapTransform, true);
        cp.setClipRect (dst);
        cp.drawImage (dst, img, QRectF (0, 0, img.width(), img.height()));
    };

    blit (m_top, top);
    blit (m_bottom, bottom);

    // ---- 2..8. blur, saturate, upscale, veil, radial mask ---------------
    OvFrost::Params p;
    p.shape    = OvFrost::Shape::Ellipse;
    p.accent   = QColor();          // the seam has no accent glow
    p.light    = 0.0;
    p.sigmaCss = 3.0;
    p.saturate = 0.85;
    p.veil     = 0.018;

    const QImage out = OvFrost::composite (capture, w, h, dpr, p);
    if (out.isNull()) return;

    g->setRenderHint (QPainter::SmoothPixmapTransform, true);
    g->drawImage (QRectF (0, 0, w, h), out,
                  QRectF (0, 0, out.width(), out.height()));
}
