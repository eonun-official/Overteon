import QtQuick

// ============================================================================
//  GPU frosted glass.
//
//  The CPU path blurs the backdrop in a QImage, which is 1.0-1.5 ms of CPU per
//  surface per repaint.  Here the same separable Gaussian runs as two fragment
//  passes and costs the CPU nothing.
//
//      ShaderEffectSource   capture the region under the glass
//      ShaderEffect  x2     blur.frag horizontally then vertically — the same
//                           dual-Gaussian construction the CPU path uses
//      ShaderEffect         glass.frag: saturate(.85), the .018 veil, the
//                           trapezoid or ellipse mask, the accent glow
//
//  Five things Qt 6 requires and does not report, all found by testing:
//    * a fragment shader needs a PAIRED vertex shader that passes the texcoord
//      through; on its own the UV is undefined and every texture() is zero
//    * a sampler must be declared layout(binding = 0), the same binding as the
//      uniform block; with any other binding the whole effect is skipped
//    * ShaderEffectSource normalises the WHOLE sourceItem to 0..1, so sourceRect
//      is required or the glass samples the wrong part of the picture
//    * sourceRect is in the SOURCE ITEM's coordinates, not this item's
//    * ShaderEffect wants PREMULTIPLIED alpha
// ============================================================================
Item {
    id: root

    property Item   source: null
    // backdrop-filter: blur(3px) is sigma 3 CSS px, and a CSS px is a LOGICAL
    // px, so at 1.302 dpr the browser blurs by 3.9 device px.
    //
    // A ShaderEffectSource does NOT do that on its own: measured with the
    // textureSize() readback, a 550x68 logical strip captured at 550x68 even
    // on a 1.302 dpr screen.  Blurring that and letting the final pass stretch
    // it back to 716x89 gave a glass that was soft *and* whose blur strength
    // was 1.3x too small — the "does not follow DPI" symptom exactly.
    // So the whole chain below runs at DEVICE resolution and sigma scales.
    property real   dpr: (typeof ovDpr !== "undefined" && ovDpr > 0) ? ovDpr : 1.0
    property real   sigmaCss: 3.0
    property real   sigmaScale: (typeof ovGlassSigma !== undefined) ? ovGlassSigma : 1.0
    readonly property real sigmaEff: sigmaCss * dpr * sigmaScale
    property real   veil: 0.018
    property bool   ellipse: false          // #hdr::before uses the radial mask
    property bool   trapTop: true
    property color  accent: "transparent"
    property real   glow: 0.0               // 0..1, already level^2
    // 与 0.9.10 的 .controlpit::before transition: opacity .1s linear 一致。
    Behavior on glow { NumberAnimation { duration: 100; easing.type: Easing.Linear } }
    property real   saturation: 0.85
    /// OVERTEON_NOFROST=1 turns every glass surface off, so a reference capture
    /// really has none.  Without this the "no frost" image still carried the GPU
    /// glass and every A/B against it read as zero difference.
    /// Whether this glass draws at all.  NOT named `enabled`: QQuickItem
    /// already has `enabled`, and a QML item that declares its own shadows the
    /// base property — Qt warns about it on every load ("Member enabled of the
    /// object OvGpuGlass overrides a member of the base object") and the two
    /// then disagree about what the property means.
    property bool   active: true
    visible: active

    readonly property int capW: Math.max (2, Math.round (width))
    readonly property int capH: Math.max (2, Math.round (height))
    // Device-pixel size of the glass; every texture in the chain uses it.
    readonly property int devW: Math.max (2, Math.round (capW * dpr))
    readonly property int devH: Math.max (2, Math.round (capH * dpr))

    // TEMP diagnostic: identify each glass so C++ can dump its sampling window.
    property string dbgName: ""
    objectName: "glass-" + dbgName

    // Where this item sits inside `source`, in the source's own coordinates.
    //
    // This is a plain property updated by the handlers below, NOT a binding.
    // mapToItem() is opaque to QML's dependency tracking: a binding that only
    // names `source` is evaluated once and then never again, so every glass
    // kept the (0, 0) it computed before the layout existed and sampled the
    // TOP-LEFT corner of its source.  That is what put the SC curve into the
    // seam and the control-pit bands.  Recomputing on every geometry change is
    // explicit and cannot go stale.
    property point srcOrigin: Qt.point (0, 0)
    function updateSrcOrigin() {
        if (! root.source) { srcOrigin = Qt.point (0, 0); return }
        const p = root.mapToItem (root.source, 0, 0)
        if (p.x !== srcOrigin.x || p.y !== srcOrigin.y)
            srcOrigin = p
    }
    onSourceChanged:      updateSrcOrigin()
    onXChanged:           updateSrcOrigin()
    onYChanged:           updateSrcOrigin()
    onWidthChanged:       updateSrcOrigin()
    onHeightChanged:      updateSrcOrigin()
    onParentChanged:      updateSrcOrigin()
    Component.onCompleted: updateSrcOrigin()

    // A parent moving (a section's y, a layout re-flow) changes this item's
    // position inside `source` without changing any property of this item, so
    // none of the handlers above fire.  A cheap poll closes that hole; it is
    // two mapToItem calls five times a second.
    Timer {
        interval: 200; running: root.visible && root.source !== null; repeat: true
        onTriggered: root.updateSrcOrigin()
    }

    // ---- the blur itself -------------------------------------------------
    // The chain lives in OvBlur.qml, which the EQ overflow fog uses as well: one
    // construction, so the two cannot disagree about what a blur is.  Non-linear
    // sigma stays here, because it is a property of the glass, not of blurring.
    OvBlur {
        id: chain
        source: root.source
        sourceRect: Qt.rect (root.srcOrigin.x, root.srcOrigin.y,
                             root.capW, root.capH)
        texW: root.devW
        texH: root.devH
        sigma: root.sigmaEff
    }

    // ---- finish: saturate, veil, shape, glow -----------------------------
    ShaderEffect {
        anchors.fill: parent
        property variant src: chain.output
        property real veil: root.veil
        property real glow: root.glow
        property real isEllipse: root.ellipse ? 1.0 : 0.0
        property real trapTop: root.trapTop ? 1.0 : 0.0
        property real saturation: root.saturation
        property real glowR: root.accent.r
        property real glowG: root.accent.g
        property real glowB: root.accent.b
        vertexShader:   "qrc:/shaders/glass.vert.qsb"
        fragmentShader: "qrc:/shaders/glass.frag.qsb"
    }
}
