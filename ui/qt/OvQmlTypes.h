#pragma once
// ============================================================
//  OnyxTEON Qt/QML UI — QML type registration.
//
//  The two painted items are registered under "OnyxTEON 1.0" so the QML
//  files can `import OnyxTEON 1.0` (both the plugin editor and the preview
//  app call this once, before loading main.qml).
// ============================================================
#include <QtQml/qqml.h>

// The UI refreshes at the display rate.  Every repainting item and every
// polling core timer uses this, so the EQ plots, the meter pod and the
// parameter polling stay in step (they used to be 30, 30 and 33 ms apart,
// which produced a stuttering pod and 125 fps plots).
namespace OvTiming
{
    constexpr int kFrameHz    = 120;
    constexpr int kFrameMs    = 1000 / kFrameHz;
    // the pod is 550x73 with 4 strokes + a ring; 60 Hz is its data rate
    constexpr int kMeterHz    = 60;
    // parameters are polled less often than the screen refreshes: 60 Hz is
    // plenty for host automation and costs nothing
    constexpr int kPollHz     = 60;
    constexpr int kPollMs     = 1000 / kPollHz;

    // The EQ plot redraws its spectrum and glow, and the engine only produces
    // spectrum at kPollHz/2.  Repainting faster re-uploads a 1040x386 texture
    // for identical pixels: measured 85 % cpu uncapped vs 20 % at this rate,
    // with no visual difference (the spectrum is the only moving part).
    constexpr int kPlotHz     = 30;
    constexpr int kPlotMs     = 1000 / kPlotHz;

    // The spectrum is the only part of a plot that moves; the engine produces
    // it at kPollHz/2 = 30 Hz.  Everything else is cached.  Rendering the plot
    // faster than this re-uploads a 1040x386 texture for identical pixels.
    constexpr int kSpectrumHz = 30;
}

#include "OvEqView.h"
#include "OvMeterView.h"
#include "OvSeamFrost.h"

inline void ovRegisterQmlTypes()
{
    static bool done = false;
    if (done) return;
    done = true;

    qmlRegisterType<OvEqView>   ("OnyxTEON", 1, 0, "OvEqView");
    qmlRegisterType<OvMeterView>("OnyxTEON", 1, 0, "OvMeterView");
    qmlRegisterType<OvSeamFrost>("OnyxTEON", 1, 0, "OvSeamFrost");
}
