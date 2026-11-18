#pragma once
// ============================================================================
//  Timing.
//
//  One number governs everything: 120 frames per second, which is the highest
//  rate the UI is ever asked to present.  There are no other rate limits
//  anywhere in the editor — no 30 Hz plot timer, no 60 Hz meter timer, no
//  separate poll rate.  Every repainting item and both polling timers run at
//  this rate, so an input reaches the screen in one frame and cannot be
//  delayed by a slower sub-system.
//
//  This matters most while dragging an EQ node: with a 30 Hz plot timer the
//  drag was rendered on the timer's next tick, so the node lagged the cursor by
//  up to 33 ms before any drawing cost was counted.
//
//  Work that is not needed every frame is still skipped, but by proving there
//  is nothing to draw (a dirty check), not by a slower clock.
// ============================================================================
namespace OvTiming
{
    constexpr int kFrameHz = 120;
    constexpr int kFrameMs = 1000 / kFrameHz;      // 8 ms

    // Every consumer uses these; they exist so a call site reads clearly, not
    // to allow a different rate.
    // The spectrum is no longer a 33 ms slide show: the analyser reads the
    // newest samples from a continuous ring on every engine tick (16 ms), so
    // the plots are allowed to repaint at 60 Hz like the display they sit in.
    // The plot's own layers are cached, and only the spectrum outline is
    // rebuilt, so this is one vertex strip per plot per frame.
    constexpr int kPlotHz   = 60;
    constexpr int kPlotMs   = 1000 / kPlotHz;
    // Meter data still arrives at 33 ms in the design (the page's mock pushes
    // `meter` on a 33 ms timer), so the surfaces that only show that data are
    // refreshed at 30 Hz.  Anything the user touches — a drag, a knob —
    // repaints from its own signal in the frame it happens, and the WINDOW
    // still presents at kFrameHz.
    constexpr int kMeterHz  = 30;
    // The engine's poll runs the source's analysis — for the mock that is a
    // 2048-point FFT.  At kFrameHz that was 120 FFTs a second to feed displays
    // that refresh at 30, so the cost landed in the callback rather than in the
    // paint.  60 Hz keeps host automation responsive and halves it.
    constexpr int kPollHz   = 60;
    constexpr int kPollMs   = kFrameMs;
}

#include "OvEqView.h"
#include "OvFogSource.h"
#include "OvFrameProbe.h"
#include "OvMeterView.h"
#include "OvSeamFrost.h"
#include "OvBlurTest.h"

inline void ovRegisterQmlTypes()
{
    static bool done = false;
    if (done) return;
    done = true;

    qmlRegisterType<OvEqView>   ("Overteon", 1, 0, "OvEqView");
    qmlRegisterType<OvFogSource> ("Overteon", 1, 0, "OvFogSource");
    qmlRegisterType<OvFrameProbe>("Overteon", 1, 0, "OvFrameProbe");
    qmlRegisterType<OvMeterView>("Overteon", 1, 0, "OvMeterView");
    qmlRegisterType<OvSeamFrost>("Overteon", 1, 0, "OvSeamFrost");
    qmlRegisterType<OvBlurTest>("Overteon", 1, 0, "OvBlurTest");
}
