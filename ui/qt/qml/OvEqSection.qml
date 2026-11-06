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

    // .controlpit::before opacity, transition: opacity .1s linear, and the
    // CSS formula level*level*0.36
    property real glowAmount: meterLight * meterLight * 0.36
    // CSS: .controlpit::before { transition: opacity .1s linear }
    Behavior on glowAmount { NumberAnimation { duration: 100; easing.type: Easing.Linear } }

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
    }

    // ------------------------------------------------------ header bar area
    Item {
        id: bar
        x: 0
        y: root.headerTop ? 0 : root.height - OvTheme.barH
        width: root.width
        height: OvTheme.barH

        readonly property real pitW: Math.min (OvTheme.pitW, width)

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
}
