pragma Singleton
import QtQuick

// ---------------------------------------------------------------------------
//  In-scene menu host.
//
//  Qt Quick's Popup lives in the window overlay and does NOT inherit the
//  scale of the design item, so a popup opened from a 0.8x-scaled design
//  appears at the wrong place and at the wrong size.  All Overteon menus are
//  therefore plain Items: they reparent themselves to `host` (the design
//  root) when created and are positioned by the control that opens them.
//
//  Only one menu can be open at a time; main.qml draws a transparent click
//  catcher beneath them.
// ---------------------------------------------------------------------------
QtObject {
    id: root

    property Item host: null          // design root, set by main.qml
    property Item open: null          // the currently open menu item

    signal changed()

    function show (menu, x, y) {
        if (open && open !== menu) open.visible = false
        open = menu
        menu.x = x
        menu.y = y
        menu.visible = true
        menu.z = 9001
        changed()
    }

    function close () {
        if (open) open.visible = false
        open = null
        changed()
    }
}
