import QtQuick

// Borderless text toggle: 6px dot + uppercase label ( .tbtn in the design ).
Item {
    id: root

    property var param: null
    property string label: ""
    property color accent: OvTheme.sc

    implicitWidth: row.implicitWidth
    implicitHeight: 20

    readonly property bool on: param ? param.norm > 0.5 : false

    Row {
        id: row
        anchors.verticalCenter: parent.verticalCenter
        spacing: 7

        Rectangle {
            width: 6
            height: 6
            radius: 3
            anchors.verticalCenter: parent.verticalCenter
            color: root.on ? root.accent : OvTheme.dotOff
            Behavior on color { ColorAnimation { duration: 120 } }

            // box-shadow: 0 0 7px var(--sc) on .tbtn.act::before
            Rectangle {
                anchors.centerIn: parent
                width: 14
                height: 14
                radius: 7
                color: root.accent
                opacity: root.on ? 0.22 : 0.0
                Behavior on opacity { NumberAnimation { duration: 120 } }
            }
        }

        Text {
            id: t
            anchors.verticalCenter: parent.verticalCenter
            text: root.label
            color: root.on ? root.accent : (ma.containsMouse ? OvTheme.ink : OvTheme.faint)
            Behavior on color { ColorAnimation { duration: 120 } }
            font.family: OvTheme.uiFont
            font.pixelSize: 10
            font.letterSpacing: 1.4
        }
    }

    MouseArea {
        id: ma
        anchors.fill: parent
        hoverEnabled: true
        onClicked: {
            if (! root.param) return
            root.param.beginGesture()
            root.param.setNorm (root.param.norm > 0.5 ? 0 : 1)
            root.param.endGesture()
        }
    }
}
