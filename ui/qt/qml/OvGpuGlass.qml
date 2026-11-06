import QtQuick

// ============================================================================
//  GPU frosted glass.
//
//  The CPU path blurs the backdrop in a QImage: correct, but it is the largest
//  item in the plot's frame budget (1.0-1.5 ms of CPU per surface per repaint).
//  Here the same separable Gaussian runs as two fragment passes and costs the
//  CPU nothing.
//
//  Chain, all on the GPU:
//     ShaderEffectSource  capture the content underneath at half resolution
//                         (the glass destroys detail anyway, so the extra
//                         pixels cannot be seen and this quarters the work)
//     ShaderEffect  x2    blur.frag horizontally, then vertically — the same
//                         dual-Gaussian construction the CPU path uses
//     ShaderEffect        glass.frag: saturate(.85), the .018 veil, the
//                         trapezoid or ellipse mask, and .controlpit::before
//
//  Two things Qt 6 requires and does not warn about, both learned the hard way:
//    * a fragment shader needs a PAIRED vertex shader that passes the texcoord
//      through; with only a fragment shader the UV is undefined and every
//      texture() returns nothing
//    * a sampler must be declared with `layout(binding = 0)`, the same binding
//      as the uniform block — with any other binding the whole effect is
//      silently skipped
// ============================================================================
Item {
    id: root

    property Item   source: null
    property real   sigmaCss: 3.0          // backdrop-filter: blur(3px)
    property real   veil: 0.018
    property bool   ellipse: false          // #hdr::before uses the radial mask
    property bool   trapTop: true
    property color  accent: "transparent"
    property real   glow: 0.0               // 0..1, already level^2
    property real   saturation: 0.85

    /// The capture is half size, so a CSS sigma of 3 is 1.5 here.
    readonly property real halfSigma: sigmaCss * 0.5
    readonly property int  halfW: Math.max (2, Math.round (width  * 0.5))
    readonly property int  halfH: Math.max (2, Math.round (height * 0.5))

    // hideSource so the glass does not capture itself
    ShaderEffectSource {
        id: capture
        sourceItem: root.source
        hideSource: false
        live: true
        recursive: false
        smooth: true
        width: root.halfW
        height: root.halfH
    }

    // ---- horizontal pass ---------------------------------------------------
    ShaderEffect {
        id: passH
        width: root.halfW
        height: root.halfH
        // NOT visible:false — an invisible item is not rendered, so a
        // ShaderEffectSource over it captures an empty texture and the whole
        // chain silently produces nothing.  These passes are drawn first and
        // are covered by the final composite.
        visible: true
        property variant src: capture
        property vector2d texelStep: Qt.vector2d (1.0 / root.halfW, 0.0)
        property real sigma: root.halfSigma
        vertexShader:   "qrc:/shaders/blur.vert.qsb"
        fragmentShader: "qrc:/shaders/blur.frag.qsb"
    }

    ShaderEffectSource {
        id: afterH
        sourceItem: passH
        live: true
        smooth: true
        width: root.halfW
        height: root.halfH
    }

    // ---- vertical pass -----------------------------------------------------
    ShaderEffect {
        id: passV
        width: root.halfW
        height: root.halfH
        visible: true
        property variant src: afterH
        property vector2d texelStep: Qt.vector2d (0.0, 1.0 / root.halfH)
        property real sigma: root.halfSigma
        vertexShader:   "qrc:/shaders/blur.vert.qsb"
        fragmentShader: "qrc:/shaders/blur.frag.qsb"
    }

    ShaderEffectSource {
        id: blurred
        sourceItem: passV
        live: true
        smooth: true
        width: root.halfW
        height: root.halfH
    }

    // ---- finish: saturate, veil, shape, glow -------------------------------
    ShaderEffect {
        anchors.fill: parent
        property variant src: blurred
        property vector2d sizePx: Qt.vector2d (width, height)
        property real veil: root.veil
        property real glow: root.glow
        property color glowColor: root.accent
        property real isEllipse: root.ellipse ? 1.0 : 0.0
        property real trapTop: root.trapTop ? 1.0 : 0.0
        property real saturation: root.saturation
        vertexShader:   "qrc:/shaders/glass.vert.qsb"
        fragmentShader: "qrc:/shaders/glass.frag.qsb"
    }
}
