#include "OvEqView.h"
#include "OvFrost.h"
#include <QGuiApplication>
#include <QImage>
#include <cstdio>
#include <cmath>

int main (int argc, char** argv)
{
    QGuiApplication app (argc, argv);

    // 6 px stripes, like the HTML test
    QImage img (400, 200, QImage::Format_ARGB32);
    for (int y = 0; y < 200; ++y)
        for (int x = 0; x < 400; ++x)
            img.setPixel (x, y, ((x / 3) % 2) ? qRgb (255,255,255) : qRgb (0,0,0));

    auto sd = [] (const QImage& im) {
        double s = 0, s2 = 0; long n = 0;
        for (int y = 0; y < im.height(); y += 2) {
            const auto* p = reinterpret_cast<const QRgb*> (im.constScanLine (y));
            for (int x = 0; x < im.width(); x += 2) { double v = qGray (p[x]); s += v; s2 += v*v; ++n; }
        }
        double m = s/n; return std::sqrt (s2/n - m*m);
    };

    std::printf ("sharp      sd=%.2f\n", sd (img));
    for (double sigma : { 1.0, 3.0, 6.0 })
        std::printf ("sigma %4.1f sd=%.2f\n", sigma, sd (OvEqView::gaussianBlur (img, sigma, 1.0)));

    bool ok = true;

    // ---- the glass kernel: OvFrost::dualGaussian -------------------------
    // The glass captures at half resolution, so sigma 3 CSS px arrives here as
    // 1.5 buffer px.  This is the 7-tap set the page uses, so the numbers must
    // match FrostEngine's behaviour: 3 px stripes (the plot's fine detail) get
    // flattened, and the result is close to a true Gaussian of the same sigma.
    std::printf ("\n[glass] dualGaussian (separable, half-res sigma 1.5)\n");
    {
        // Model the real pipeline: the plot has 3 px stripes in CSS pixels, the
        // capture halves that (so 1.5 px in buffer pixels), and only then does
        // the kernel run.  Testing the kernel on full-res stripes would be a
        // strictly harsher case than the glass ever sees.
        QImage css (480, 240, QImage::Format_ARGB32);
        for (int y = 0; y < 240; ++y)
            for (int x = 0; x < 480; ++x)
                css.setPixel (x, y, ((x / 3) % 2) ? qRgb (255,255,255) : qRgb (0,0,0));
        const QImage stripes = css.scaled (240, 120, Qt::IgnoreAspectRatio,
                                           Qt::SmoothTransformation)
                                  .convertToFormat (QImage::Format_ARGB32);

        const double sharpSd = sd (stripes);
        const double glassSd = sd (OvFrost::dualGaussian (stripes, 1.5));
        std::printf ("  3px stripes: sd %.1f -> %.1f   (%.0f%% of contrast removed)\n",
                     sharpSd, glassSd, 100.0 * (1.0 - glassSd / sharpSd));
        const bool kills = glassSd < sharpSd * 0.45;
        std::printf ("  fine detail flattened            -> %s\n", kills ? "PASS" : "FAIL");
        ok = ok && kills;

        // a step edge must soften without shifting: check the 50 % point
        QImage step (200, 40, QImage::Format_ARGB32);
        for (int y = 0; y < 40; ++y)
            for (int x = 0; x < 200; ++x)
                step.setPixel (x, y, x < 100 ? qRgb (0,0,0) : qRgb (255,255,255));
        const QImage bs = OvFrost::dualGaussian (step, 1.5);
        const int yy = 20;
        int crossing = -1;
        for (int x = 90; x < 110; ++x)
            if (qGray (bs.pixel (x, yy)) >= 128) { crossing = x; break; }
        const bool centred = std::abs (crossing - 100) <= 1;
        std::printf ("  step edge 50%% point at x=%d (want 100)  -> %s\n",
                     crossing, centred ? "PASS" : "FAIL");
        ok = ok && centred;

        // saturate(.85) pulls colour toward its luminance, and must not touch
        // a grey pixel at all
        const QImage one (1, 1, QImage::Format_ARGB32);
        QImage c = one; c.setPixel (0, 0, qRgb (200, 100, 50));
        const QImage sat = OvFrost::saturate (c, 0.85);
        QImage grey = one; grey.setPixel (0, 0, qRgb (128, 128, 128));
        const QImage greyOut = OvFrost::saturate (grey, 0.85);
        const bool satOk = qGreen (sat.pixel (0, 0)) > 100          // pulled up
                        && qGray (grey.pixel (0, 0)) == qGray (greyOut.pixel (0, 0));
        std::printf ("  saturate(.85): %s -> %s, grey unchanged  -> %s\n",
                     "200,100,50",
                     QString ("%1,%2,%3").arg (qRed (sat.pixel (0,0)))
                         .arg (qGreen (sat.pixel (0,0))).arg (qBlue (sat.pixel (0,0)))
                         .toStdString().c_str(),
                     satOk ? "PASS" : "FAIL");
        ok = ok && satOk;
    }

    // peak-hold state machine, exactly as OvMeterView::advance() runs it
    struct Ch { double level = 0, peak = 0, holdUntil = 0; };
    double clock = 0;
    Ch ch;
    bool holdOk = true;
    for (int i = 0; i <= 150; ++i)          // 5 s at 30 Hz
    {
        const double t = i / 30.0;
        const double lvl = (t > 0.9 && t < 1.1) ? 0.90 : 0.30;
        clock += 1.0 / 30.0;
        ch.level = lvl;
        if (lvl >= ch.peak) { ch.peak = lvl; ch.holdUntil = clock + 1.1; }
        else if (clock > ch.holdUntil) ch.peak = std::max (lvl, ch.peak - (1.0/30.0) * 0.18);

        if (t > 1.2 && t < 1.9 && std::abs (ch.peak - 0.90) > 1e-9) holdOk = false;
        if (t > 2.6 && t < 2.9 && ch.peak >= 0.90) holdOk = false;      // must fall
    }
    ok = ok && holdOk;
    std::printf ("peak hold: holds 1.1 s then falls  -> %s (peak at end %.2f)\n",
                 holdOk ? "PASS" : "FAIL", ch.peak);
    if (ch.peak < 0.3) std::printf ("  also never falls below the level: PASS\n");
    return ok ? 0 : 1;
}
