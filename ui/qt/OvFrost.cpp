#include "OvFrost.h"

#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QRadialGradient>
#include <algorithm>
#include <vector>
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
    if (src.isNull() || sigma <= 0.0) return src;

    const QImage in = src.convertToFormat (QImage::Format_ARGB32_Premultiplied);
    const int w = in.width(), h = in.height();
    if (w < 2 || h < 2) return in;

    // ---- taps -----------------------------------------------------------
    //  A separable Gaussian is two 1-D passes, and each 1-D Gaussian is
    //  symmetric, so the whole kernel is a centre weight plus one weight per
    //  integer offset out to 3 sigma (where the tail is below 0.3 %).
    //
    //  The first version of this used X-PostProcessing-Library's fixed 7-tap
    //  set (0, +-1, +-2, +-6 at .40 .15 .10 .05).  Its variance is right, but
    //  the taps are ROUNDED to whole pixels, so for a given sigma they land on
    //  positions with gaps between them.  Over the big glass surfaces that is
    //  invisible; on a thin 4 px ring it is not — the profile came out as
    //  140 140 25 0 12 12 0 0 …, i.e. the halo's tail broke off a few pixels
    //  from the stroke instead of fading smoothly like feGaussianBlur.
    //
    //  So the taps are derived from sigma directly.  It is still exactly the
    //  dual (two-pass) Gaussian — same construction, same variance — only
    //  without the rounding artefacts.
    const int radius = std::max (1, int (std::ceil (3.0 * sigma)));
    const double inv2s2 = 1.0 / (2.0 * sigma * sigma);
    std::vector<double> wt ((size_t) radius + 1);
    double sum = 0.0;
    for (int i = 0; i <= radius; ++i)
    {
        wt[(size_t) i] = std::exp (-double (i) * double (i) * inv2s2);
        sum += (i == 0) ? wt[0] : 2.0 * wt[(size_t) i];
    }
    for (double& v : wt) v /= sum;          // taps sum to 1, so no gain change

    QImage tmp (w, h, QImage::Format_ARGB32_Premultiplied);
    QImage out (w, h, QImage::Format_ARGB32_Premultiplied);

    // ---- horizontal pass ----
    for (int y = 0; y < h; ++y)
    {
        const QRgb* s = reinterpret_cast<const QRgb*> (in.constScanLine (y));
        QRgb* d = reinterpret_cast<QRgb*> (tmp.scanLine (y));
        for (int x = 0; x < w; ++x)
        {
            double r = qRed (s[x]) * wt[0];
            double g = qGreen (s[x]) * wt[0];
            double b = qBlue (s[x]) * wt[0];
            double a = qAlpha (s[x]) * wt[0];
            for (int k = 1; k <= radius; ++k)
            {
                const QRgb l = clampFetch (s, w, x - k);
                const QRgb rr = clampFetch (s, w, x + k);
                const double t = wt[(size_t) k];
                r += (qRed (l)   + qRed (rr))   * t;
                g += (qGreen (l) + qGreen (rr)) * t;
                b += (qBlue (l)  + qBlue (rr))  * t;
                a += (qAlpha (l) + qAlpha (rr)) * t;
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
        std::vector<const QRgb*> up ((size_t) radius + 1), dn ((size_t) radius + 1);
        for (int k = 0; k <= radius; ++k)
        {
            up[(size_t) k] = reinterpret_cast<const QRgb*> (
                                 tmp.constScanLine (std::clamp (y - k, 0, h - 1)));
            dn[(size_t) k] = reinterpret_cast<const QRgb*> (
                                 tmp.constScanLine (std::clamp (y + k, 0, h - 1)));
        }
        QRgb* d = reinterpret_cast<QRgb*> (out.scanLine (y));
        for (int x = 0; x < w; ++x)
        {
            const QRgb c = up[0][x];
            double r = qRed (c) * wt[0];
            double g = qGreen (c) * wt[0];
            double b = qBlue (c) * wt[0];
            double a = qAlpha (c) * wt[0];
            for (int k = 1; k <= radius; ++k)
            {
                const QRgb u = up[(size_t) k][x];
                const QRgb v = dn[(size_t) k][x];
                const double t = wt[(size_t) k];
                r += (qRed (u)   + qRed (v))   * t;
                g += (qGreen (u) + qGreen (v)) * t;
                b += (qBlue (u)  + qBlue (v))  * t;
                a += (qAlpha (u) + qAlpha (v)) * t;
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
    //
    //   .controlpit.top::before{
    //     background: radial-gradient(ellipse 70% 160% at 50% 0%,
    //                 accent .36 0%, accent .13 38%, accent 0 78%);
    //     opacity: var(--light) }
    //
    // "ellipse 70% 160%" means rx = 0.70 * width and ry = 1.60 * height — in
    // this 640x32 box a 448x51 ellipse, not a circle.  It was drawn as a
    // circle of radius 0.42*w, so it was both too narrow (269 instead of 448)
    // and far too tall for the strip, and the last stop was at 100 % where the
    // CSS finishes at 78 %, so the falloff was stretched as well.
    if (p.accent.isValid() && p.light > 0.001)
    {
        const double rx = w * 0.70;
        const double ry = h * 1.60;
        const double cx = w / 2.0;
        const double cy = (p.shape == Shape::TrapTop) ? 0.0 : h;

        g.save();
        g.translate (cx, cy);
        g.scale (rx, ry);                     // unit circle -> the CSS ellipse
        QRadialGradient gr (QPointF (0, 0), 1.0);
        const double a = p.light * 0.36;      // light is already squared
        QColor c0 = p.accent; c0.setAlphaF (std::clamp (a, 0.0, 1.0));
        QColor c1 = p.accent; c1.setAlphaF (std::clamp (a * (0.13 / 0.36), 0.0, 1.0));
        QColor c2 = p.accent; c2.setAlphaF (0.0);
        gr.setColorAt (0.00, c0);
        gr.setColorAt (0.38, c1);
        gr.setColorAt (0.78, c2);             // CSS finishes here, not at 100 %
        gr.setColorAt (1.00, c2);
        g.setPen (Qt::NoPen);
        g.setBrush (gr);
        g.drawRect (QRectF (-1, -1, 2, 2));
        g.restore();
    }

    // ---- rgba(255,255,255,.018) veil -------------------------------------
    g.fillRect (QRectF (0, 0, w, h),
                QColor (255, 255, 255, int (std::lround (p.veil * 255.0))));

    // ---- mask-image, seam only -------------------------------------------
    //
    //   mask-image: radial-gradient(ellipse at center,
    //               #000 0%, #000 45%, transparent 78%)
    //
    // ELLIPSE, not a circle.  A QRadialGradient is circular, so drawing it at
    // radius max(w,h)/2 (=275 in a 550x68 box) left the vertical direction
    // only 34/275 = 12 % of the ramp used: the mask never reached its 45 %
    // stop vertically, so the top and bottom edges stayed fully opaque and the
    // glass looked like a hard-edged rectangle.
    //
    // The gradient is therefore drawn in a unit circle and the painter is
    // scaled to the box, which is what the CSS ellipse is: radii w/2 and h/2
    // (farthest-corner from the centre).
    if (p.shape == Shape::Ellipse)
    {
        g.setCompositionMode (QPainter::CompositionMode_DestinationIn);
        // NO explicit size in the CSS, so the gradient's ending shape is
        // farthest-corner: an ellipse of the box's aspect ratio that passes
        // through a corner.  For a box of half-size (w/2, h/2) that is
        //     rx = w/2 * sqrt(2),  ry = h/2 * sqrt(2)
        //     test: (w/2/rx)^2 + (h/2/ry)^2 = 1/2 + 1/2 = 1
        //
        // Using (w/2, h/2) directly made the fade finish 29 % too early: at the
        // strip's top and bottom edge the mask was already at 100 % transparent
        // instead of about 22 % opaque, so the glass shrank to a small blob in
        // the middle and the surface looked like it was not there at all.
        const double kCorner = std::sqrt (2.0);
        g.save();
        g.translate (w / 2.0, h / 2.0);
        g.scale (w / 2.0 * kCorner, h / 2.0 * kCorner);
        QRadialGradient m (QPointF (0, 0), 1.0);
        m.setColorAt (0.00, QColor (0, 0, 0, 255));
        m.setColorAt (0.45, QColor (0, 0, 0, 255));
        m.setColorAt (0.78, QColor (0, 0, 0, 0));
        m.setColorAt (1.00, QColor (0, 0, 0, 0));
        g.setPen (Qt::NoPen);
        g.setBrush (m);
        g.drawRect (QRectF (-1, -1, 2, 2));
        g.restore();
    }

    return out;
}

} // namespace OvFrost
