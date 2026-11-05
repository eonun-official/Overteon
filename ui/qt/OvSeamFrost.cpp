#include "OvSeamFrost.h"
#include "OvEqView.h"

#include <QPainter>
#include <QRadialGradient>
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

void OvSeamFrost::paint (QPainter* g)
{
    if (m_top == nullptr || m_bottom == nullptr) return;

    const QImage top = m_top->contentImage();
    const QImage bottom = m_bottom->contentImage();
    if (top.isNull() && bottom.isNull()) return;

    const double w = width(), h = height();
    if (w < 2.0 || h < 2.0) return;

    const qreal dpr = window() != nullptr ? window()->devicePixelRatio() : 1.0;
    const int pw = int (std::lround (w * dpr)), ph = int (std::lround (h * dpr));

    // Collect the content under the strip from both halves.  The strip is
    // centred on the seam, so its upper part comes from the sidechain plot and
    // its lower part from the post eq plot.
    //
    // +8 px of bleed on every side so the filter never clamps at the rim: the
    // source is rendered slightly larger and the extra border is cropped away
    // by the drawing below.
    constexpr int kPad = 8;
    QImage canvas (pw + 2 * kPad, ph + 2 * kPad, QImage::Format_ARGB32_Premultiplied);
    canvas.fill (QColor (0x16, 0x18, 0x1d));

    {
        QPainter cg (&canvas);
        cg.setRenderHint (QPainter::SmoothPixmapTransform, true);
        cg.translate (kPad, kPad);
        cg.scale (dpr, dpr);

        // position of this strip in each view's own coordinates
        auto blit = [&] (OvEqView* view, const QImage& img)
        {
            if (view == nullptr || img.isNull()) return;

            // where the view sits in this item's coordinate space
            const QPointF origin = view->mapToItem (this, QPointF (0, 0));
            const QRectF target (origin, QSizeF (view->width(), view->height()));
            const QRectF self (0, 0, w, h);
            if (! target.intersects (self)) return;

            cg.save();
            cg.setClipRect (target);
            cg.drawImage (target, img, QRectF (0, 0, img.width(), img.height()));
            cg.restore();
        };

        blit (m_top, top);
        blit (m_bottom, bottom);
    }

    // separable Gaussian, sigma 3 px in item space
    QImage blurred = OvEqView::gaussianBlur (canvas, 3.0 * dpr, 0.85);
    blurred.setDevicePixelRatio (1.0);
    const QRectF src (kPad, kPad, pw, ph);

    g->setRenderHint (QPainter::SmoothPixmapTransform, true);
    g->save();
    g->setClipRect (QRectF (0, 0, w, h));
    g->drawImage (QRectF (0, 0, w, h), blurred, src);

    // background: rgba(255,255,255,.018)
    g->fillRect (QRectF (0, 0, w, h), QColor (255, 255, 255, 5));

    // mask-image: radial-gradient(ellipse at center, #000 0%, #000 45%,
    // transparent 78%) — a destination-in fade over the whole strip
    g->setCompositionMode (QPainter::CompositionMode_DestinationIn);
    g->translate (w * 0.5, h * 0.5);
    g->scale (w * 0.5, h * 0.5);
    QRadialGradient m (0, 0, 1);
    m.setColorAt (0.00, QColor (0, 0, 0, 255));
    m.setColorAt (0.45, QColor (0, 0, 0, 255));
    m.setColorAt (0.78, QColor (0, 0, 0, 0));
    m.setColorAt (1.00, QColor (0, 0, 0, 0));
    g->setBrush (m);
    g->setPen (Qt::NoPen);
    g->drawRect (QRectF (-1, -1, 2, 2));
    g->restore();
}
