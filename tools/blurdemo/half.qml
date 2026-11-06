import QtQuick
import OnyxTEON 1.0

// Left half: CPU dual-Gaussian.  Right half: GPU two-pass.
// Both show the SAME crop of the same photo at the same size, so the two halves
// can be subtracted pixel for pixel.
Item {
    id: root
    width: 1040; height: 772

    property string photo: "/home/x_x/BingWallpaper/20241105-ShiShiBeach_ZH-CN8685799566_UHD.jpg"
    property real   sigma: 6.0
    readonly property int halfW: Math.round (width / 2)
    readonly property int ph:    height                // 772
    readonly property point crop: Qt.point (1200, 700) // a detailed area

    Rectangle { anchors.fill: parent; color: "black" }

    // ---------- LEFT: CPU ----------
    OvBlurTest {
        x: 0; y: 0
        width: root.halfW; height: root.ph
        file: root.photo
        sigma: root.sigma
        cropOrigin: root.crop
    }

    // ---------- RIGHT: GPU ----------
    // The same crop, so both halves hold identical pixels before blurring.
    Image {
        id: srcGpu
        x: root.halfW; y: 0
        width: root.halfW; height: root.ph
        source: "file://" + root.photo
        sourceClipRect: Qt.rect (root.crop.x, root.crop.y, root.halfW, root.ph)
        sourceSize: Qt.size (3840, 2160)
        fillMode: Image.Pad
        smooth: true
        visible: false
    }

    ShaderEffectSource {
        id: capGpu
        x: root.halfW; y: 0
        width: root.halfW; height: root.ph
        sourceItem: srcGpu
        live: true
    }

    ShaderEffect {
        id: passH
        x: root.halfW; y: 0
        width: root.halfW; height: root.ph
        property variant src: capGpu
        property real stepX: 1.0 / root.halfW
        property real stepY: 0.0
        property real sigma: root.sigma
        vertexShader:   "qrc:/shaders/blur.vert.qsb"
        fragmentShader: "qrc:/shaders/blur.frag.qsb"
    }

    ShaderEffectSource {
        id: midGpu
        x: root.halfW; y: 0
        width: root.halfW; height: root.ph
        sourceItem: passH
        live: true
    }

    ShaderEffect {
        id: passV
        x: root.halfW; y: 0
        width: root.halfW; height: root.ph
        property variant src: midGpu
        property real stepX: 0.0
        property real stepY: 1.0 / root.ph
        property real sigma: root.sigma
        vertexShader:   "qrc:/shaders/blur.vert.qsb"
        fragmentShader: "qrc:/shaders/blur.frag.qsb"
    }

    ShaderEffectSource {
        x: root.halfW; y: 0
        width: root.halfW; height: root.ph
        sourceItem: passV
        live: true
    }
}
