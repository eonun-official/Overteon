import QtQuick
import OnyxTEON 1.0

// One EQ half: the plot, the glazed header bar with its frosted trapezoid,
// and the controls that sit inside that trapezoid.
Item {
    id: root

    property var engine: null
    property string prefix: "sc"
    property bool headerTop: true
    property color accent: OvTheme.sc
    property string title: "SIDECHAIN EQ"
    property int driveMode: 1
    property real meterLight: 0.0      // 0..1, drives the accent glow
    property bool cpuFrost: false      // ONYXTEON_CPUFROST=1

    // .controlpit::before opacity, transition: opacity .1s linear, and the
    // CSS formula level*level*0.36
    property real glowAmount: meterLight * meterLight * 0.36   // var(--light) * .36
    //
    // NO Behavior here any more.  The CSS 0.1 s LINEAR transition was a third
    // interpolation on top of the meter's own: meterLight already arrives
    // through ovb::Fall (60 dB in 0.12 s, smootherstep-eased, 60 Hz), so a
    // linear ramp here only re-shaped a curve that had been shaped once
    // already — and linear is exactly the curve this change removes.  The
    // squaring stays: it is the page's, and it is a mapping, not a motion.

    readonly property bool inactive: prefix === "sc" ? driveMode === 0 : driveMode === 2

    // limiter mode: 0 Off, 1 Clip, 2 Modern, 3 True Peak
    readonly property int limiterIndex: {
        const p = engine ? engine.param ("protectMode") : null
        return p ? Math.round (p.norm * 3) : 2
    }
    readonly property bool limiterOn: limiterIndex > 0

    // centre of the grid's 1fr column (preset trigger), measured on the design
    readonly property real presetsCentreX: 519

    clip: true

    // ---------------------------------------------------------------- plot
    // The plot itself lives in main.qml's plot layer, so the frosted glass can
    // sample a subtree that contains only the EQ canvases.  Sampling this
    // section instead would recursively capture the glass items inside it,
    // which renders as a washed-out white slab.
    property OvEqView view: null

    Binding {
        target: root.view
        property: "opacity"
        value: root.inactive ? 0.22 : 1.0
        when: root.view !== null
    }

    Connections {
        target: root.view
        enabled: root.view !== null

        function onMenuRequested (band, pos, currentType) {
            bandMenu.band = band
            bandMenu.currentType = currentType
            const host = OvMenu.host ? OvMenu.host : root
            const p = root.view.mapToItem (host, pos)
            const mw = bandMenu.width, mh = bandMenu.height
            const x = Math.max (4, Math.min (host.width - mw - 4, p.x))
            const y = Math.max (4, Math.min (host.height - mh - 4, p.y))
            OvMenu.show (bandMenu, x, y)
        }

        function onHoverInfo (text, pos) {
            tipText.text = text              // the label, not the frame
            const p = root.view.mapToItem (root, pos)
            tip.x = Math.max (4, Math.min (root.width - tip.width - 6, p.x + 12))
            tip.y = Math.max (4, p.y - 30)
            tip.visible = true
            tipTimer.restart()          // 1400 ms, as showTip() does
        }

        function onHoverEnded () { tip.visible = false; tipTimer.stop() }

        // ---- node numeric editor (double-click a node) -------------------
        //
        // The widget is nodeEdit, below.  These three handlers are the same
        // contract the page has with its own canvas: a double-click on a node
        // opens it, a double-click on empty canvas that needs a new node says
        // so when the pool is full, and a plot gesture can take it away.
        function onNodeEditRequested (band) { nodeEdit.open (band) }
        function onNodeEditClosed () { nodeEdit.close () }

        function onPoolExhausted () {
            tipText.text = "ALL 5 NODES IN USE — REMOVE ONE FIRST"
            tip.x = Math.max (4, root.width / 2 - tip.width / 2)
            tip.y = 8
            tip.visible = true
            tipTimer.restart()          // 1400 ms, as flashFull() does
        }
    }

    // ------------------------------------------------------ header bar area
    Item {
        id: bar
        x: 0
        y: root.headerTop ? 0 : root.height - OvTheme.barH
        width: root.width
        height: OvTheme.barH

        readonly property real pitW: Math.min (OvTheme.pitW, width)

        // GPU glass for the control pit.  The CPU path rasterised this strip
        // into the plot's QImage on every repaint (1.0-1.5 ms); here it is four
        // fragment passes and costs the CPU nothing.
        OvGpuGlass {
            dbgName: root.prefix + "-pod"
            // ONYXTEON_NOFROST must switch this one off too: the reference
            // capture has to be free of glass, or a comparison against it
            // silently hides whatever this surface draws.
            visible: ! root.cpuFrost
                     && ! ((typeof ovNoFrost !== "undefined") && ovNoFrost)
            x: (parent.width - bar.pitW) / 2
            y: 0
            width: bar.pitW
            height: bar.height
            source: root.view
            ellipse: false
            trapTop: root.headerTop
            sigmaCss: 3.0
            accent: root.accent
            glow: root.meterLight * root.meterLight
        }

        // The frosted glass (backdrop blur + veil + accent glow) is painted by
        // OvEqView itself, in the same image it renders the plot into — the
        // same construction as FrostEngine in web_fx/index.html.  Drawing it
        // here with a ShaderEffectSource would capture a subtree that contains
        // glass items, which renders as a flat white slab.

        // opaque side fills, clipped away from the trapezoid (eqhdr::before/after)
        Canvas {
            id: sideFill
            anchors.fill: parent
            onPaint: {
                const ctx = getContext ("2d")
                ctx.reset()
                const w = width, h = height
                const px = (w - bar.pitW) / 2
                const pitX0 = px, pitX1 = px + bar.pitW
                ctx.fillStyle = "#1c1f26"
                ctx.beginPath()
                if (root.headerTop) {
                    // left block: up to the trapezoid's top-left edge
                    ctx.moveTo (0, 0)
                    ctx.lineTo (pitX0 + bar.pitW * 0.20, 0)
                    ctx.lineTo (pitX0, h)
                    ctx.lineTo (0, h)
                    ctx.closePath()
                    ctx.fill()
                    ctx.beginPath()
                    ctx.moveTo (pitX0 + bar.pitW * 0.80, 0)
                    ctx.lineTo (w, 0)
                    ctx.lineTo (w, h)
                    ctx.lineTo (pitX1, h)
                    ctx.closePath()
                    ctx.fill()
                } else {
                    ctx.moveTo (0, 0)
                    ctx.lineTo (pitX0, 0)
                    ctx.lineTo (pitX0 + bar.pitW * 0.20, h)
                    ctx.lineTo (0, h)
                    ctx.closePath()
                    ctx.fill()
                    ctx.beginPath()
                    ctx.moveTo (pitX1, 0)
                    ctx.lineTo (w, 0)
                    ctx.lineTo (w, h)
                    ctx.lineTo (pitX0 + bar.pitW * 0.80, h)
                    ctx.closePath()
                    ctx.fill()
                }
            }
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
        }

        // ---- title (left) ------------------------------------------------
        Text {
            id: titleText
            anchors.left: parent.left
            anchors.leftMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            text: root.title
            color: root.accent
            font.family: OvTheme.uiFont
            font.pointSize: OvTheme.pt (11.5)
            font.letterSpacing: 2
            font.bold: true
            opacity: root.inactive ? 0.5 : 1.0
        }

        // ---- POST EQ also carries the limiter-mode token (.limdd) ----------
        // This is a real dropdown: OFF / CLIP / MODERN / TRUE PEAK.
        OvDropdown {
            id: limiterMode
            visible: root.prefix === "pq"
            anchors.left: titleText.right
            anchors.leftMargin: 10
            anchors.verticalCenter: parent.verticalCenter

            param: root.engine ? root.engine.param ("protectMode") : null
            options: [ "OFF", "CLIP", "MODERN", "TRUE PEAK" ]
            showDot: true
            accent: OvTheme.sc
            textColor: root.limiterOn ? OvTheme.sc : OvTheme.faint
            minWidth: 0
        }

        // ---- centre pit controls ------------------------------------------
        // Positions come from the design's own layout:
        //   SIDECHAIN  .pitcontrols is a 5-column grid inside the 640 px pit
        //              with padding 0 110px and gap 6px, so the preset trigger
        //              is centred between the two separators;
        //   POST EQ    .pitcontrols is a centred flex row, gap 18px.
        // The measured left edges of each group in the web render are used
        // below so both front ends agree to the pixel.
        Item {
            id: pit
            anchors.fill: parent

            // SIDECHAIN — grid: [SAT OS][sep][presets 1fr][sep][LIM OS]
            Item {
                visible: root.prefix === "sc"
                anchors.fill: parent

                OvDropdown {
                    x: 311
                    anchors.verticalCenter: parent.verticalCenter
                    param: root.engine ? root.engine.param ("osFactor") : null
                    label: "SAT OS"
                    openDown: true
                    labelColor: OvTheme.faint
                    accent: root.accent
                    dimmed: root.driveMode === 2
                }

                Rectangle {
                    x: 401; width: 1; height: 12; color: OvTheme.line
                    anchors.verticalCenter: parent.verticalCenter
                }

                OvPresetButton {
                    x: root.presetsCentreX - width / 2
                    anchors.verticalCenter: parent.verticalCenter
                    engine: root.engine
                    accent: root.accent
                }

                Rectangle {
                    x: 639; width: 1; height: 12; color: OvTheme.line
                    anchors.verticalCenter: parent.verticalCenter
                }

                OvDropdown {
                    x: 659
                    anchors.verticalCenter: parent.verticalCenter
                    param: root.engine ? root.engine.param ("limiterOs") : null
                    label: "LIM OS"
                    openDown: true
                    labelColor: OvTheme.faint
                    accent: root.accent
                    dimmed: root.limiterIndex <= 1
                }
            }

            // POST EQ — centred flex row, gap 18
            Item {
                visible: root.prefix === "pq"
                anchors.fill: parent

                OvDropdown {
                    x: 319
                    anchors.verticalCenter: parent.verticalCenter
                    param: root.engine ? root.engine.param ("msMode") : null
                    label: "STEREO"
                    labelColor: OvTheme.faint
                    accent: root.accent
                }

                Rectangle {
                    x: 437; width: 1; height: 12; color: OvTheme.line
                    anchors.verticalCenter: parent.verticalCenter
                }

                OvDropdown {
                    x: 456
                    anchors.verticalCenter: parent.verticalCenter
                    param: root.engine ? root.engine.param ("driveMode") : null
                    label: "MODE"
                    labelColor: OvTheme.faint
                    accent: root.accent
                }

                Rectangle {
                    x: 577; width: 1; height: 12; color: OvTheme.line
                    anchors.verticalCenter: parent.verticalCenter
                }

                OvDropdown {
                    x: 597
                    anchors.verticalCenter: parent.verticalCenter
                    param: root.engine ? root.engine.param ("driveType") : null
                    label: "SATURATION"
                    labelColor: OvTheme.faint
                    accent: root.accent
                    dimmed: root.driveMode === 2
                }
            }
        }

        // ---- right hand controls ------------------------------------------
        // The design puts the toggle and the range badge at fixed offsets from
        // the bar's right edge (measured: dot at 862 / 873, badge text ending
        // at 1015 in a 1040 wide design).
        Item {
            anchors.fill: parent

            OvToggle {
                x: root.prefix === "sc" ? 862 : 873
                anchors.verticalCenter: parent.verticalCenter
                param: root.engine
                       ? root.engine.param (root.prefix === "sc" ? "harmSolo" : "autoGain")
                       : null
                label: root.prefix === "sc" ? "SOLO HARM" : "AUTO GAIN"
                accent: root.accent
            }

            OvRangeBadge {
                anchors.right: parent.right
                anchors.rightMargin: 11
                anchors.verticalCenter: parent.verticalCenter
                param: root.engine ? root.engine.param (root.prefix + "Range") : null
                options: root.prefix === "sc"
                         ? [ "±12 dB", "±24 dB", "±48 dB", "±90 dB" ]
                         : [ "±3 dB", "±6 dB", "±12 dB", "±30 dB" ]
                accent: root.accent
                openDown: root.headerTop
            }
        }
    }

    // showTip() auto-hide: 1400 ms
    Timer {
        id: tipTimer
        interval: 1400
        onTriggered: tip.visible = false
    }

    // node tooltip
    Rectangle {
        id: tip
        visible: false
        width: tipText.implicitWidth + 16
        height: 22
        color: OvTheme.menuBg
        border.color: OvTheme.line
        radius: 4
        z: 20
        Text {
            id: tipText
            anchors.centerIn: parent
            color: OvTheme.ink
            font.family: OvTheme.uiFont
            font.pixelSize: 10
            font.features: { "tnum": 1 }
        }
    }

    OvBandMenu {
        id: bandMenu
        engine: root.engine
        prefix: root.prefix
        accent: root.accent

        onChoose: (typeName) => {
            const base = root.prefix + (bandMenu.band + 1)
            const pType = root.engine ? root.engine.param (base + "Type") : null
            const pGain = root.engine ? root.engine.param (base + "Gain") : null
            if (! pType) return

            if (typeName === "Remove") {
                pType.setNorm (0)                    // Bell
                if (pGain) pGain.setValue (0)        // back into the pool
                OvMenu.close()
                return
            }

            const names = [ "Bell", "Notch", "Low Shelf", "High Shelf",
                            "Low Cut 12", "Low Cut 24", "High Cut 12", "High Cut 24" ]
            const idx = names.indexOf (typeName)
            if (idx < 0) return
            pType.beginGesture()
            pType.setNorm (idx / 7)
            pType.endGesture()

            // give a fresh gain-type node something audible to show
            if ((idx === 0 || idx === 2 || idx === 3) && pGain && Math.abs (pGain.value) < 0.5)
                pGain.setValue (idx === 0 ? 4.0 : 3.0)

            OvMenu.close()
        }
    }
    // ======================================================================
    //  Node numeric editor — 1:1 with .nodeedit in web_fx/index.html
    //
    //  Double-click a node and three bare values appear above it, each with
    //  its unit appended.  The editor is bound to the BAND, not to a screen
    //  position: it follows its node wherever the node goes and stays open
    //  until dismissed — ✕, Esc, a press on empty canvas, or walking more than
    //  30 px away.  The last two arrive as nodeEditClosed from the plot.
    //
    //  Geometry, colours and type sizes are the page's:
    //      width 80   padding 6 8 7   radius 4
    //      background rgba(22,25,31,.96)   border rgba(58,66,80,.65)
    //      head 8 px / 700 / letter-spacing 1.2      ✕ 9 px
    //      fields 11.5 px, right aligned, tabular figures, 46 px
    //      units 8 px faint in a 13 px column:  Hz / dB / (none)
    //      order: freq, gain, Q — gain greyed on cuts/notches, Q on shelves
    // ======================================================================
    Item {
        id: nodeEdit
        visible: false
        z: 50
        width: 80

        // The height is COMPUTED, not derived from the contents' implicit
        // height.  `neColumn.implicitHeight` is not ready during the first
        // layout pass, so place() read 0 and treated the box as 18 px tall:
        // it never flipped below the node, and the third field was laid out
        // outside the box and clipped away.  These are the page's own numbers:
        //   padding 6 top / 7 bottom, head 13, head margin 3,
        //   three 15 px rows with 2 px gaps
        readonly property int padTop:    6
        readonly property int padBottom: 7
        readonly property int headH:     13
        readonly property int headGap:   3
        readonly property int fieldH:    15
        readonly property int fieldGap:  2

        height: padTop + headH + headGap
              + fieldH * 3 + fieldGap * 2
              + padBottom

        property int  band: -1
        property bool below: false
        property real arrowX: width / 2
        /// True once the pointer has been inside the box, so that leaving it
        /// later is what closes the editor — and merely appearing under a
        /// pointer that is nowhere near it does not.
        property bool wasHovered: false

        /// True while a field is being scrubbed.  Scrubbing freq or gain moves
        /// the node, the box follows it, and the pointer stays where the hand
        /// is — so without this the 30 px rule (and the pointer-leave rule)
        /// would close the editor in the middle of the gesture.
        property bool scrubbing: false

        function beginScrub () {
            scrubbing = true
            if (root.view) root.view.setEditingScrub (true)
        }

        function endScrub () {
            scrubbing = false
            if (root.view) root.view.setEditingScrub (false)
        }

        readonly property var typeNames: [ "BELL", "NOTCH", "LOW SHELF", "HIGH SHELF",
                                           "LOW CUT 12", "LOW CUT 24",
                                           "HIGH CUT 12", "HIGH CUT 24" ]

        function paramOf (suffix) {
            if (! root.engine || band < 0) return null
            return root.engine.param (root.prefix + (band + 1) + suffix)
        }

        function typeIndex () {
            const p = paramOf ("Type")
            return p ? Math.round (p.norm * 7) : 0
        }

        function open (b) {
            if (b < 0 || b > 4) return
            band = b
            wasHovered = false
            visible = true
            if (root.view) root.view.setEditingBand (b)
            refresh (true)
            place ()
        }

        function close () {
            if (! visible) return
            visible = false
            band = -1
            if (root.view) root.view.setEditingBand (-1)
        }

        /// Fill the fields from the parameters.  `force` also overwrites the
        /// field that has focus, so typing is never clobbered mid-entry —
        /// updateEdit()'s `if (fill || document.activeElement !== iF)`.
        function refresh (force) {
            if (band < 0) return
            const t = typeIndex ()
            const pf = paramOf ("Freq"), pg = paramOf ("Gain"), pq = paramOf ("Q")
            const mult = root.view ? root.view.gainDisplayMult () : 1.0

            neTitle.text = "#" + (band + 1) + " " + (typeNames[t] || "?")

            const gainOk = root.view ? root.view.typeHasGain (t) : true
            const qOk    = root.view ? root.view.typeHasQ (t) : true

            fieldF.fieldEnabled = true
            fieldG.fieldEnabled = gainOk
            fieldQ.fieldEnabled = qOk

            if (force || ! fieldF.fieldFocus) fieldF.text = pf ? String (Math.round (pf.value)) : ""
            if (force || ! fieldG.fieldFocus) fieldG.text = pg ? (pg.value * mult).toFixed (1) : ""
            if (force || ! fieldQ.fieldFocus) fieldQ.text = pq ? pq.value.toFixed (2) : ""
        }

        /// Centre over the node, flip below when there is no room, clamp into
        /// the plot, and slide the pointer along the box's edge so it always
        /// aims at the node.  updateEdit() in the page.
        function place () {
            if (band < 0 || ! root.view) return
            const p = root.view.nodePosition (band)
            const ew = width, eh = height

            let px = p.x
            let py = p.y
            let x = px - ew * 0.5
            x = Math.max (4, Math.min (x, root.width - ew - 4))

            let y = py - eh - 14
            below = false
            if (y < 4) { y = py + 16; below = true }
            y = Math.max (4, Math.min (y, root.height - eh - 4))

            nodeEdit.x = x
            nodeEdit.y = y
            arrowX = Math.max (11, Math.min (ew - 11, px - x))
        }

        // The node moves while it is dragged, and whenever the range or the
        // curve changes.  30 Hz is enough for a box that only has to keep up
        // with a finger.
        Timer {
            running: nodeEdit.visible
            interval: 33
            repeat: true
            onTriggered: nodeEdit.place ()
        }

        // ---- the box -----------------------------------------------------
        // box-shadow: 0 8px 22px rgba(0,0,0,.5) — approximated with stacked
        // translucent rounded rects.  A real blur here would need the box
        // rendered to a texture and read back, and on this background the
        // difference is invisible next to the cost.
        Repeater {
            model: 4
            Rectangle {
                x: -2 - index
                y: 6 - 2 - index
                width: nodeEdit.width + 4 + index * 2
                height: nodeEdit.height + 4 + index * 2
                radius: 4 + index
                color: Qt.rgba (0, 0, 0, 0.13)
            }
        }

        Rectangle {
            id: neBody
            anchors.fill: parent
            radius: 4
            color: Qt.rgba (22 / 255, 25 / 255, 31 / 255, 0.96)
            border.width: 1
            border.color: Qt.rgba (58 / 255, 66 / 255, 80 / 255, 0.65)
        }

        // ::before — the 8x8 diamond, rotated 45°, that ties the box to its
        // node.  Only the two edges facing away from the box are drawn, which
        // is what border-right + border-bottom do on the page.
        Item {
            width: 8; height: 8
            x: nodeEdit.arrowX - 4
            y: nodeEdit.below ? -4 : nodeEdit.height - 5
            rotation: 45
            Rectangle {
                width: 8; height: 8; radius: 0
                color: Qt.rgba (22 / 255, 25 / 255, 31 / 255, 0.96)
                border.width: 1
                border.color: Qt.rgba (58 / 255, 66 / 255, 80 / 255, 0.65)
                z: -1
            }
            Rectangle {
                anchors.fill: parent
                anchors.margins: 1
                color: Qt.rgba (22 / 255, 25 / 255, 31 / 255, 0.96)
            }
        }

        // The page stops every pointer event on the editor from reaching the
        // canvas:  ed.addEventListener("pointerdown", ev => ev.stopPropagation())
        // Without it a press on the box's own background falls through to the
        // plot, which then treats it as "click on empty canvas" and dismisses
        // the editor the user is trying to use.  Declared before the fields so
        // they, being later children, still get their own events first.
        MouseArea {
            id: neBlock
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
            onClicked: (m) => { m.accepted = true }
            // pointerleave closes it, as the page's listener does
            onContainsMouseChanged: {
                if (! containsMouse && nodeEdit.visible && nodeEdit.wasHovered
                        && ! nodeEdit.scrubbing)
                    nodeEdit.close ()
                if (containsMouse) nodeEdit.wasHovered = true
            }
        }

        Column {
            id: neColumn
            x: 8
            y: nodeEdit.padTop
            width: parent.width - 16
            spacing: 0

            // .nehead { gap:5px; margin-bottom:3px }
            Item {
                width: parent.width
                height: nodeEdit.headH

                Text {
                    id: neTitle
                    color: root.accent
                    opacity: 0.85
                    font.family: OvTheme.uiFont
                    font.pixelSize: 8
                    font.weight: Font.Bold
                    font.letterSpacing: 1.2
                    anchors.verticalCenter: parent.verticalCenter
                }

                // .nex — transparent until the box is hovered, then faint
                Text {
                    text: "✕"
                    color: neBlock.containsMouse ? OvTheme.faint : "transparent"
                    font.family: OvTheme.uiFont
                    font.pixelSize: 9
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -3
                        cursorShape: Qt.PointingHandCursor
                        onClicked: nodeEdit.close ()
                    }
                }
            }

            Item { width: 1; height: nodeEdit.headGap }   // .nehead margin-bottom

            // .nerow { flex-direction:column; gap:2px }
            Column {
                width: parent.width
                spacing: nodeEdit.fieldGap

                OvNeField { id: fieldF; key: "f"; unit: "Hz"; host: nodeEdit }
                OvNeField { id: fieldG; key: "g"; unit: "dB"; host: nodeEdit }
                OvNeField { id: fieldQ; key: "q"; unit: "";   host: nodeEdit }
            }
        }


        /// applyEditField() in the page: parse the text for one field, clamp,
        /// write it, redraw.
        function applyField (key, raw) {
            if (band < 0 || ! root.view) return
            const v = String (raw).trim ()
            if (! v) { refresh (true); return }

            if (key === "f") {
                const m = v.match (/(-?[\d.]+)\s*(k)?/i)
                if (! m) { refresh (true); return }
                let hz = parseFloat (m[1])
                if (m[2]) hz *= 1000
                if (! isFinite (hz)) { refresh (true); return }
                const p = paramOf ("Freq")
                if (p) { p.beginGesture (); p.setValue (hz); p.endGesture () }

            } else if (key === "g") {
                if (! root.view.typeHasGain (typeIndex ())) { refresh (true); return }
                const db = parseFloat (v)
                if (! isFinite (db)) { refresh (true); return }
                const mult = root.view.gainDisplayMult ()
                const p = paramOf ("Gain")
                if (p) { p.beginGesture (); p.setValue (db / (mult || 1.0)); p.endGesture () }

            } else {
                const q = parseFloat (v)
                if (! isFinite (q)) { refresh (true); return }
                const p = paramOf ("Q")
                if (p) { p.beginGesture (); p.setValue (Math.max (0.1, Math.min (6, q))); p.endGesture () }
            }
            refresh (true)
        }
    }
}
