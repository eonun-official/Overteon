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
    property real   sigmaCss: 3.0           // backdrop-filter: blur(3px)
    property real   veil: 0.018
    property bool   ellipse: false          // #hdr::before uses the radial mask
    property bool   trapTop: true
    property color  accent: "transparent"
    property real   glow: 0.0               // 0..1, already level^2
    property real   saturation: 0.85

    readonly property int capW: Math.max (2, Math.round (width))
    readonly property int capH: Math.max (2, Math.round (height))

    // Where this item sits inside `source`, in the source's own coordinates.
    readonly property point srcOrigin: root.source
        ? root.mapToItem (root.source, 0, 0) : Qt.point (0, 0)

    // ---- capture the region under the glass ------------------------------
    ShaderEffectSource {
        id: capture
        width: root.capW; height: root.capH
        sourceItem: root.source
        sourceRect: Qt.rect (root.srcOrigin.x, root.srcOrigin.y,
                             root.capW, root.capH)
        live: true
        recursive: false
        smooth: true
        visible: false
    }

    // ---- horizontal pass -------------------------------------------------
    ShaderEffect {
        id: passH
        x: 0; y: 0
        width: root.capW; height: root.capH
        visible: false                    // intermediate, read by afterH
        property variant src: capture
        property real stepX: 1.0 / root.capW
        property real stepY: 0.0
        property real sigma: root.sigmaCss
        vertexShader:   "qrc:/shaders/blur.vert.qsb"
        fragmentShader: "qrc:/shaders/blur.frag.qsb"
    }

    ShaderEffectSource {
        id: afterH
        width: root.capW; height: root.capH
        sourceItem: passH
        live: true
        smooth: true
        visible: false
    }

    // ---- vertical pass ---------------------------------------------------
    ShaderEffect {
        id: passV
        x: 0; y: 0
        width: root.capW; height: root.capH
        visible: false
        property variant src: afterH
        property real stepX: 0.0
        property real stepY: 1.0 / root.capH
        property real sigma: root.sigmaCss
        vertexShader:   "qrc:/shaders/blur.vert.qsb"
        fragmentShader: "qrc:/shaders/blur.frag.qsb"
    }

    ShaderEffectSource {
        id: blurred
        width: root.capW; height: root.capH
        sourceItem: passV
        live: true
        smooth: true
        visible: false
    }

    // ---- finish: saturate, veil, shape, glow -----------------------------
    ShaderEffect {
        anchors.fill: parent
        property variant src: blurred
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
