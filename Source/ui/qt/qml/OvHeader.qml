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

    // The elliptical frost on the seam (#hdr::before) is painted by the two
    // OvEqView halves — each one draws its own half of the 550x68 strip, so
    // the blur comes from the same pixels as the plot and cannot fall out of
    // sync.  Nothing is drawn here.

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
        // The art is 64/480 of the width, but its halo is a real Gaussian
        // (stdDeviation 2.5 in viewBox units) that spills past the art — the
        // SVG's #hpod carries overflow:visible for exactly that.  A painted
        // item clips at its own bounds, so the item is 24 px taller at each
        // end with the art centred inside it.
        height: width * 64.0 / 480.0 + 48
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
