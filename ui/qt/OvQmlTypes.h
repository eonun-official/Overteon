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
    constexpr int kPlotHz   = kFrameHz;
    constexpr int kPlotMs   = kFrameMs;
    constexpr int kMeterHz  = kFrameHz;
    constexpr int kPollHz   = kFrameHz;
    constexpr int kPollMs   = kFrameMs;
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
