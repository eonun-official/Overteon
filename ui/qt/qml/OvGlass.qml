import QtQuick
import QtQuick.Effects

// ---------------------------------------------------------------------------
//  Frosted glass — the Qt counterpart of the CSS
//     backdrop-filter: blur(3px) saturate(.85)
//  used by #hdr::before and .controlpit::after in web_fx/index.html.
//
//  This is a REAL Gaussian blur, not a tint: the item underneath is rendered
//  into an off-screen texture and blurred by Qt's built-in multi-pass effects
//  shader (the same downsampled Gaussian chain Excel/blur "full" mode uses),
//  then composited back with a saturation lift.
//
//  NOTE: Qt 6 ships only pre-compiled shaders, so runtime GLSL (the old
//  fragmentShader strings) is not available.  MultiEffect is used here
//  because it is the supported, full-quality multi-pass blur in Qt 6.
//  Shaping is done by the caller: the trapezoid panels are cut out by opaque
//  polygons painted on top (exactly like .eqhdr::before/::after in the CSS),
//  and the soft ellipse uses a radial fade.
// ---------------------------------------------------------------------------
Item {
    id: root

    property Item source: null
    property real radius: 3.0          // CSS-equivalent blur radius, px
    property real saturation: 0.85     // CSS saturate()
    property real strength: 1.0        // 0..1 overall opacity of the glass

    visible: source !== null
    clip: true

    MultiEffect {
        anchors.fill: parent
        source: root.source
        visible: root.source !== null

        blurEnabled: true
        blurMax: 16
        // MultiEffect normalises blur to blurMax; CSS blur(r) is a gaussian
        // with sigma = r, whose visible radius is about 3r, hence the factor.
        blur: Math.max (0.05, Math.min (1.0, root.radius / 16.0 * 3.0))

        // MultiEffect uses offsets: 0 = unchanged, so 0.85 -> -0.15
        saturation: root.saturation - 1.0
        brightness: 0.0
        contrast: 0.0

        autoPaddingEnabled: false
        opacity: root.strength
    }
}
