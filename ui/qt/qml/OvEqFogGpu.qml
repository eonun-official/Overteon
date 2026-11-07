import QtQuick
import OnyxTEON 1.0

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
    /// fitScale of the design box (main.qml): design px -> window px.
    property real   fitScale: 1.0
    /// Device pixel ratio the texture is built at.
    property real   dpr: (typeof ovDpr !== "undefined" && ovDpr > 0) ? ovDpr : 1.0
    /// The fog's reach, in design px / on SCREEN.  Passed in because it is
    /// divided by fitScale, i.e. it is a fixed size on screen rather than a
    /// length in the design (see main.qml).
    property real   range: 224
    /// Half the gap between the two plot edges, in ITEM pixels.  Comes from the
    /// views because the padding rule (min(24, h*0.2)) is theirs.
    property real   edgeGap: 24
    property bool   enabled: true
    /// ONYXTEON_FOGRAW=1: show the unblurred strokes.
    property bool   raw: (typeof ovFogRaw !== "undefined") ? ovFogRaw : false

    visible: enabled && topView !== null && bottomView !== null

    readonly property int devW: Math.max (2, Math.round (width  * dpr))
    readonly property int devH: Math.max (2, Math.round (height * dpr))

    OvFogSource {
        id: srcItem
        anchors.fill: parent
        topView: root.topView
        bottomView: root.bottomView
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
        sigma: 0.4
        sigmaSlope: 6.05
        fade: true

        // Always live.  A three-stage ShaderEffectSource chain driven by
        // scheduleUpdate() did not reliably fill in (measured: the fog never
        // appeared at all), and a chain that is always live cannot be stale —
        // which is what a window being dragged needs.
        live: true
        exposeOutput: ! root.raw
    }

    // ONYXTEON_FOGRAW: the unblurred strokes, to check the source on its own.
    ShaderEffectSource {
        anchors.fill: parent
        visible: root.raw
        sourceItem: srcItem
        textureSize: Qt.size (root.devW, root.devH)
        live: true
        smooth: true
    }
}
