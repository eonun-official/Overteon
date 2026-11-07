#include "OvSeamFrost.h"
#include "OvEqView.h"
#include "OvFrost.h"

#include <QPainter>
#include <QQuickWindow>
#include <QQuickWindow>
#include <QString>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstdlib>

namespace
{
    constexpr double kStripW = 550.0;    // #hdr '#hpod' box in the design
    constexpr double kStripH = 68.0;
}

OvSeamFrost::OvSeamFrost (QQuickItem* parent) : QQuickItem (parent)
{
    setAntialiasing (true);
    setFlag (QQuickItem::ItemHasContents, true);
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
        // Coalesce to 30 Hz.  The plots repaint at 60 and the seam's own
        // composite is ~0.25 ms; refreshing it faster than this only burns CPU
        // for a surface that is already the output of a 3 px blur.
        const auto now = std::chrono::steady_clock::now();
        if (now - m_lastRebuild < std::chrono::milliseconds (33)) return;
        m_lastRebuild = now;
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
QImage OvSeamFrost::renderGlass()
{
    // ONYXTEON_NOFROST means no glass anywhere, so a reference capture really
    // has none.  (It used to skip only the control pits, which made an A/B of
    // the seam look like the seam was not painting.)
    if (std::getenv ("ONYXTEON_NOFROST") != nullptr) return QImage();
    if (std::getenv ("ONYXTEON_NOSEAM") != nullptr) return QImage();
    if (m_top == nullptr || m_bottom == nullptr) return QImage();

    // unfrostedImage(): the page's FrostEngine samples the raw plot canvases.
    // Using contentImage() here fed the glass a picture that already had each
    // half's control-pit frost baked in, so the seam was blurred twice and came
    // out visibly darker and more saturated than the page.
    const QImage top    = m_top->unfrostedImage();
    const QImage bottom = m_bottom->unfrostedImage();
    if (top.isNull() && bottom.isNull()) return QImage();

    const double w = width(), h = height();
    if (w < 2.0 || h < 2.0) return QImage();

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

    // Rebuilding this costs a half-res capture plus a full Gaussian over a
    // 716x89 buffer, and the two plots repaint at up to 120 Hz — so without a
    // cache the seam was re-blurred ~35 times a second for a picture that had
    // barely changed, and that alone was most of the CPU.
    //
    // The key is a checksum of the capture; identical content reuses the last
    // result.  Sampled every 3rd pixel: the content is blurred anyway, so a
    // finer key would only cause needless rebuilds.
    quint64 sum = 0, mix = 1469598103934665603ull;
    for (int y = 0; y < capture.height(); y += 3)
    {
        const auto* line = reinterpret_cast<const QRgb*> (capture.constScanLine (y));
        for (int x = 0; x < capture.width(); x += 3)
        {
            sum += line[x];
            mix = (mix ^ line[x]) * 1099511628211ull;
        }
    }
    const quint64 key = sum ^ mix ^ (quint64 (w) << 48) ^ (quint64 (h) << 32)
                        ^ quint64 (dpr * 100);

    if (key == m_cacheKey && ! m_cache.isNull()
        && m_cache.size() == QSize (int (std::lround (w * dpr)),
                                    int (std::lround (h * dpr))))
        return m_cache;

    const QImage out = OvFrost::composite (capture, w, h, dpr, p);
    if (out.isNull()) return QImage();
    m_cacheKey = key;
    m_cache = out;

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

    return out;
}

QSGNode* OvSeamFrost::updatePaintNode (QSGNode* old, UpdatePaintNodeData*)
{
    const QImage glass = renderGlass();
    if (glass.isNull()) return old;

    if (m_node == nullptr)
    {
        m_node = window()->createImageNode();
        m_node->setFiltering (QSGTexture::Linear);
        m_node->setTextureCoordinatesTransform (QSGImageNode::NoTransform);
    }

    // The glass changes whenever the plots do, so the key is the checksum the
    // cache already computed plus the size — the image itself is never compared.
    const QString key = QStringLiteral ("%1|%2x%3")
                            .arg (m_cacheKey).arg (glass.width()).arg (glass.height());
    if (key != m_texKey)
    {
        m_texKeep = glass.copy();     // must outlive the render pass's upload
        m_node->setTexture (window()->createTextureFromImage (m_texKeep));
        m_texKey = key;
    }
    m_node->setRect (QRectF (0, 0, width(), height()));

    if (old == nullptr) return m_node;
    return old;
}
