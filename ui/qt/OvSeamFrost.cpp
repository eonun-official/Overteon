#include "OvSeamFrost.h"
#include "OvEqView.h"
#include "OvFrost.h"

#include <QPainter>
#include <QQuickWindow>
#include <QString>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

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

void OvSeamFrost::attach (OvEqView* v)
{
    if (v == nullptr) return;
    // The plots re-render whenever their data or geometry changes; the glass
    // must rebuild then, or it shows whatever was on screen when it last
    // painted — which on startup is nothing at all.
    connect (v, &QQuickItem::widthChanged,  this, &OvSeamFrost::scheduleRebuild);
    connect (v, &QQuickItem::heightChanged, this, &OvSeamFrost::scheduleRebuild);
    connect (v, &OvEqView::contentChanged,  this, &OvSeamFrost::scheduleRebuild);
}

// FrostEngine rebuilds at most once per frame via requestAnimationFrame; this
// is the same idea with a zero-delay single shot, which also breaks the
// update()->paint()->contentChanged()->update() cycle.
void OvSeamFrost::scheduleRebuild()
{
    if (m_scheduled) return;
    m_scheduled = true;
    QTimer::singleShot (0, this, [this] {
        m_scheduled = false;
        update();
    });
}

void OvSeamFrost::setTopView (OvEqView* v)
{
    if (v == m_top) return;
    m_top = v;
    attach (m_top);
    emit viewsChanged();
    scheduleRebuild();
}

void OvSeamFrost::setBottomView (OvEqView* v)
{
    if (v == m_bottom) return;
    m_bottom = v;
    attach (m_bottom);
    emit viewsChanged();
    scheduleRebuild();
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
// reproduced here through OvFrost so every glass surface in the UI goes
// through identical code: half-resolution capture, 7-tap dual Gaussian,
// saturate, bilinear upscale, veil, then the radial mask.
void OvSeamFrost::paint (QPainter* g)
{
    // ONYXTEON_NOFROST means no glass anywhere, so a reference capture really
    // has none.  (It used to skip only the control pits, which made an A/B of
    // the seam look like the seam was not painting.)
    if (std::getenv ("ONYXTEON_NOFROST") != nullptr) return;
    if (std::getenv ("ONYXTEON_NOSEAM") != nullptr) return;
    if (m_top == nullptr || m_bottom == nullptr) return;

    // unfrostedImage(): the page's FrostEngine samples the raw plot canvases.
    // Using contentImage() here fed the glass a picture that already had each
    // half's control-pit frost baked in, so the seam was blurred twice and came
    // out visibly darker and more saturated than the page.
    const QImage top    = m_top->unfrostedImage();
    const QImage bottom = m_bottom->unfrostedImage();
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

        // ...then in capture pixels, clipped to this view like the page does
        // with ctx.save()/clip per source canvas.
        const QRectF dst (origin * scale,
                          QSizeF (view->width() * scale, view->height() * scale));

        // Only the part that actually lands in the glass.  Scaling a whole
        // 1040x333 plot down to a 275x34 strip at 60 Hz was most of the cost of
        // rebuilding this surface; the source rectangle costs nothing to shrink
        // and produces the same pixels.
        const QRectF keep = dst.intersected (QRectF (0, 0, w * scale, h * scale));
        if (keep.isEmpty()) return;

        const double sx = img.width()  / dst.width();
        const double sy = img.height() / dst.height();
        const QRectF src ((keep.left()   - dst.left()) * sx,
                          (keep.top()    - dst.top())  * sy,
                          keep.width()  * sx,
                          keep.height() * sy);

        QPainter cp (&capture);
        cp.setRenderHint (QPainter::SmoothPixmapTransform, true);
        cp.drawImage (keep, img, src);
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

    if (std::getenv ("ONYXTEON_FROSTDEBUG") != nullptr)
        std::fprintf (stderr, "[seam] %gx%g capture=%dx%d dpr=%.2f top=%dx%d bottom=%dx%d\n",
                      w, h, capture.width(), capture.height(), double (dpr),
                      top.width(), top.height(), bottom.width(), bottom.height());

    // debug: dump what the glass actually produced
    if (std::getenv ("ONYXTEON_SEAMDUMP") != nullptr)
    {
        static int n = 0;
        out.save (QStringLiteral ("/tmp/seam_%1.png").arg (n++));
    }

    g->setRenderHint (QPainter::SmoothPixmapTransform, true);
    g->drawImage (QRectF (0, 0, w, h), out,
                  QRectF (0, 0, out.width(), out.height()));
}
