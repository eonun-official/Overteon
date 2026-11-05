import QtQuick
import QtQuick.Shapes
import Overteon 1.0

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

    readonly property bool inactive: prefix === "sc" ? driveMode === 0 : driveMode === 2

    clip: true

    // ---------------------------------------------------------------- plot
    OvEqView {
        id: eq
        anchors.fill: parent
        engine: root.engine
        prefix: root.prefix
        accent: root.accent
        headerTop: root.headerTop
        opacity: root.inactive ? 0.22 : 1.0
        Behavior on opacity { NumberAnimation { duration: 150 } }

        onMenuRequested: (band, pos, currentType) => {
            bandMenu.band = band
            bandMenu.currentType = currentType
            const p = eq.mapToItem (OvMenu.host ? OvMenu.host : root, pos)
            const mw = bandMenu.width, mh = bandMenu.height
            const host = OvMenu.host ? OvMenu.host : root
            const x = Math.max (4, Math.min (host.width - mw - 4, p.x))
            const y = Math.max (4, Math.min (host.height - mh - 4, p.y))
            OvMenu.show (bandMenu, x, y)
        }
        onHoverInfo: (text, pos) => {
            tip.text = text
            const p = eq.mapToItem (root, pos)
            tip.x = Math.max (4, Math.min (root.width - tip.width - 6, p.x + 12))
            tip.y = Math.max (4, p.y - 30)
            tip.visible = true
        }
        onHoverEnded: tip.visible = false
    }

    // ------------------------------------------------------ header bar area
    Item {
        id: bar
        x: 0
        y: root.headerTop ? 0 : root.height - OvTheme.barH
        width: root.width
        height: OvTheme.barH

        readonly property real pitW: Math.min (OvTheme.pitW, width)

        // The real frosted glass: a Gaussian blur of the plot underneath.
        // It covers the whole bar; the opaque side fills drawn on top cut it
        // back to the trapezoid, exactly like .eqhdr::before/::after do.
        OvGlass {
            anchors.fill: parent
            source: eq
            radius: 3.0
            saturation: 0.85
        }

        // accent glow inside the trapezoid (.controlpit::before), driven by
        // the meter level of this half of the signal path
        Canvas {
            id: glow
            anchors.fill: parent
            onPaint: {
                const ctx = getContext ("2d")
                ctx.reset()
                const w = width, h = height
                const cx = w / 2
                const cy = root.headerTop ? 0 : h
                const g = ctx.createRadialGradient (cx, cy, 0, cx, cy, w * 0.42)
                const a = root.meterLight * root.meterLight * 0.36
                const c = root.accent
                g.addColorStop (0.0, "rgba(" + Math.round (c.r*255) + "," + Math.round (c.g*255) + "," + Math.round (c.b*255) + "," + a + ")")
                g.addColorStop (0.38, "rgba(" + Math.round (c.r*255) + "," + Math.round (c.g*255) + "," + Math.round (c.b*255) + "," + (a * 0.36) + ")")
                g.addColorStop (1.0, "rgba(" + Math.round (c.r*255) + "," + Math.round (c.g*255) + "," + Math.round (c.b*255) + ",0)")
                // clip to the trapezoid
                ctx.beginPath()
                const pitW = Math.min (OvTheme.pitW, w)
                const x0 = (w - pitW) / 2
                if (root.headerTop) {
                    ctx.moveTo (x0 + pitW * 0.20, 0)
                    ctx.lineTo (x0 + pitW * 0.80, 0)
                    ctx.lineTo (x0 + pitW, h)
                    ctx.lineTo (x0, h)
                } else {
                    ctx.moveTo (x0, 0)
                    ctx.lineTo (x0 + pitW, 0)
                    ctx.lineTo (x0 + pitW * 0.80, h)
                    ctx.lineTo (x0 + pitW * 0.20, h)
                }
                ctx.closePath()
                ctx.clip()
                ctx.fillStyle = g
                ctx.fillRect (0, 0, w, h)
            }
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            Connections {
                target: root
                function onMeterLightChanged() { glow.requestPaint() }
            }
            Component.onCompleted: requestPaint()
        }

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
            font.pixelSize: 12
            font.letterSpacing: 2
            font.bold: true
            opacity: root.inactive ? 0.5 : 1.0
        }

        // ---- POST EQ also carries the limiter-mode token -------------------
        Row {
            anchors.left: titleText.right
            anchors.leftMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            spacing: 7
            visible: root.prefix === "pq"

            Rectangle {
                width: 6; height: 6; radius: 3
                anchors.verticalCenter: parent.verticalCenter
                color: {
                    const m = root.engine && root.engine.param ("protectMode")
                              ? Math.round (root.engine.param ("protectMode").norm * 3) : 2
                    return m === 0 ? OvTheme.dotOff : OvTheme.sc
                }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: {
                    const p = root.engine ? root.engine.param ("protectMode") : null
                    return p ? p.display.toUpperCase() : "MODERN"
                }
                color: {
                    const p = root.engine ? root.engine.param ("protectMode") : null
                    const m = p ? Math.round (p.norm * 3) : 2
                    return m === 0 ? OvTheme.faint : OvTheme.sc
                }
                font.family: OvTheme.uiFont
                font.pixelSize: 10
                font.letterSpacing: 1.4
            }
        }

        // ---- centre pit controls ------------------------------------------
        Row {
            anchors.centerIn: parent
            spacing: 18
            height: parent.height

            // SIDECHAIN: SAT OS | presets | LIM OS
            Row {
                visible: root.prefix === "sc"
                spacing: 12
                anchors.verticalCenter: parent.verticalCenter

                OvDropdown {
                    anchors.verticalCenter: parent.verticalCenter
                    param: root.engine ? root.engine.param ("osFactor") : null
                    label: "SAT OS"
                    openDown: true
                    labelColor: OvTheme.faint
                    accent: root.accent
                    dimmed: root.driveMode === 2
                }
                Rectangle { width: 1; height: 12; color: OvTheme.line; anchors.verticalCenter: parent.verticalCenter }
                OvPresetButton {
                    anchors.verticalCenter: parent.verticalCenter
                    engine: root.engine
                    accent: root.accent
                }
                Rectangle { width: 1; height: 12; color: OvTheme.line; anchors.verticalCenter: parent.verticalCenter }
                OvDropdown {
                    anchors.verticalCenter: parent.verticalCenter
                    param: root.engine ? root.engine.param ("limiterOs") : null
                    label: "LIM OS"
                    openDown: true
                    labelColor: OvTheme.faint
                    accent: root.accent
                    dimmed: {
                        const p = root.engine ? root.engine.param ("protectMode") : null
                        return p ? Math.round (p.norm * 3) <= 1 : false
                    }
                }
            }

            // POST EQ: STEREO | MODE | SATURATION
            Row {
                visible: root.prefix === "pq"
                spacing: 12
                anchors.verticalCenter: parent.verticalCenter

                OvDropdown {
                    anchors.verticalCenter: parent.verticalCenter
                    param: root.engine ? root.engine.param ("msMode") : null
                    label: "STEREO"
                    labelColor: OvTheme.faint
                    accent: root.accent
                }
                Rectangle { width: 1; height: 12; color: OvTheme.line; anchors.verticalCenter: parent.verticalCenter }
                OvDropdown {
                    anchors.verticalCenter: parent.verticalCenter
                    param: root.engine ? root.engine.param ("driveMode") : null
                    label: "MODE"
                    labelColor: OvTheme.faint
                    accent: root.accent
                }
                Rectangle { width: 1; height: 12; color: OvTheme.line; anchors.verticalCenter: parent.verticalCenter }
                OvDropdown {
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
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            spacing: 18

            OvToggle {
                anchors.verticalCenter: parent.verticalCenter
                visible: root.prefix === "sc"
                param: root.engine ? root.engine.param ("harmSolo") : null
                label: "SOLO HARM"
                accent: root.accent
            }
            OvToggle {
                anchors.verticalCenter: parent.verticalCenter
                visible: root.prefix === "pq"
                param: root.engine ? root.engine.param ("autoGain") : null
                label: "AUTO GAIN"
                accent: root.accent
            }

            OvRangeBadge {
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
