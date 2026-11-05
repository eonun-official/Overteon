import QtQuick

// ---------------------------------------------------------------------------
//  Frosted glass — the Qt counterpart of
//     backdrop-filter: blur(3px) saturate(.85)
//  used by #hdr::before and .controlpit::after in web_fx/index.html.
//
//  This is a real 3-level DUAL-KAWASE pyramid, compiled to .qsb and run on the
//  GPU (Qt 6 only accepts pre-compiled shaders, which is why these live in
//  ui/qt/shaders and are baked at build time):
//
//      content ──► /1 ──► /2 ──► /4 ──► /8      (kawase_down, 3 passes)
//                          ◄──   ◄──   ◄──       (kawase_up: tent + finer level)
//                                        └──► composite (saturate, mask, glow)
//
//  The shaping (trapezoid / feathered ellipse) is done analytically in the
//  composite shader, so the edges stay antialiased at any window scale — the
//  CSS clip-path / mask-image equivalent.
//
//  The HTML front end runs the same algorithm in FrostEngine (canvas, CPU),
//  so both editors blur identically.
// ---------------------------------------------------------------------------
Item {
    id: root

    property Item source: null
    property real radius: 3.0          // CSS blur radius; scales the pyramid
    property real saturation: 0.85     // CSS saturate()
    property real veil: 0.018          // background: rgba(255,255,255,.018)

    // 0 rectangle, 1 trapezoid with the narrow edge up, 2 narrow edge down,
    // 3 feathered ellipse
    property int shape: 0
    property real pitWidth: 0.6        // narrow edge as a fraction of the width
    property real edgeSoftness: 1.0    // antialias width, item px

    property color glowColor: "transparent"
    property real  glowStrength: 0.0
    property bool  glowFromBottom: false

    // blur budget: CSS blur(3px) needs the whole pyramid; tiny radii can skip
    // the deepest level to save two passes
    readonly property int levels: radius >= 2.0 ? 3 : (radius >= 1.0 ? 2 : 1)

    visible: source !== null
    clip: true

    readonly property int w0: Math.max (1, Math.round (width))
    readonly property int h0: Math.max (1, Math.round (height))
    readonly property int w1: Math.max (1, w0 >> 1)
    readonly property int h1: Math.max (1, h0 >> 1)
    readonly property int w2: Math.max (1, w0 >> 2)
    readonly property int h2: Math.max (1, h0 >> 2)
    readonly property int w3: Math.max (1, w0 >> 3)
    readonly property int h3: Math.max (1, h0 >> 3)

    // ---- capture -------------------------------------------------------
    ShaderEffectSource {
        id: lvl0
        sourceItem: root.source
        live: true
        hideSource: false
        recursive: false
        smooth: true
        wrapMode: ShaderEffectSource.ClampToEdge
        textureSize: Qt.size (root.w0, root.h0)
        visible: false
    }

    // ---- downward: 0 -> 1 -> 2 -> 3 ------------------------------------
    ShaderEffect {
        id: down1
        width: root.w1; height: root.h1
        visible: false
        property variant source: lvl0
        property vector2d texel: Qt.vector2d (1.0 / root.w0, 1.0 / root.h0)
        fragmentShader: "qrc:/shaders/kawase_down.frag.qsb"
    }
    ShaderEffectSource { id: s1; sourceItem: down1; live: true; hideSource: true
                         smooth: true; wrapMode: ShaderEffectSource.ClampToEdge; visible: false }

    ShaderEffect {
        id: down2
        width: root.w2; height: root.h2
        visible: false
        property variant source: s1
        property vector2d texel: Qt.vector2d (1.0 / root.w1, 1.0 / root.h1)
        fragmentShader: "qrc:/shaders/kawase_down.frag.qsb"
    }
    ShaderEffectSource { id: s2; sourceItem: down2; live: true; hideSource: true
                         smooth: true; wrapMode: ShaderEffectSource.ClampToEdge; visible: false }

    ShaderEffect {
        id: down3
        width: root.w3; height: root.h3
        visible: false
        property variant source: s2
        property vector2d texel: Qt.vector2d (1.0 / root.w2, 1.0 / root.h2)
        fragmentShader: "qrc:/shaders/kawase_down.frag.qsb"
    }
    ShaderEffectSource { id: s3; sourceItem: down3; live: true; hideSource: true
                         smooth: true; wrapMode: ShaderEffectSource.ClampToEdge; visible: false }

    // ---- upward: 3 -> 2 -> 1 -> 0 --------------------------------------
    ShaderEffect {
        id: up2
        width: root.w2; height: root.h2
        visible: false
        property variant coarse: s3
        property variant fine: s2
        property vector2d texel: Qt.vector2d (1.0 / root.w3, 1.0 / root.h3)
        fragmentShader: "qrc:/shaders/kawase_up.frag.qsb"
    }
    ShaderEffectSource { id: u2; sourceItem: up2; live: true; hideSource: true
                         smooth: true; wrapMode: ShaderEffectSource.ClampToEdge; visible: false }

    ShaderEffect {
        id: up1
        width: root.w1; height: root.h1
        visible: false
        property variant coarse: u2
        property variant fine: s1
        property vector2d texel: Qt.vector2d (1.0 / root.w2, 1.0 / root.h2)
        fragmentShader: "qrc:/shaders/kawase_up.frag.qsb"
    }
    ShaderEffectSource { id: u1; sourceItem: up1; live: true; hideSource: true
                         smooth: true; wrapMode: ShaderEffectSource.ClampToEdge; visible: false }

    // ---- composite -----------------------------------------------------
    ShaderEffect {
        anchors.fill: parent
        property variant blurred: u1
        property real saturation: root.saturation
        property real veil: root.veil
        property real shape: root.shape
        property real pitWidth: root.pitWidth
        property real edgeSoftness: root.edgeSoftness
        property vector2d itemSize: Qt.vector2d (root.w0, root.h0)
        property color glowColor: Qt.rgba (root.glowColor.r, root.glowColor.g,
                                           root.glowColor.b, root.glowStrength)
        property real glowFromBottom: root.glowFromBottom ? 1 : 0
        fragmentShader: "qrc:/shaders/glass.frag.qsb"
    }
}
