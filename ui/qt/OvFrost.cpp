#include "OvFrost.h"

#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QRadialGradient>
#include <algorithm>
#include <cmath>

namespace OvFrost
{
namespace
{
    // ---- FrostEngine's kernel constants (web_fx/index.html) ----------------
    constexpr double kTapOffset[3] { 1.0, 2.0, 6.0 };
    constexpr double kTapWeight[3] { 0.15, 0.10, 0.05 };
    constexpr double kCentreWeight  { 0.40 };
    // variance of that tap set per unit offset: 2*(.15*1 + .10*4 + .05*36)
    constexpr double kVarPerOffset2 { 4.70 };

    constexpr int kPad = 8;              // PAD in the page: bleed at the rim

    inline QRgb clampFetch (const QRgb* row, int n, int i)
    {
        return row[std::clamp (i, 0, n - 1)];
    }
}

QImage beginCapture (double w, double h)
{
    // Half resolution, and NOT tagged with a device pixel ratio: this is a
    // plain buffer whose pixels are `scale` per item pixel.  The page does the
    // same with a canvas whose width is round(W/2).
    const int w0 = std::max (2, int (std::lround (w / 2.0)));
    const int h0 = std::max (2, int (std::lround (h / 2.0)));
    QImage cap (w0, h0, QImage::Format_ARGB32_Premultiplied);
    cap.fill (QColor (0x16, 0x18, 0x1d));
    return cap;
}

void blitInto (QImage& capture, const QImage& src, const QRectF& dest)
{
    if (src.isNull() || dest.isEmpty()) return;
    QPainter p (&capture);
    p.setRenderHint (QPainter::SmoothPixmapTransform, true);
    // `dest` is already expressed in capture pixels by the caller, which is
    // what FrostEngine passes to ctx.drawImage().
    p.drawImage (dest, src, QRectF (0, 0, src.width(), src.height()));
}

QImage dualGaussian (const QImage& src, double sigma)
{
    if (src.isNull()) return src;

    const QImage in = src.convertToFormat (QImage::Format_ARGB32_Premultiplied);
    const int w = in.width(), h = in.height();
    if (w < 2 || h < 2) return in;

    // offset = sigma / sqrt(variance-per-unit-offset), then whole pixels, and
    // never less than one: exactly FrostEngine's
    //   const offset = Math.max(0.35, sigma / Math.sqrt(VAR_PER_OFFSET2));
    //   const step = TAP_OFF.map(o => Math.max(1, Math.round(o * offset)));
    const double offset = std::max (0.35, sigma / std::sqrt (kVarPerOffset2));
    int step[3];
    for (int k = 0; k < 3; ++k)
        step[k] = std::max (1, int (std::lround (kTapOffset[k] * offset)));

    QImage tmp (w, h, QImage::Format_ARGB32_Premultiplied);
    QImage out (w, h, QImage::Format_ARGB32_Premultiplied);

    // ---- horizontal pass ----
    for (int y = 0; y < h; ++y)
    {
        const QRgb* s = reinterpret_cast<const QRgb*> (in.constScanLine (y));
        QRgb* d = reinterpret_cast<QRgb*> (tmp.scanLine (y));
        for (int x = 0; x < w; ++x)
        {
            double r = qRed   (s[x]) * kCentreWeight;
            double g = qGreen (s[x]) * kCentreWeight;
            double b = qBlue  (s[x]) * kCentreWeight;
            double a = qAlpha (s[x]) * kCentreWeight;
            for (int k = 0; k < 3; ++k)
            {
                const QRgb l = clampFetch (s, w, x - step[k]);
                const QRgb rr = clampFetch (s, w, x + step[k]);
                const double wt = kTapWeight[k];
                r += (qRed (l)   + qRed (rr))   * wt;
                g += (qGreen (l) + qGreen (rr)) * wt;
                b += (qBlue (l)  + qBlue (rr))  * wt;
                a += (qAlpha (l) + qAlpha (rr)) * wt;
            }
            d[x] = qRgba (int (std::clamp (r, 0.0, 255.0)),
                          int (std::clamp (g, 0.0, 255.0)),
                          int (std::clamp (b, 0.0, 255.0)),
                          int (std::clamp (a, 0.0, 255.0)));
        }
    }

    // ---- vertical pass ----
    for (int y = 0; y < h; ++y)
    {
        // pre-fetch the rows this one needs, clamped
        const QRgb* rows[7];
        for (int k = 0; k < 3; ++k)
        {
            rows[k]       = reinterpret_cast<const QRgb*> (
                                tmp.constScanLine (std::clamp (y - step[k], 0, h - 1)));
            rows[3 + k]   = reinterpret_cast<const QRgb*> (
                                tmp.constScanLine (std::clamp (y + step[k], 0, h - 1)));
        }
        const QRgb* c = reinterpret_cast<const QRgb*> (tmp.constScanLine (y));
        QRgb* d = reinterpret_cast<QRgb*> (out.scanLine (y));
        for (int x = 0; x < w; ++x)
        {
            double r = qRed (c[x]) * kCentreWeight;
            double g = qGreen (c[x]) * kCentreWeight;
            double b = qBlue (c[x]) * kCentreWeight;
            double a = qAlpha (c[x]) * kCentreWeight;
            for (int k = 0; k < 3; ++k)
            {
                const QRgb u = rows[k][x];
                const QRgb d2 = rows[3 + k][x];
                const double wt = kTapWeight[k];
                r += (qRed (u)   + qRed (d2))   * wt;
                g += (qGreen (u) + qGreen (d2)) * wt;
                b += (qBlue (u)  + qBlue (d2))  * wt;
                a += (qAlpha (u) + qAlpha (d2)) * wt;
            }
            d[x] = qRgba (int (std::clamp (r, 0.0, 255.0)),
                          int (std::clamp (g, 0.0, 255.0)),
                          int (std::clamp (b, 0.0, 255.0)),
                          int (std::clamp (a, 0.0, 255.0)));
        }
    }

    return out;
}

QImage saturate (const QImage& src, double sat)
{
    if (src.isNull() || std::abs (sat - 1.0) < 1e-6) return src;

    QImage out = src.convertToFormat (QImage::Format_ARGB32_Premultiplied);
    const int w = out.width(), h = out.height();
    for (int y = 0; y < h; ++y)
    {
        QRgb* d = reinterpret_cast<QRgb*> (out.scanLine (y));
        for (int x = 0; x < w; ++x)
        {
            if (qAlpha (d[x]) < 1) continue;      // transparent: nothing to tint
            const double r = qRed (d[x]), g = qGreen (d[x]), b = qBlue (d[x]);
            const double l = 0.2126 * r + 0.7152 * g + 0.0722 * b;
            d[x] = qRgba (int (std::clamp (l + (r - l) * sat, 0.0, 255.0)),
                          int (std::clamp (l + (g - l) * sat, 0.0, 255.0)),
                          int (std::clamp (l + (b - l) * sat, 0.0, 255.0)),
                          qAlpha (d[x]));
        }
    }
    return out;
}

QImage composite (const QImage& capture, double w, double h, double dpr,
                 const Params& p)
{
    if (capture.isNull() || w < 2.0 || h < 2.0) return QImage();

    const int pw = int (std::lround (w * dpr));
    const int ph = int (std::lround (h * dpr));
    QImage out (std::max (1, pw), std::max (1, ph),
                QImage::Format_ARGB32_Premultiplied);
    out.fill (Qt::transparent);
    // left untagged on purpose: callers blit it with explicit rects

    // buffer px per item px (the capture is half res)
    const double scale = capture.width() / w;

    const QImage blurred = saturate (dualGaussian (capture, p.sigmaCss * scale),
                                     p.saturate);

    QPainter g (&out);
    g.setRenderHint (QPainter::SmoothPixmapTransform, true);
    g.setRenderHint (QPainter::Antialiasing, true);
    g.scale (dpr, dpr);

    // ---- shape clip ------------------------------------------------------
    QPainterPath path;
    switch (p.shape)
    {
        case Shape::TrapTop:
            path.moveTo (w * 0.20, 0); path.lineTo (w * 0.80, 0);
            path.lineTo (w, h);        path.lineTo (0, h);
            break;
        case Shape::TrapBottom:
            path.moveTo (0, 0);        path.lineTo (w, 0);
            path.lineTo (w * 0.80, h); path.lineTo (w * 0.20, h);
            break;
        case Shape::Ellipse:
            // nothing to clip: the radial fade below does the shaping
            path.addRect (0, 0, w, h);
            break;
    }
    path.closeSubpath();
    g.setClipPath (path);

    // ---- the blurred capture, scaled back up (bilinear) ------------------
    g.drawImage (QRectF (0, 0, w, h), blurred,
                 QRectF (0, 0, blurred.width(), blurred.height()));

    // ---- .controlpit::before — accent glow tied to the meter -------------
    if (p.accent.isValid() && p.light > 0.001)
    {
        const double cx = w / 2.0;
        const double cy = (p.shape == Shape::TrapTop) ? 0.0 : h;
        const double rad = w * 0.42;
        QRadialGradient gr (QPointF (cx, cy), rad);
        const double a = p.light * 0.36;          // light is already squared
        QColor c0 = p.accent; c0.setAlphaF (std::clamp (a, 0.0, 1.0));
        QColor c1 = p.accent; c1.setAlphaF (std::clamp (a * 0.36, 0.0, 1.0));
        QColor c2 = p.accent; c2.setAlphaF (0.0);
        gr.setColorAt (0.00, c0);
        gr.setColorAt (0.38, c1);
        gr.setColorAt (1.00, c2);
        g.fillRect (QRectF (0, 0, w, h), gr);
    }

    // ---- rgba(255,255,255,.018) veil -------------------------------------
    g.fillRect (QRectF (0, 0, w, h),
                QColor (255, 255, 255, int (std::lround (p.veil * 255.0))));

    // ---- mask-image, seam only -------------------------------------------
    if (p.shape == Shape::Ellipse)
    {
        g.setCompositionMode (QPainter::CompositionMode_DestinationIn);
        QRadialGradient m (QPointF (w / 2.0, h / 2.0),
                           std::max (w, h) * 0.5);
        m.setColorAt (0.00, QColor (0, 0, 0, 255));
        m.setColorAt (0.45, QColor (0, 0, 0, 255));
        m.setColorAt (0.78, QColor (0, 0, 0, 0));
        m.setColorAt (1.00, QColor (0, 0, 0, 0));
        g.setPen (Qt::NoPen);
        g.setBrush (m);
        g.drawRect (QRectF (0, 0, w, h));
    }

    return out;
}

} // namespace OvFrost
