import QtQuick

// ============================================================================
//  The dual-Gaussian blur chain, in one place.
//
//  Everything that blurs a backdrop in this UI goes through here: the seam
//  frost and the control pits (OvGpuGlass) and the EQ overflow fog
//  (OvEqFogGpu).  It is the same construction in all of them, which is the
//  point — the fog's tearing was a second, differently-built blur that only
//  approximated the first.
//
//      ShaderEffectSource   capture the region to blur, at DEVICE resolution
//      ShaderEffect         blur.frag, axis 0: horizontal
//      ShaderEffect         blur.frag, axis 1: vertical
//      ShaderEffectSource   `output` — what the caller samples
//
//  Device resolution matters: a ShaderEffectSource captures at LOGICAL size
//  unless textureSize is set (measured — a 550x68 strip stayed 550x68 on a
//  1.302 dpr screen).  Blurring that and scaling it back up gives a glass that
//  is soft AND whose blur radius is 1.3x too small: the "does not follow DPI"
//  symptom.  So texW/texH are device pixels and sigma is in device pixels.
//
//  sigmaSlope and fade are what the fog needs and the glass does not use:
//  sigmaSlope grows the radius with the distance from the surface's edge (the
//  fog's falloff), and fade applies the fog's alpha ramp in the same pass.  The
//  glass leaves both at 0 and gets exactly the plain blur.
// ============================================================================
Item {
    id: root

    /// The item to blur.
    property Item   source: null
    /// The region of `source` to capture, in the SOURCE's coordinates.
    /// ShaderEffectSource normalises the whole sourceItem to 0..1, so without
    /// this the wrong part of the picture is sampled.
    property rect   sourceRect: Qt.rect (0, 0, 0, 0)
    /// Capture size, in DEVICE pixels.
    property int    texW: 2
    property int    texH: 2
    /// The size of the region being blurred, in the source's own units.  It
    /// equals texW/texH for the glass, which renders at device resolution; a
    /// caller may instead want a LOWER resolution texture for the same layout,
    /// which is what the EQ fog does — it is a low-frequency smear, so half
    /// resolution is indistinguishable and costs a quarter of the work.
    ///
    /// sigma is given in the same units as itemW/itemH and converted here, so a
    /// caller never has to know the texture's density.
    property real   itemW: texW
    property real   itemH: texH
    /// Blur radius in DEVICE pixels, and how much it grows over the surface's
    /// height (0 = a plain, uniform blur).
    property real   sigma: 1.0
    property real   sigmaSlope: 0.0
    /// Texture texels per item unit.  BOTH sigma and sigmaSlope are given in
    /// the caller's units and converted here — converting only sigma left the
    /// gradient in texels while the base was in item units, so the slope came
    /// out at the wrong scale and the fog barely grew.
    readonly property real texScale: texH / Math.max (1, itemH)
    /// The fog's gradient geometry, normalised to this surface's height:
    /// the seam centre, half the gap between the two plot edges, and the fog's
    /// reach.  Only used when sigmaSlope or fade is set; the glass leaves them
    /// at 0/0/1 and never looks at the result.
    property real   fogMid: 0.0
    property real   fogGap: 0.0
    property real   fogRange: 1.0
    /// Apply the fog's (1 - t) ^ 1.15 alpha ramp.
    property bool   fade: false
    /// `live` re-blurs every frame.  That is right for a source that is itself
    /// animating (the plots, which repaint with the spectrum) and wrong for one
    /// that only changes when a parameter moves — the fog.  A live chain costs
    /// two full-surface Gaussian passes per frame per surface, which is what
    /// made the fog the slowest thing in the window.  With live off, call
    /// scheduleUpdate() when the content or the geometry changed.
    property bool   live: true
    property bool   smooth: true

    /// Keep capturing the source but take it OUT of the scene.
    ///
    /// A ShaderEffectSource draws its own texture wherever it sits, so a source
    /// item that is also captured on purpose would be composited twice: once
    /// sharp, once blurred.  hideSource is Qt's answer — the item still renders
    /// into this capture and is not drawn again.
    property bool   hideSource: false
    /// Show the unblurred capture itself (a raw view, for debugging).
    property bool   showInput: false

    /// Recompute the whole chain.  Safe to call every frame: Qt coalesces it.
    function scheduleUpdate() {
        capture.scheduleUpdate()
        afterH.scheduleUpdate()
        blurred.scheduleUpdate()
    }

    // Deliberately visible: the helpers inside are all visible:false, so this
    // item draws nothing unless exposeOutput is set.  Hiding the item itself
    // would also hide the result when a caller does want it drawn.
    /// Show the result as this component's own drawing instead of only
    /// exposing it for sampling.  The fog needs that: it IS the picture, while
    /// the glass goes on to composite the blur with a mask and a veil.
    property bool   exposeOutput: false
    /// The blurred result.  Sample this; it is an opaque ShaderEffectSource.
    readonly property alias output: blurred
    /// The UNBLURRED capture, for a raw view.
    readonly property alias input: capture

    ShaderEffectSource {
        id: capture
        width: root.itemW; height: root.itemH
        sourceItem: root.source
        sourceRect: root.sourceRect
        textureSize: Qt.size (root.texW, root.texH)
        live: root.live
        recursive: false
        smooth: root.smooth
        hideSource: root.hideSource
        // Only drawn for a raw view; normally it is an intermediate.
        visible: root.showInput
    }

    ShaderEffect {
        id: passH
        width: root.itemW; height: root.itemH
        visible: false                    // intermediate, read by passV
        property variant src: capture
        property real sigma: root.sigma * root.texScale
        property real axis: 0.0
        property real sigmaSlope: root.sigmaSlope * root.texScale
        property real alphaMul: 0.0       // the fade goes on the last pass only
        property real fogMid: root.fogMid
        property real fogGap: root.fogGap
        property real fogRange: root.fogRange
        vertexShader:   "qrc:/shaders/blur.vert.qsb"
        fragmentShader: "qrc:/shaders/blur.frag.qsb"
    }

    ShaderEffectSource {
        id: afterH
        width: root.itemW; height: root.itemH
        sourceItem: passH
        textureSize: Qt.size (root.texW, root.texH)
        live: root.live
        smooth: root.smooth
        visible: false
    }

    ShaderEffect {
        id: passV
        width: root.itemW; height: root.itemH
        visible: false
        property variant src: afterH
        property real sigma: root.sigma * root.texScale
        property real axis: 1.0
        property real sigmaSlope: root.sigmaSlope * root.texScale
        property real alphaMul: root.fade ? 1.0 : 0.0
        property real fogMid: root.fogMid
        property real fogGap: root.fogGap
        property real fogRange: root.fogRange
        vertexShader:   "qrc:/shaders/blur.vert.qsb"
        fragmentShader: "qrc:/shaders/blur.frag.qsb"
    }

    ShaderEffectSource {
        id: blurred
        x: 0; y: 0
        width:  root.itemW
        height: root.itemH
        sourceItem: passV
        textureSize: Qt.size (root.texW, root.texH)
        live: root.live
        smooth: root.smooth
        visible: root.exposeOutput
    }
}
