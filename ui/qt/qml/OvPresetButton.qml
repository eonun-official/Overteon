import QtQuick

// Preset trigger in the SIDECHAIN header pit: current preset name, then a
// FACTORY / USER listing plus LOAD… / SAVE AS… (the #presetMenu of the
// design, flattened to one column).  In-scene menu, see OvMenu.
Item {
    id: root

    property var engine: null
    property color accent: OvTheme.sc
    readonly property string current: engine ? engine.presetName : "default"

    implicitWidth: btnText.implicitWidth + 8
    implicitHeight: 20

    readonly property int rowHeight: 24
    readonly property int menuWidth: 156

    function entries () {
        const out = []
        const p = (root.engine && root.engine.presets) ? root.engine.presets : ({})
        for (const g of [ [ "FACTORY", "Factory" ], [ "USER", "User" ] ]) {
            out.push ({ kind: "header", text: g[0], h: 22 })
            const list = p[g[1]] || []
            if (list.length === 0) out.push ({ kind: "empty", text: "(EMPTY)", h: 20 })
            for (const n of list) out.push ({ kind: "preset", folder: g[1], text: n, h: rowHeight })
        }
        out.push ({ kind: "sep", text: "", h: 1 })
        out.push ({ kind: "action", act: "load", text: "LOAD…", h: rowHeight })
        out.push ({ kind: "action", act: "save", text: "SAVE AS…", h: rowHeight })
        return out
    }

    readonly property var entryList: entries()

    function menuHeight() {
        let h = 6
        for (const e of entryList) h += e.h
        return h
    }

    Text {
        id: btnText
        anchors.centerIn: parent
        text: root.current
        color: (ma.containsMouse || menu.visible) ? root.accent : OvTheme.faint
        font.family: OvTheme.uiFont
        font.pixelSize: 10
        elide: Text.ElideRight
        width: Math.min (implicitWidth, 168)
    }

    MouseArea {
        id: ma
        anchors.fill: parent
        hoverEnabled: true
        onClicked: {
            if (root.engine) root.engine.refreshPresets()
            if (menu.visible) { OvMenu.close() } else { root.openMenu() }
        }
    }

    function openMenu() {
        if (OvMenu.host === null) return
        const p = root.mapToItem (OvMenu.host, 0, 0)
        const x = Math.max (2, Math.min (OvMenu.host.width - menuWidth - 2,
                                          p.x + root.width / 2 - menuWidth / 2))
        OvMenu.show (menu, x, p.y + root.height + 7)
    }

    Item {
        id: menu
        visible: false
        width: root.menuWidth
        height: root.menuHeight()

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
                model: root.entryList
                delegate: Item {
                    id: rowItem
                    required property var modelData
                    width: root.menuWidth - 6
                    height: modelData.h

                    Rectangle {
                        visible: rowItem.modelData.kind === "sep"
                        width: parent.width
                        height: 1
                        color: OvTheme.line
                    }

                    Text {
                        visible: rowItem.modelData.kind === "header"
                                 || rowItem.modelData.kind === "empty"
                        anchors.left: parent.left
                        anchors.leftMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        text: rowItem.modelData.text
                        color: OvTheme.faint
                        font.family: OvTheme.uiFont
                        font.pixelSize: 9
                    }

                    Rectangle {
                        visible: rowItem.modelData.kind === "preset"
                                 || rowItem.modelData.kind === "action"
                        anchors.fill: parent
                        color: rowMa.containsMouse ? OvTheme.panel : "transparent"
                    }

                    Text {
                        visible: rowItem.modelData.kind === "preset"
                                 || rowItem.modelData.kind === "action"
                        anchors.left: parent.left
                        anchors.leftMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        text: rowItem.modelData.text
                        color: rowMa.containsMouse ? OvTheme.ink : OvTheme.dim
                        font.family: OvTheme.uiFont
                        font.pixelSize: 10
                    }

                    MouseArea {
                        id: rowMa
                        anchors.fill: parent
                        hoverEnabled: true
                        enabled: rowItem.modelData.kind === "preset"
                                 || rowItem.modelData.kind === "action"
                        onClicked: {
                            const k = rowItem.modelData.kind
                            if (k === "preset" && root.engine)
                                root.engine.loadPreset (rowItem.modelData.folder,
                                                        rowItem.modelData.text)
                            else if (k === "action" && root.engine) {
                                if (rowItem.modelData.act === "load") root.engine.loadPresetDialog()
                                else root.engine.savePresetDialog()
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
