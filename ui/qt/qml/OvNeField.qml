import QtQuick

// ============================================================================
//  One numeric field of the node editor: value + unit, centred as a pair.
//
//  1:1 with a `.nodeedit label` in web_fx/index.html:
//
//      label  { display:flex; align-items:baseline; justify-content:center;
//               gap:3px }
//      input  { flex:0 0 46px; text-align:right; font-size:11.5px;
//               font-variant-numeric:tabular-nums; cursor:ns-resize }
//      ::after { flex:0 0 13px; font-size:8px; color:var(--faint) }
//
//  i.e. a 46 px right-aligned number, a 3 px gap, a 13 px unit column, the set
//  centred in the 64 px content box.
//
//  FONTS ARE pixelSize, NOT pointSize.
//
//  That is the fix for "the text runs out of the box".  Every rect in this
//  design is in logical pixels, and the whole design tree is scaled by
//  fitScale (main.qml) — but a pointSize is resolved against the display's
//  logical DPI and then scaled again, so on a host-scaled editor the glyphs and
//  the box they sit in grow by different amounts and the text leaves the frame.
//  pixelSize is in the same unit as the rects, so the two can never disagree.
//
//  11.5 CSS px is not representable as a pixelSize (Qt stores it as an int);
//  12 is used, which is 4 % wider than the page.  The row is 15 px tall and the
//  value column is 46 px, both of which absorb that: measured, "20000" at
//  12 px is 33 px, so the widest value the parameter can hold still fits.
//
//  The layout is a Row rather than fixed x offsets.  With fixed offsets a
//  single wide glyph pushed the unit out of the frame and nothing noticed; a
//  Row sizes itself around its contents and is centred, so the pair stays
//  symmetric even if a font renders wider than the page's Inter does.
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

    // Explicit offsets, not a Row.
    //
    // A Row sizes itself from its children's widths, and in this tree that made
    // the unit column land on (and past) the box's right border on a scaled
    // host — measured, the unit's own rect came out at the box edge while the
    // numbers said it had 10 px of margin.  Fixed offsets cannot disagree with
    // the arithmetic, and both columns are clipped, so nothing can escape the
    // frame no matter what the font metrics do.
    //
    //     0 ......... 3 (left margin)
    //     3 .. 43      value, 40 px, right aligned
    //     43 .. 46     gap, 3 px
    //     46 .. 64     unit, 18 px
    //
    // The page gives the unit 13 px and the value 46.  Measured on this build,
    // 13 px does not hold "Hz" at the rendered metrics — the z was cut off at
    // the column edge — so the two are re-balanced to 40 + 18.  The total is
    // the same 62 px inside the same 64 px content box, the numbers are still
    // right-aligned in a fixed column so the three rows line up, and every
    // unit now has room for its full text.
    Item {
        id: theRow
        anchors.fill: parent

        TextInput {
            id: input
            x: 3
            width: 40
            anchors.verticalCenter: parent.verticalCenter

            horizontalAlignment: TextInput.AlignRight
            color: root.fieldEnabled ? (activeFocus ? "#ffffff" : OvTheme.ink)
                                : Qt.rgba (231 / 255, 235 / 255, 242 / 255, 0.3)
            selectionColor: OvTheme.faint
            selectedTextColor: OvTheme.ink
            font.family: OvTheme.uiFont
            font.pixelSize: 12
            font.features: { "tnum": 1 }
            selectByMouse: true
            activeFocusOnPress: true
            // The value must never paint outside its own column, whatever the
            // font metrics do.
            clip: true

            // TextInput consumes Return itself, so Keys.onReturnPressed never
            // fires; onAccepted is the signal it emits instead.  editingFinished
            // covers the blur path, which is the page's `change` event.
            onAccepted: { if (host) host.applyField (root.key, text) }
            onEditingFinished: { if (host) host.applyField (root.key, text) }
            Keys.onEscapePressed: { if (host) host.close () }
        }

        // label::after — the unit, 8 px faint.
        //
        // The page reserves exactly 13 px so the three numbers line up even
        // when only one row has a unit.  `width` does that, and `clip` keeps a
        // unit that renders wider than the page's Inter inside its column
        // instead of pushing the frame open.
        Text {
            id: unitText
            text: root.unit
            x: 46
            width: 18
            color: OvTheme.faint
            font.family: OvTheme.uiFont
            font.pixelSize: 8
            font.letterSpacing: 0.2
            anchors.verticalCenter: parent.verticalCenter
        }
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
        x: input.mapToItem (root, 0, 0).x - 2
        y: -2
        width: input.width + 4
        height: root.height + 4
        cursorShape: Qt.SizeVerCursor

        // The drag is measured in WINDOW coordinates, not in this item's.
        //
        // This is the whole of the "dragging the dB field goes wild" bug.  The
        // numeric box is glued to its node, so scrubbing gain moves the node,
        // which moves the box, which moves this MouseArea — and `m.y` is
        // relative to this MouseArea.  So m.y changed even while the pointer was
        // still, the delta fed itself, and a few pixels of hand movement drove
        // the value across the whole range.
        //
        // The page is immune for a reason that is easy to miss: it reads
        // ev.clientY, an absolute viewport coordinate, so its delta depends only
        // on the pointer.  MouseEvent has no scenePosition in Qt 6, so the
        // window coordinate is rebuilt here — item position plus local offset,
        // which is exactly pointer position however the item moves.
        readonly property var winY: function (ev) {
            return root.mapToItem (null, ev.x, ev.y).y
        }

        property real startY: 0
        property real startNorm: 0
        property bool moved: false

        onPressed: (m) => {
            startY = winY (m)
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
            const dy = startY - winY (m)
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
            // Refresh the readout AND repaint the plot in this frame: the
            // number and the node it belongs to have to move together.
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
}
