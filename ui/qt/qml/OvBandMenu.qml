import QtQuick

// Right-click menu on an EQ node: the 8-way filter type + Remove, the
// .ctxmenu of the design.  In-scene item (see OvMenu) so it scales with the
// rest of the UI.
Item {
    id: root

    property var engine: null
    property string prefix: "sc"
    property int band: 0
    property color accent: OvTheme.sc
    property string currentType: ""

    signal choose (string typeName)

    visible: false
    readonly property int rowHeight: 24
    readonly property int menuWidth: 116

    function types () {
        return prefix === "sc"
            ? [ "Bell", "Notch", "Low Shelf", "High Shelf",
                "Low Cut 12", "Low Cut 24", "High Cut 12", "High Cut 24" ]
            : [ "Bell", "Low Shelf", "High Shelf",
                "Low Cut 12", "Low Cut 24", "High Cut 12", "High Cut 24" ]
    }

    readonly property var typeList: types()
    readonly property int slotCount: typeList.length + 2      // + separator + remove

    height: slotCount * rowHeight + 6
    width: menuWidth

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
            model: root.typeList
            delegate: Item {
                id: row
                required property int index
                required property string modelData
                width: root.menuWidth - 6
                height: root.rowHeight

                Rectangle {
                    anchors.fill: parent
                    color: (hov.containsMouse || root.currentType === row.modelData)
                           ? OvTheme.panel : "transparent"
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    text: row.modelData
                    color: (hov.containsMouse || root.currentType === row.modelData)
                           ? OvTheme.ink : OvTheme.dim
                    font.family: OvTheme.uiFont
                    font.pixelSize: 10
                }
                MouseArea {
                    id: hov
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: { root.choose (row.modelData); OvMenu.close() }
                }
            }
        }
    }

    // separator + Remove, pinned below the type rows
    Rectangle {
        x: 3
        y: 3 + root.typeList.length * root.rowHeight
        width: root.menuWidth - 6
        height: 1
        color: OvTheme.line
    }

    Item {
        x: 3
        y: 4 + root.typeList.length * root.rowHeight
        width: root.menuWidth - 6
        height: root.rowHeight

        Rectangle {
            anchors.fill: parent
            color: rm.containsMouse ? OvTheme.panel : "transparent"
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: 10
            text: "✕ Remove"
            color: root.accent
            font.family: OvTheme.uiFont
            font.pixelSize: 10
        }
        MouseArea {
            id: rm
            anchors.fill: parent
            hoverEnabled: true
            onClicked: { root.choose ("Remove"); OvMenu.close() }
        }
    }

    Component.onCompleted: if (OvMenu.host) parent = OvMenu.host
}
