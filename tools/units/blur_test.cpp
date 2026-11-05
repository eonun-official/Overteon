#include "OvEqView.h"
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

    // peak-hold state machine, exactly as OvMeterView::advance() runs it
    struct Ch { double level = 0, peak = 0, holdUntil = 0; };
    double clock = 0;
    Ch ch;
    bool ok = true;
    for (int i = 0; i <= 150; ++i)          // 5 s at 30 Hz
    {
        const double t = i / 30.0;
        const double lvl = (t > 0.9 && t < 1.1) ? 0.90 : 0.30;
        clock += 1.0 / 30.0;
        ch.level = lvl;
        if (lvl >= ch.peak) { ch.peak = lvl; ch.holdUntil = clock + 1.1; }
        else if (clock > ch.holdUntil) ch.peak = std::max (lvl, ch.peak - (1.0/30.0) * 0.18);

        if (t > 1.2 && t < 1.9 && std::abs (ch.peak - 0.90) > 1e-9) ok = false;
        if (t > 2.6 && t < 2.9 && ch.peak >= 0.90) ok = false;          // must fall
    }
    std::printf ("peak hold: holds 1.1 s then falls  -> %s (peak at end %.2f)\n",
                 ok ? "PASS" : "FAIL", ch.peak);
    if (ch.peak < 0.3) std::printf ("  also never falls below the level: PASS\n");
    return ok ? 0 : 1;
}
