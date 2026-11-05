import QtQuick
import OnyxTEON 1.0

// The floating header that sits on the seam between the two EQ halves:
// ONYX|TEON wordmark, the curved meter pod with the harmonic ring, INIT.
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

    // Elliptical frost on the seam (.hdr::before):
    //   backdrop-filter: blur(3px) saturate(.85)
    //   rgba(255,255,255,.018) veil
    //   mask-image: radial-gradient(ellipse at center, #000 0%, #000 45%, transparent 78%)
    // All four are done in one dual-Kawase + composite shader pass, so the rim
    // feathers exactly like the CSS radial mask instead of fading a rectangle.
    OvGlass {
        width: Math.min (550, root.width)
        height: 68
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        visible: root.sourceItem !== null

        source: root.sourceItem
        radius: 3.0
        saturation: 0.85
        veil: 0.018
        shape: 3                 // feathered ellipse
        edgeSoftness: 1.0
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
                text: "ONYX"
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
