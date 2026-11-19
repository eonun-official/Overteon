import QtQuick
import Overteon 1.0

// ============================================================================
//  The EQ overflow fog, on the GPU, as ONE surface.
//
//  OvFogSource paints both halves' outgoing curves into a single texture — the
//  parts of the EQ curves that have left their plots — and OvBlur blurs it with
//  the same dual-Gaussian chain the central glass uses, the radius growing
//  continuously with the distance from the seam.
//
//  TWO halves as separate items was the bug the user diagnosed: each had its own
//  texture and its own chain, and while a window was being dragged they did not
//  present in the same frame.  The upper half showed a new frame and the lower
//  one the previous frame, and the mismatch reads as tearing exactly at the
//  crossing.  One surface cannot be inconsistent with itself.
//
//  Geometry: the surface spans the seam, half a reach each way.  The gradient
//  runs outward from the seam centre in both directions, which is what the two
//  plots' edges are — the upper curve escapes down from its bottom edge, the
//  lower escapes up from its top edge, and those edges are the same seam.
// ============================================================================
Item {
    id: root

    property OvEqView topView: null
    property OvEqView bottomView: null
    /// Device pixel ratio the texture is built at.
    property real   dpr: (typeof ovDpr !== "undefined" && ovDpr > 0) ? ovDpr : 1.0
    /// The fog's reach, in DESIGN px (the page's 224).  It scales with the
    /// design, exactly like the curve, so the overflow covers the same number
    /// of dB at every window size.
    property real   range: 224
    /// Half the gap between the two plot edges, in ITEM pixels.  Comes from the
    /// views because the padding rule (min(24, h*0.2)) is theirs.
    property real   edgeGap: 24
    /// See OvGpuGlass: `enabled` is QQuickItem's own property, so the fog uses
    /// its own name instead of shadowing it.
    property bool   active: true
    /// OVERTEON_FOGRAW=1: show the unblurred strokes.
    property bool   raw: (typeof ovFogRaw !== "undefined") ? ovFogRaw : false

    visible: active && topView !== null && bottomView !== null

    /// The surface's own paint rate, for OVERTEON_SOLO.
    readonly property alias paintFps: srcItem.paintFps

    // srcItem emits the curve as QSGGeometry, so it renders on whatever RHI
    // backend Qt is using — no QPainter, no OpenGL requirement.

    readonly property int devW: Math.max (2, Math.round (width  * dpr))
    readonly property int devH: Math.max (2, Math.round (height * dpr))

    // The strokes, captured and then HIDDEN.
    //
    // Two things had to be true and only one could be at a time:
    //
    //   * the raw strokes must not be drawn, or they are composited on top of
    //     their own blurred copy and the fog reads as a hard line with a faint
    //     halo — which is exactly how it looked: "it never blurs";
    //   * a hidden item is NOT rendered, so `visible: false` alone leaves the
    //     texture empty and the fog disappears completely (measured: 0 pink
    //     pixels anywhere).
    //
    // ShaderEffectSource::hideSource is the answer: it keeps capturing the item
    // while taking it out of the scene.
    OvFogSource {
        id: srcItem
        anchors.fill: parent
        topView: root.topView
        bottomView: root.bottomView
    }

    // Keeps srcItem out of the scene while still capturing it.
    //
    // This exists because of what a capture is NOT: hideSource on the same
    // ShaderEffectSource that feeds the blur does not work — the item is dropped
    // from the render pass, nothing renders it into that capture, and the blur
    // reads an empty texture (measured: the fog vanished completely).  Taking it
    // out of the scene has to be a separate capture from the one the blur
    // consumes, so this one hides the item and the blur makes its own.
    //
    // It is parked off-screen rather than made invisible: a ShaderEffectSource
    // draws its own texture wherever it sits, which would composite the sharp
    // strokes over their blurred copy, and `visible: false` stops it rendering
    // at all.
    ShaderEffectSource {
        id: srcTex
        x: root.raw ? 0 : -100000
        y: 0
        visible: true
        sourceItem: srcItem
        hideSource: true
        live: true
        smooth: true
        width: root.width
        height: root.height
        sourceRect: Qt.rect (0, 0, root.width, root.height)
        textureSize: Qt.size (root.devW, root.devH)
    }

    OvBlur {
        id: chain
        source: srcItem
        sourceRect: Qt.rect (0, 0, root.width, root.height)

        // Half resolution for the texture, full size for the layout: the fog is
        // a low-frequency smear, so the difference is invisible and it costs a
        // quarter of the blur work.  OvBlur converts sigma for the density.
        itemW: root.width
        itemH: root.height
        texW: Math.max (2, Math.round (root.devW / 2))
        texH: Math.max (2, Math.round (root.devH / 2))

        // The gradient, in the surface's own units.  The seam centre is the
        // middle of this item by construction; the gap and the reach come from
        // the plots and from main.qml.
        fogMid: 0.5
        fogGap: root.height > 0 ? (root.edgeGap / root.height) : 0.0
        // The reach, less the gap the two edges already cover — the surface is
        // centred on the seam and spans a reach each way.
        fogRange: root.height > 0
                  ? ((root.height / 2 - root.edgeGap) / root.height) : 1.0
        // The blur is in DESIGN pixels and grows with the distance from the
        // plot edge, exactly like 0.9.10's twelve bands:
        //   band 0 (first ~19 px past the edge) -> filter: none
        //   ...
        //   band 11 (at the 224 px reach)      -> blur(6.45 px)
        // Because the whole design scales, a smaller window has fewer screen
        // pixels of overflow, hence a smaller blur: the excess gets clearer,
        // not softer.  A screen-constant sigma was the opposite of that.
        sigma: 0.0
        sigmaSlope: 6.45
        // true: the fade is applied to the BLURRED result, exactly like the
        // reference's per-band `globalAlpha` after its blur filter.  That keeps
        // the solid core of the stroke; pre-fading the segments and blurring
        // afterwards (the old `false` here) made the fog look dimmer and softer
        // than 0.9.10.
        fade: true

        // LIVE ONLY WHILE THE STROKES CHANGE.
        //
        // `live: true` re-ran two full-surface Gaussian passes every frame for a
        // picture that only changes when a curve does — the crossing region's
        // own cost, and the reason it sat at ~2 fps while the spectrum (which
        // does change every frame) ran at 107.
        //
        // The earlier attempt at a non-live chain failed for a different reason:
        // scheduleUpdate() on a three-stage ShaderEffectSource chain did not
        // propagate in dependency order, so the capture rendered into an empty
        // intermediate and the fog vanished.  The fix is to hold the chain live
        // for a few frames after each change — long enough for every stage to
        // fill — and then let it go quiet.
        live: settle.running
        exposeOutput: ! root.raw
    }

    // Runs for a moment after any change, then stops.  4 frames at 60 Hz is
    // enough for capture -> H -> V to fill in order; after that an idle fog
    // costs nothing.
    Timer {
        id: settle
        interval: 70; repeat: false
    }
    function wake() { settle.restart() }

    // The strokes are rebuilt by OvFogSource only when the curve moved, and that
    // is the one event that can change this picture.
    Connections {
        target: srcItem
        function onRevisionChanged() { settle.restart() }
    }
    onWidthChanged:   settle.restart()
    onHeightChanged:  settle.restart()
    onYChanged:       settle.restart()

    // OVERTEON_FOGRAW: the unblurred strokes, to check the source on its own.

}
