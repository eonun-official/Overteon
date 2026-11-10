import QtQuick

// ============================================================================
//  One numeric field of the node editor: input + unit, centred as a pair.
//
//  1:1 with a `.nodeedit label` in web_fx/index.html, which is
//
//      label  { display:flex; align-items:baseline; justify-content:center;
//               gap:3px }
//      input  { flex:0 0 46px; text-align:right; font-size:11.5px;
//               font-variant-numeric:tabular-nums; cursor:ns-resize }
//      ::after { flex:0 0 13px; font-size:8px; color:var(--faint) }
//
//  i.e. a 46 px right-aligned number, a 3 px gap, a 13 px unit column, and the
//  three together centred in the 64 px content box.
//
//  Interaction, also the page's:
//    * typing and pressing Enter / leaving the field commits it
//    * Escape closes the whole editor
//    * dragging up or down scrubs the value (0.005 normalised per px, 0.0012
//      with Shift) and a plain click still puts the caret in for typing
// ============================================================================
Item {
    id: root

    /// "f", "g" or "q" — which parameter this field edits.
    property string key: "f"
    /// The unit printed to the right: "Hz", "dB", or empty for Q.
    property string unit: ""
    /// The editor this belongs to (provides paramOf / typeIndex / refresh).
    property var host: null

    /// Two-way: the host writes the value in, and the field writes back what
    /// was typed.  NOT `readonly` — a read-only alias refuses the assignment
    /// and every refresh raises
    ///     TypeError: Cannot assign to read-only property "text"
    /// which is exactly what left the three fields empty.
    property alias text: input.text
    /// Item.activeFocus is FINAL too, so expose it under our own name.
    readonly property bool fieldFocus: input.activeFocus
    /// Item.enabled is FINAL in QtQuick, so this one carries its own name.
    property bool fieldEnabled: true

    width: parent ? parent.width : 64
    /// One row.  Kept equal to the editor's fieldH so the box's computed
    /// height and the rows it contains cannot drift apart.
    height: 15

    function param () {
        if (! host) return null
        const n = key === "f" ? "Freq" : (key === "g" ? "Gain" : "Q")
        return host.paramOf (n)
    }

    TextInput {
        id: input
        x: (root.width - 46 - 3 - 13) / 2
        width: 46
        anchors.verticalCenter: parent.verticalCenter

        horizontalAlignment: TextInput.AlignRight
        color: root.fieldEnabled ? (activeFocus ? "#ffffff" : OvTheme.ink)
                            : Qt.rgba (231 / 255, 235 / 255, 242 / 255, 0.3)
        selectionColor: OvTheme.faint
        selectedTextColor: OvTheme.ink
        font.family: OvTheme.uiFont
        // 11.5 px cannot be a pixelSize (Qt stores that as an int); pt()
        // converts the page's CSS px to points for the current scale.
        font.pointSize: OvTheme.pt (11.5)
        font.features: { "tnum": 1 }
        selectByMouse: true
        activeFocusOnPress: true

        // TextInput consumes Return itself, so Keys.onReturnPressed never
        // fires; onAccepted is the signal it emits instead.  editingFinished
        // covers the blur path, which is the page's `change` event.
        onAccepted: { if (host) host.applyField (root.key, text) }
        onEditingFinished: { if (host) host.applyField (root.key, text) }
        Keys.onEscapePressed: { if (host) host.close () }

    }

    // Pro-Q style scrub.  This sits ABOVE the TextInput and is a sibling of it,
    // not a child: as a child of TextInput it never received a press at all
    // (measured — the click fell through to the plot underneath), because the
    // input's own handlers took the event first.
    //
    // A press without movement still focuses the field so the user can type,
    // which is what the page gets from pointerdown focusing the input and only
    // calling preventDefault() once the pointer has really moved 3 px.
    MouseArea {
        id: scrub
        z: 10
        x: input.x - 2
        y: -2
        width: input.width + 4
        height: root.height + 4
        cursorShape: Qt.SizeVerCursor

        property real startY: 0
        property real startNorm: 0
        property bool moved: false

        onPressed: (m) => {
            startY = m.y
            moved = false
            const p = root.param ()
            startNorm = p ? p.norm : 0
            if (host) host.beginScrub ()
        }

        onCanceled: {
            if (host) host.endScrub ()
        }

        onPositionChanged: (m) => {
            if (! pressed) return
            const dy = startY - m.y
            if (! moved && Math.abs (dy) < 3) return
            const p = root.param ()
            if (! p) return
            if (! moved) {
                moved = true
                p.beginGesture ()
                input.focus = false          // stop editing, start scrubbing
            }
            const k = (m.modifiers & Qt.ShiftModifier) ? 0.0012 : 0.005
            p.setNorm (Math.max (0, Math.min (1, startNorm + dy * k)))
            if (host) host.refresh (false)
        }

        onReleased: {
            if (moved) {
                const p = root.param ()
                if (p) p.endGesture ()
            } else {
                input.forceActiveFocus ()    // a click, not a drag: type
                input.selectAll ()
            }
            moved = false
            if (host) host.endScrub ()
        }
    }

    // label::after — the unit, 8 px faint, in a 13 px column
    Text {
        text: root.unit
        color: OvTheme.faint
        font.family: OvTheme.uiFont
        font.pixelSize: 8
        font.letterSpacing: 0.2
        width: 13
        x: input.x + 46 + 3
        anchors.verticalCenter: parent.verticalCenter
    }
}
