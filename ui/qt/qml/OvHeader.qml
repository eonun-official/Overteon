import QtQuick
import Overteon 1.0

// The floating header that sits on the seam between the two EQ halves:
// OVER|TEON wordmark, the curved meter pod with the harmonic ring, INIT.
// The soft frost behind the pod is .hdr::before in the design: an elliptical
// backdrop blur of the EQ canvas underneath.
Item {
    id: root

    property var engine: null
    property Item sourceItem: null      // the EQ layer to frost
    property bool schranz: false

    signal aboutRequested()

    height: OvTheme.headerH
    width: parent ? parent.width : 0

    // Elliptical frost on the seam (.hdr::before): a Gaussian blur of the EQ
    // canvas, feathered out with a radial fade in the background colour so it
    // melts into the grid the way the CSS mask-image does.
    Item {
        id: frost
        width: Math.min (550, root.width)
        height: 68
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        visible: root.sourceItem !== null

        OvGlass {
            anchors.fill: parent
            source: root.sourceItem
            radius: 3.0
            saturation: 0.85
        }

        // light white veil (rgba(255,255,255,.018) in the design)
        Rectangle {
            anchors.fill: parent
            color: Qt.rgba (1, 1, 1, 0.018)
        }

        // radial feather: keep the glass solid in the middle, fade to the
        // page background at the rim
        Canvas {
            id: feather
            anchors.fill: parent
            onPaint: {
                const ctx = getContext ("2d")
                ctx.reset()
                const w = width, h = height
                const g = ctx.createRadialGradient (w/2, h/2, 0, w/2, h/2, Math.max (w, h) * 0.5)
                const bg = OvTheme.bg
                const c = "rgba(" + Math.round (bg.r*255) + "," + Math.round (bg.g*255) + "," + Math.round (bg.b*255)
                g.addColorStop (0.00, c + ",0)")
                g.addColorStop (0.45, c + ",0)")
                g.addColorStop (0.78, c + ",1)")
                g.addColorStop (1.00, c + ",1)")
                ctx.fillStyle = g
                ctx.fillRect (0, 0, w, h)
            }
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            Component.onCompleted: requestPaint()
        }
    }

    // ---- wordmark ------------------------------------------------------
    Item {
        id: logo
        anchors.left: parent.left
        anchors.leftMargin: 32
        anchors.verticalCenter: parent.verticalCenter
        width: logoRow.width
        height: logoRow.height

        Row {
            id: logoRow
            Text {
                text: "OVER"
                color: OvTheme.ink
                font.family: OvTheme.displayFont
                font.pixelSize: 18
                font.letterSpacing: 2
            }
            Text {
                text: "TEON"
                color: OvTheme.sc
                font.family: OvTheme.displayFont
                font.pixelSize: 18
                font.letterSpacing: 2
            }
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: root.aboutRequested()
        }
    }

    // ---- meter pod -----------------------------------------------------
    OvMeterView {
        id: pod
        width: Math.min (550, root.width * 0.55)
        height: width * 64.0 / 480.0
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        engine: root.engine
    }

    // ---- INIT ----------------------------------------------------------
    Text {
        id: initBtn
        anchors.right: parent.right
        anchors.rightMargin: 16
        anchors.verticalCenter: parent.verticalCenter
        text: "INIT"
        color: initMa.containsMouse ? OvTheme.ink : OvTheme.dim
        font.family: OvTheme.uiFont
        font.pixelSize: 10
        font.letterSpacing: 1.5

        MouseArea {
            id: initMa
            anchors.fill: parent
            anchors.margins: -4
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: if (root.engine) root.engine.initAll()
        }
    }
}
