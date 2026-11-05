import QtQuick

// .rangebadge — borderless "±12 dB ▾" token with a right-aligned menu,
// drawn in-scene (see OvMenu) so it follows the design scale.
Item {
    id: root

    property var param: null
    property var options: []
    property bool openDown: false
    property color accent: OvTheme.sc
    property bool dimmed: false

    implicitWidth: badge.implicitWidth + 14
    implicitHeight: 20
    opacity: dimmed ? 0.22 : 1.0
    enabled: ! dimmed

    readonly property int index: param ? Math.round (param.norm * Math.max (0, options.length - 1)) : 0
    readonly property int rowHeight: 24
    readonly property int menuWidth: 92

    Rectangle {
        anchors.fill: parent
        radius: 3
        color: (ma.containsMouse || menu.visible) ? Qt.rgba (1, 1, 1, 0.045) : "transparent"
    }

    Text {
        id: badge
        anchors.centerIn: parent
        text: {
            const i = Math.max (0, Math.min (root.options.length - 1, root.index))
            return root.options.length > 0 ? root.options[i] + " ▾" : ""
        }
        color: ma.containsMouse ? OvTheme.ink : OvTheme.dim
        font.family: OvTheme.uiFont
        font.pixelSize: 10
    }

    MouseArea {
        id: ma
        anchors.fill: parent
        hoverEnabled: true
        onClicked: menu.visible ? OvMenu.close() : root.openMenu()
    }

    function openMenu() {
        if (OvMenu.host === null) return
        const p = root.mapToItem (OvMenu.host, 0, 0)
        const x = Math.max (2, Math.min (OvMenu.host.width - menuWidth - 2,
                                         p.x + root.width - menuWidth))
        const y = root.openDown ? p.y + root.height + 2 : p.y - options.length * rowHeight - 8
        OvMenu.show (menu, x, Math.max (2, y))
    }

    Item {
        id: menu
        visible: false
        width: root.menuWidth
        height: root.options.length * root.rowHeight + 6

        Rectangle {
            anchors.fill: parent
            color: OvTheme.menuBg
            border.color: OvTheme.line
            radius: 5
        }

        Column {
            x: 3
            y: 3
            Repeater {
                model: root.options
                delegate: Item {
                    id: row
                    required property int index
                    required property string modelData
                    width: root.menuWidth - 6
                    height: root.rowHeight

                    Rectangle {
                        anchors.fill: parent
                        color: hov.containsMouse ? OvTheme.panel : "transparent"
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.right: parent.right
                        anchors.rightMargin: 10
                        text: row.modelData
                        color: hov.containsMouse ? OvTheme.ink : OvTheme.dim
                        font.family: OvTheme.uiFont
                        font.pixelSize: 10
                    }
                    MouseArea {
                        id: hov
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: {
                            if (root.param) {
                                root.param.beginGesture()
                                root.param.setNorm (root.options.length > 1
                                    ? row.index / (root.options.length - 1) : 0)
                                root.param.endGesture()
                            }
                            OvMenu.close()
                        }
                    }
                }
            }
        }

        Component.onCompleted: if (OvMenu.host) parent = OvMenu.host
    }
}
