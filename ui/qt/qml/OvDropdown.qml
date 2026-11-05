import QtQuick

// Borderless text dropdown — .ddbtn / .ddmenu from the design:
// uppercase value + a small triangle, no chrome, menu opens up or down.
//
// The menu is an in-scene item (see OvMenu) so it scales and positions with
// the rest of the design; Qt Popup would not.
Item {
    id: root

    property var param: null
    property var options: []          // optional override, else param.choices
    property bool openDown: false
    property color accent: OvTheme.pq
    property color textColor: OvTheme.ink
    property color labelColor: OvTheme.faint
    property string label: ""
    property bool dimmed: false
    property real minWidth: 58

    signal picked (int index)

    implicitWidth: row.implicitWidth
    implicitHeight: 20

    opacity: dimmed ? 0.28 : 1.0
    enabled: ! dimmed

    readonly property var items: options.length > 0 ? options
                                                    : (param ? param.choices : [])
    readonly property int index: param ? Math.round (param.norm * Math.max (0, items.length - 1)) : 0
    readonly property int rowHeight: 24
    readonly property int menuWidth: 84

    Row {
        id: row
        anchors.verticalCenter: parent.verticalCenter
        spacing: 6

        Text {
            visible: root.label !== ""
            text: root.label
            color: root.labelColor
            font.family: OvTheme.uiFont
            font.pixelSize: 9
            anchors.verticalCenter: parent.verticalCenter
        }

        Item {
            id: btn
            width: Math.max (root.minWidth, btnText.implicitWidth + 10)
            height: 20
            anchors.verticalCenter: parent.verticalCenter

            Text {
                id: btnText
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: 2
                text: {
                    const it = root.items
                    const i = Math.max (0, Math.min (it.length - 1, root.index))
                    return (it.length > 0 ? it[i] : "") + (root.openDown ? " ▾" : " ▴")
                }
                color: (mouse.containsMouse || menu.visible) ? root.accent : root.textColor
                font.family: OvTheme.uiFont
                font.pixelSize: 10
            }

            MouseArea {
                id: mouse
                anchors.fill: parent
                hoverEnabled: true
                onClicked: menu.visible ? OvMenu.close() : root.openMenu()
            }
        }
    }

    function openMenu() {
        if (OvMenu.host === null) return
        const p = root.mapToItem (OvMenu.host, 0, 0)
        const x = Math.max (2, Math.min (OvMenu.host.width - menuWidth - 2, p.x))
        const y = root.openDown ? p.y + root.height + 2 : p.y - items.length * rowHeight - 8
        OvMenu.show (menu, x, Math.max (2, y))
    }

    // ---- the menu itself (reparented to the design root) ----------------
    Item {
        id: menu
        visible: false
        width: root.menuWidth
        height: root.items.length * root.rowHeight + 6

        Rectangle {
            anchors.fill: parent
            color: OvTheme.menuBg
            border.color: OvTheme.line
            border.width: 1
            radius: 5
        }

        Column {
            x: 3
            y: 3
            Repeater {
                model: root.items
                delegate: Item {
                    id: itemRow
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
                        anchors.left: parent.left
                        anchors.leftMargin: 10
                        text: itemRow.modelData
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
                                root.param.setNorm (root.items.length > 1
                                    ? itemRow.index / (root.items.length - 1) : 0)
                                root.param.endGesture()
                            }
                            root.picked (itemRow.index)
                            OvMenu.close()
                        }
                    }
                }
            }
        }

        Component.onCompleted: if (OvMenu.host) parent = OvMenu.host
    }
}
