#pragma once
// ============================================================
//  Overteon Qt/QML UI — the design's frosted glass.
//
//  web_fx/index.html does not use backdrop-filter for the glass: plug-in hosts
//  ignore it, so the page computes the blur itself in FrostEngine.  This is the
//  same pipeline, in the same order, so the Qt front end and the page produce
//  the same glass rather than merely a similar one:
//
//    1. capture what is under the glass at HALF resolution, on a #16181d base
//    2. dual Gaussian, sigma 3 CSS px expressed in buffer px (so 1.5)
//    3. saturate(0.85) around the luminance
//    4. upscale back to CSS size with bilinear filtering
//    5. clip to the shape (trapezoid, or a rect for the seam ellipse)
//    6. accent glow tied to the meter   (.controlpit::before)
//    7. the rgba(255,255,255,.018) veil (.controlpit::after / #hdr::before)
//    8. destination-in radial mask, seam only (mask-image)
//
//  Half resolution is not an optimisation detail — it is why the glass looks
//  like frosted glass instead of a slightly out-of-focus copy.  Fine detail is
//  gone before the blur runs, so the result has no residual structure to
//  resolve, which is exactly what a real backdrop blur does.
// ============================================================
#include <QColor>
#include <QImage>
#include <QRectF>

namespace OvFrost
{
    enum class Shape
    {
        TrapTop,      // .controlpit.top::after     — wide at the bottom
        TrapBottom,   // .controlpit.bottom::after  — wide at the top
        Ellipse       // #hdr::before               — radial mask, no polygon
    };

    struct Params
    {
        Shape  shape     = Shape::TrapBottom;
        QColor accent;                  // invalid => no glow
        double light     = 0.0;         // 0..1, the meter level (squared inside)
        double sigmaCss  = 3.0;         // CSS blur(3px)
        double saturate  = 0.85;        // CSS saturate(.85)
        double veil      = 0.018;       // rgba(255,255,255,.018)
    };

    /// A half-resolution capture buffer with the design's background colour.
    /// `w`, `h` are the glass size in item (CSS) pixels.
    QImage beginCapture (double w, double h);

    /// Draw one source image into the capture.  `dest` is the source's
    /// rectangle in the glass's own coordinates; the capture scales it by its
    /// own buffer/size ratio, exactly as FrostEngine's `ctx.drawImage(...)`.
    void blitInto (QImage& capture, const QImage& src, const QRectF& dest);

    /// Steps 2..8.  Returns an image in item coordinates at `dpr` (tagged 1.0,
    /// so callers blit it with explicit rects and nothing is scaled twice).
    QImage composite (const QImage& capture, double w, double h, double dpr,
                      const Params& p);

    /// The 7-tap separable kernel used by FrostEngine.
    ///
    /// Shape taken from X-PostProcessing-Library's DualGaussianBlur
    /// (QianMo/X-PostProcessing-Library, Effects/DualGaussianBlur): one pass
    /// per axis, taps 0, ±1, ±2, ±6 with weights .40 .15 .10 .05 summing to 1.
    /// Its variance is 2·(0.15·1² + 0.10·2² + 0.05·6²)·offset² = 4.70·offset²,
    /// so a requested sigma becomes offset = sigma / sqrt(4.70).
    /// Written from that description; no third-party code copied.
    ///
    /// Steps are rounded to whole pixels and clamped at the edge, which is what
    /// the page does.  At the sigma the glass actually uses two taps land on
    /// the same step and their weights add — harmless, and reproduced here so
    /// the two renderers match rather than merely both being "a blur".
    QImage dualGaussian (const QImage& src, double sigma);

    /// CSS saturate(): pull each channel toward its luminance.
    QImage saturate (const QImage& src, double sat);
}
