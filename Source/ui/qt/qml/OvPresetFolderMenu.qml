import QtQuick

// ---------------------------------------------------------------------------
//  One panel of the cascading preset menu.
//
//  The root panel is opened by OvPresetButton through OvMenu (so the shared
//  click-catcher, Esc and every other in-scene menu keep working).  A folder
//  row opens ANOTHER instance of this same component beside the panel — to the
//  right when it fits, otherwise to the left — and that instance can do the
//  same again.  Nothing here knows a depth limit: the panel's model IS the
//  folder node, so a folder inside a folder simply gets another panel.
//
//  All panels are siblings parented to OvMenu.host, exactly like the other
//  menus in this UI: Qt's Popup does not inherit the design item's scale.
// ---------------------------------------------------------------------------
Item {
    id: root

    property var    engine: null
    property var    node: ({})          // { name, folders: [...], presets: [...] }
    property string path: ""            // "Factory/Drums" — relative to presets/
    property bool   isRoot: false
    property color  accent: OvTheme.sc

    // Set on child panels only.
    property var parentMenu: null
    property int depth: 0

    visible: false
    width: 156
    z: 9001 + depth

    readonly property int rowH: 22

    // ---- contents ------------------------------------------------------
    //
    // Folders first, then presets — the same order at every level.  Names are
    // taken verbatim from the scan; only the display is upper-cased, so the
    // on-disk casing is never rewritten.
    function buildRows () {
        const out = []
        const n = root.node || ({})
        const folders = n.folders || []
        const presets = n.presets || []

        for (let i = 0; i < folders.length; ++i)
            out.push ({ kind: "folder", text: String (folders[i].name || "").toUpperCase(),
                        folderIndex: i, row: out.length, h: root.rowH })

        if (! root.isRoot && folders.length === 0 && presets.length === 0)
            out.push ({ kind: "empty", text: "(EMPTY)", row: out.length, h: root.rowH })

        for (const p of presets)
            out.push ({ kind: "preset", text: String (p), row: out.length, h: root.rowH })

        if (root.isRoot) {
            out.push ({ kind: "sep", text: "", row: out.length, h: 1 })
            out.push ({ kind: "action", act: "load", text: "LOAD…", row: out.length, h: root.rowH })
            out.push ({ kind: "action", act: "save", text: "SAVE AS…", row: out.length, h: root.rowH })
        }
        return out
    }

    readonly property var rows: buildRows()

    function rowOffset (rowIndex) {
        let y = 0
        for (let i = 0; i < rowIndex && i < rows.length; ++i) y += rows[i].h
        return y
    }

    height: 6 + rowOffset (rows.length)

    // ---- child panels --------------------------------------------------
    property var childMenu: null
    property int childRow: -1
    /// 指针是否在这个面板的矩形内（由下面的轮询更新）。
    property bool pointerInside: false

    // Created by URL, not declared as `Component { OvPresetFolderMenu {} }`.
    // Qt refuses a type that instantiates itself in its own file
    // ("OvPresetFolderMenu is instantiated recursively") even though the panel
    // is only ever created at run time.  Qt.createComponent() from the file's
    // own URL sidesteps that check and is still synchronous for qrc.
    property var panelComp: null

    function folderPanelComponent () {
        if (panelComp === null)
            panelComp = Qt.createComponent ("OvPresetFolderMenu.qml",
                                            Component.PreferSynchronous)
        return panelComp
    }

    /// 指针在本面板或它的任意一层子面板里。
    function pointerInsideChain () {
        if (pointerInside) return true
        return childMenu !== null && childMenu.pointerInsideChain ()
    }

    /// 事件驱动：每次鼠标移动判定一次本面板的矩形。
    ///
    /// 指针从父面板移进子面板时，父面板的 pointerInside 会变 false，但
    /// 子面板的为 true；outsideTimer 触发时用 pointerInsideChain() 再确认
    /// 一次，所以不会在指针进入子面板后把它收掉。
    function evaluatePointer () {
        if (! visible) return
        pointerInside = OvPointer.inside (root, 2)
        if (pointerInside) {
            outsideTimer.stop ()
        } else if (childMenu !== null && ! outsideTimer.running) {
            outsideTimer.start ()
        }
    }

    Connections {
        target: OvPointer
        function onMoved () { root.evaluatePointer () }
    }

    /// 离开整个面板链 250 ms 后才收起下一层。计时器触发时再确认一次
    /// 链上是否有指针，因为父面板的 pointerInside 在子面板里也是 false。
    Timer {
        id: outsideTimer
        interval: 250
        onTriggered: { if (! root.pointerInsideChain ()) root.closeChild () }
    }

    function closeChild () {
        outsideTimer.stop ()
        if (childMenu) {
            childMenu.visible = false
            childMenu.destroy ()
            childMenu = null
        }
        childRow = -1
    }

    function openFolder (i) {
        const folders = (node && node.folders) ? node.folders : []
        if (i < 0 || i >= folders.length) return
        if (childMenu !== null && childRow === root.folderRow (i)) return

        closeChild ()

        const host = OvMenu.host ? OvMenu.host : root.parent
        if (! host) return

        const comp = folderPanelComponent ()
        if (comp === null || comp.status !== Component.Ready) {
            console.warn ("preset menu: cannot create folder panel:",
                          comp ? comp.errorString () : "")
            return
        }

        const f = folders[i]
        const c = comp.createObject (host, {
            "engine": root.engine,
            "node": f,
            "path": (root.path ? root.path + "/" : "") + String (f.name || ""),
            "isRoot": false,
            "accent": root.accent,
            "parentMenu": root,
            "depth": root.depth + 1
        })
        if (! c) return

        childMenu = c
        childRow = root.folderRow (i)

        // Flush against the panel's edge: no gap for the pointer to cross,
        // which is what makes walking into the submenu reliable.  Right first,
        // left when there is no room (the preset button sits mid-window).
        const ry = 3 + rowOffset (root.folderRow (i))
        let cx = root.x + root.width - 2
        if (cx + c.width > host.width - 2) cx = root.x - c.width + 2
        cx = Math.max (2, Math.min (cx, host.width - c.width - 2))
        let cy = root.y + ry
        cy = Math.max (2, Math.min (cy, host.height - c.height - 2))

        c.x = cx
        c.y = cy
        c.visible = true
        outsideTimer.stop ()   // the new panel is now part of the chain
    }

    function folderRow (folderIndex) {
        for (const r of rows)
            if (r.kind === "folder" && r.folderIndex === folderIndex) return r.row
        return -1
    }

    // Hidden by OvMenu.close() (catcher, Esc, another menu): take the whole
    // chain with it.
    onVisibleChanged: {
        if (visible) evaluatePointer ()
        else closeChild ()
    }

    // ---- panel ---------------------------------------------------------
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
            model: root.rows
            delegate: Item {
                id: rowItem
                required property var modelData

                width: root.width - 6
                height: modelData.h

                property bool hovered: rowMa.containsMouse
                                       || (root.childMenu !== null
                                           && root.childRow === modelData.row)

                Rectangle {
                    visible: rowItem.modelData.kind === "sep"
                    width: parent.width
                    height: 1
                    color: OvTheme.line
                }

                Rectangle {
                    anchors.fill: parent
                    visible: rowItem.modelData.kind !== "sep"
                    color: rowItem.hovered ? OvTheme.panel : "transparent"
                }

                Text {
                    visible: rowItem.modelData.kind === "empty"
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    text: rowItem.modelData.text
                    color: OvTheme.faint
                    font.family: OvTheme.uiFont
                    font.pixelSize: 9
                }

                Text {
                    id: rowLabel
                    visible: rowItem.modelData.kind !== "sep"
                             && rowItem.modelData.kind !== "empty"
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    anchors.right: rowItem.modelData.kind === "folder"
                                   ? folderArrow.left : parent.right
                    anchors.rightMargin: rowItem.modelData.kind === "folder" ? 10 : 8
                    anchors.verticalCenter: parent.verticalCenter
                    text: rowItem.modelData.text
                    elide: Text.ElideRight
                    color: rowItem.hovered ? OvTheme.ink : OvTheme.dim
                    Behavior on color { ColorAnimation { duration: 120 } }
                    font.family: OvTheme.uiFont
                    font.pointSize: OvTheme.pt (9.5)
                    font.letterSpacing: rowItem.modelData.kind === "folder" ? 1.0 : 0.4
                }

                Text {
                    id: folderArrow
                    visible: rowItem.modelData.kind === "folder"
                    text: "›"
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    color: rowItem.hovered ? root.accent : OvTheme.faint
                    font.family: OvTheme.uiFont
                    font.pixelSize: 12
                }

                MouseArea {
                    id: rowMa
                    anchors.fill: parent
                    hoverEnabled: true

                    onEntered: {
                        if (rowItem.modelData.kind === "folder")
                            root.openFolder (rowItem.modelData.folderIndex)
                        else
                            root.closeChild ()
                    }

                    onClicked: {
                        const d = rowItem.modelData
                        if (d.kind === "folder") {
                            root.openFolder (d.folderIndex)
                        } else if (d.kind === "preset") {
                            if (root.engine)
                                root.engine.loadPreset (root.path, d.text)
                            OvMenu.close ()
                        } else if (d.kind === "action") {
                            if (root.engine) {
                                if (d.act === "load") root.engine.loadPresetDialog ()
                                else root.engine.savePresetDialog ()
                            }
                            OvMenu.close ()
                        }
                    }
                }
            }
        }
    }
}
