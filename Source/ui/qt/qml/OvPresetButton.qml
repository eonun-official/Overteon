import QtQuick

// Preset trigger in the SIDECHAIN header pit: the current preset name, then a
// cascading folder tree plus LOAD… / SAVE AS….
//
// The tree is NOT flattened here any more.  The root panel shows the top-level
// folders (Factory, User, and anything else found on disk); hovering a folder
// opens its contents in a panel beside it, and a folder inside that panel opens
// another one.  The panels themselves live in OvPresetFolderMenu.qml.
Item {
    id: root

    property var engine: null
    property color accent: OvTheme.sc
    readonly property string current: engine ? engine.presetName : "default"

    implicitWidth: btnText.implicitWidth + 8
    implicitHeight: 20

    Component { id: rootPanel; OvPresetFolderMenu { } }

    /// The root panel, created on first open and reused afterwards.  It is
    /// shown through OvMenu, so the shared click-catcher and Esc close it and
    /// its child panels with it.
    property var menu: null

    Text {
        id: btnText
        anchors.centerIn: parent
        text: root.current
        color: (ma.containsMouse || (root.menu && root.menu.visible))
               ? root.accent : OvTheme.faint
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
            if (root.menu && root.menu.visible) OvMenu.close()
            else root.openMenu()
        }
    }

    function openMenu () {
        if (OvMenu.host === null) return
        if (root.engine && root.engine.presets) {
            if (root.menu === null) {
                root.menu = rootPanel.createObject (OvMenu.host, {
                    "engine": root.engine,
                    "node": root.engine.presets,
                    "path": "",
                    "isRoot": true,
                    "accent": root.accent
                })
            } else {
                // refreshPresets() runs on every open; take the new tree.
                root.menu.engine = root.engine
                root.menu.node = root.engine.presets
            }
        }
        if (root.menu === null) return

        const host = OvMenu.host
        const p = root.mapToItem (host, 0, 0)
        const w = root.menu.width
        const x = Math.max (2, Math.min (host.width - w - 2,
                                          p.x + root.width / 2 - w / 2))
        const y = Math.max (2, Math.min (p.y + root.height + 7,
                                          host.height - root.menu.height - 2))
        OvMenu.show (root.menu, x, y)
    }
}
