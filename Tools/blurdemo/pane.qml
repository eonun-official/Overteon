import QtQuick
import Overteon 1.0

// 2x2 in ONE image file:
//   left column   the photo, untouched
//   right column  the same photo, with a blur layer over its RIGHT HALF only
//                 top = GPU two-pass,  bottom = CPU OvFrost::dualGaussian
//
// Each cell is identical, so the four compare directly.  Only the right half of
// each right-hand cell carries blur: the seam in the middle of the cell is what
// makes the blur visible.
//
// Every intermediate item is hidden and every visible one has an explicit x/y —
// a ShaderEffectSource is a single item at a single place, so an unpositioned
// one lands at 0,0 and paints over the top-left cell.
Item {
    id: root
    width: 1280; height: 960

    property string photo: ""
    property real   sigma: 6.0
    property point  crop: Qt.point (1200, 700)
    readonly property int cellW: Math.round (width / 2)     // 640
    readonly property int cellH: Math.round (height / 2)    // 480
    readonly property int halfW: Math.round (cellW / 2)     // 320
    readonly property point cropR: Qt.point (crop.x + halfW, crop.y)

    Rectangle { anchors.fill: parent; color: "#16181d" }

    // ---- the four sharp cells, straight from the photo ------------------
    component Cell: Image {
        width: root.cellW
        height: root.cellH
        source: root.photo.length ? "file://" + root.photo : ""
        sourceClipRect: Qt.rect (root.crop.x, root.crop.y, root.cellW, root.cellH)
        sourceSize: Qt.size (3840, 2160)
        fillMode: Image.Pad
        smooth: true
    }

    Cell { x: 0;               y: 0 }
    Cell { x: root.cellW;      y: 0 }
    Cell { x: 0;               y: root.cellH }
    Cell { x: root.cellW;      y: root.cellH }

    // ===================== TOP-RIGHT: GPU blur over the right half =======
    Image {
        id: gpuSrc
        width: root.cellW; height: root.cellH
        source: root.photo.length ? "file://" + root.photo : ""
        sourceClipRect: Qt.rect (root.crop.x, root.crop.y, root.cellW, root.cellH)
        sourceSize: Qt.size (3840, 2160)
        fillMode: Image.Pad
        smooth: true
        visible: false
    }
    ShaderEffectSource {
        id: gpuCap
        width: root.cellW; height: root.cellH
        sourceItem: gpuSrc
        live: true
        visible: false
    }
    ShaderEffect {
        id: gpuH
        width: root.cellW; height: root.cellH
        visible: false
        property variant src: gpuCap
        property real sigma: root.sigma
        property real axis: 0.0
        vertexShader:   "qrc:/shaders/blur.vert.qsb"
        fragmentShader: "qrc:/shaders/blur.frag.qsb"
    }
    ShaderEffectSource {
        id: gpuMid
        width: root.cellW; height: root.cellH
        sourceItem: gpuH
        live: true
        visible: false
    }
    ShaderEffect {
        id: gpuV
        width: root.cellW; height: root.cellH
        visible: false
        property variant src: gpuMid
        property real sigma: root.sigma
        property real axis: 1.0
        vertexShader:   "qrc:/shaders/blur.vert.qsb"
        fragmentShader: "qrc:/shaders/blur.frag.qsb"
    }
    // visible: the blurred cell's right half, over the top-right cell
    ShaderEffectSource {
        x: root.cellW + root.halfW; y: 0
        width: root.halfW; height: root.cellH
        sourceItem: gpuV
        sourceRect: Qt.rect (root.halfW, 0, root.halfW, root.cellH)
        live: true
    }

    // ===================== BOTTOM-RIGHT: CPU blur over the right half ====
    OvBlurTest {
        x: root.cellW + root.halfW; y: root.cellH
        width: root.halfW; height: root.cellH
        file: root.photo
        sigma: root.sigma
        cropOrigin: root.cropR
    }
}
