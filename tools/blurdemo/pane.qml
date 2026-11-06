import QtQuick

// ONE image under everything.  On the right half a separate blur layer sits on
// top of it — the same picture, blurred, clipped to that half.  The seam is
// therefore the blur appearing over the image, not two copies side by side.
Item {
    id: root
    width: 1040; height: 772

    property string photo: ""
    property real   sigma: 6.0
    readonly property int halfW: Math.round (width / 2)

    Rectangle { anchors.fill: parent; color: "#000000" }

    // ---- the image, whole window, untouched ------------------------------
    Image {
        id: base
        anchors.fill: parent
        source: root.photo.length ? "file://" + root.photo : ""
        sourceClipRect: Qt.rect (1200, 700, root.width, root.height)
        sourceSize: Qt.size (3840, 2160)
        fillMode: Image.Pad
        smooth: true
        visible: false
    }

    ShaderEffectSource {
        id: sharpLayer
        anchors.fill: parent
        sourceItem: base
        live: true
    }

    // ---- the blur layer: the same image, blurred, over the right half ----
    // The passes run over the WHOLE image so the right half's pixels stay in
    // place; the last item clips the result to the right half only.
    ShaderEffectSource {
        id: cap
        anchors.fill: parent
        sourceItem: base
        live: true
        visible: false
    }

    ShaderEffect {
        id: passH
        x: 0; y: 0
        width: root.width; height: root.height
        visible: false          // intermediate only
        property variant src: cap
        property real stepX: 1.0 / root.width
        property real stepY: 0.0
        property real sigma: root.sigma
        vertexShader:   "qrc:/shaders/blur.vert.qsb"
        fragmentShader: "qrc:/shaders/blur.frag.qsb"
    }

    ShaderEffectSource {
        id: mid
        x: 0; y: 0
        width: root.width; height: root.height
        sourceItem: passH
        live: true
        visible: false
    }

    ShaderEffect {
        id: passV
        x: 0; y: 0
        width: root.width; height: root.height
        visible: false          // drawn through the clipped layer below
        property variant src: mid
        property real stepX: 0.0
        property real stepY: 1.0 / root.height
        property real sigma: root.sigma
        vertexShader:   "qrc:/shaders/blur.vert.qsb"
        fragmentShader: "qrc:/shaders/blur.frag.qsb"
    }

    // ---- the blur layer itself: only the right half shows ----------------
    // sourceRect is in the SOURCE ITEM's coordinates, and the source effect's
    // own origin is not the window's: asking for (halfW,0) put the seam at
    // x≈166 instead of 520.  Map the window-x through the item instead.
    ShaderEffectSource {
        id: layer
        x: root.halfW; y: 0
        width: root.halfW; height: root.height
        sourceItem: passV
        sourceRect: Qt.rect (passV.mapFromItem (root, root.halfW, 0).x,
                             0, root.halfW, root.height)
        live: true
    }
}
