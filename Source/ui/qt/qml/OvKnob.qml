import QtQuick

// Pro-Q4 style knob: bare accent arc + needle + centre dot, no track, no
// frame — exactly .kring/.karc/.kndl/.kdot from web_fx/index.html.
//
// Geometry (identical to the design's knob()):
//   * 270° sweep, from -135° to +135° measured from 12 o'clock, clockwise
//   * ring radius 15.5 in a 40-unit box, needle from r=2 to r=15
//   * accent arc only drawn above 0.4 % (the CSS hides it below that)
Item {
    id: root

    property var param: null
    property string label: ""
    property color accent: OvTheme.sc
    property real ringSize: 46
    property var fmt: null                 // optional JS formatter(value)
    property bool dimmed: false

    width: 58
    height: 88

    readonly property real norm: param ? Math.max (0, Math.min (1, param.norm)) : 0
    readonly property double value: param ? param.value : 0

    // Wheel state.  The accumulator turns sub-detent deltas into whole notches;
    // wheelHeld marks a gesture that the wheel opened, so the timeout that ends
    // it can never be confused with a drag the pointer is still holding.
    property real wheelAccum: 0
    property bool wheelHeld: false

    /// One wheel notch: `dir` is +1 up, -1 down.
    function applyWheel (dir, shift) {
        if (! param) return
        if (param.isChoice || param.isBool) {
            const count = param.isBool ? 2 : param.choices.length
            if (count < 2) return
            const from = Math.round (param.norm * (count - 1))
            const to   = Math.max (0, Math.min (count - 1, from + dir))
            if (to !== from) param.setNorm (to / (count - 1))
            return
        }
        const step = shift ? 0.01 : 0.04
        param.setNorm (Math.max (0, Math.min (1, param.norm + dir * step)))
    }

    Timer {
        id: wheelEnd
        interval: 350
        repeat: false
        onTriggered: {
            if (root.wheelHeld && root.param) root.param.endGesture()
            root.wheelHeld = false
        }
    }

    readonly property string shownText: {
        if (! param) return "—"
        if (fmt) return fmt (param.value)
        return param.display
    }

    opacity: dimmed ? 0.28 : 1.0
    enabled: ! dimmed

    // Canvas angles are radians with 0 = 3 o'clock, positive clockwise.
    // 135° is down-left; sweeping +270° runs through left, top, right and
    // ends at 45° (down-right), leaving the gap at the bottom exactly like
    // the design's point(deg) helper does.
    readonly property real startRad: 135 * Math.PI / 180
    readonly property real sweepRad: 270 * Math.PI / 180

    Canvas {
        id: ring
        width: root.ringSize
        height: root.ringSize
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        antialiasing: true

        onPaint: {
            const ctx = getContext ("2d")
            ctx.reset()
            const s = width / 40.0
            ctx.scale (s, s)
            ctx.clearRect (0, 0, 40, 40)

            const cx = 20, cy = 20, rad = 15.5
            const n = root.norm

            // accent arc (value portion only, like .karc)
            if (n > 0.004) {
                ctx.beginPath()
                ctx.arc (cx, cy, rad, root.startRad, root.startRad + n * root.sweepRad)
                ctx.strokeStyle = root.accent
                ctx.lineWidth = 2.6
                ctx.lineCap = "round"
                ctx.stroke()
            }

            // needle: same angle, pointing outward from the centre
            const a = root.startRad + n * root.sweepRad
            ctx.beginPath()
            ctx.moveTo (cx + 2 * Math.cos (a), cy + 2 * Math.sin (a))
            ctx.lineTo (cx + 15 * Math.cos (a), cy + 15 * Math.sin (a))
            ctx.strokeStyle = root.accent
            ctx.lineWidth = 1.9
            ctx.lineCap = "round"
            ctx.stroke()

            // centre dot
            ctx.beginPath()
            ctx.arc (cx, cy, 2, 0, Math.PI * 2)
            ctx.fillStyle = root.accent
            ctx.fill()
        }

        Connections {
            target: root.param
            function onValueChanged() { ring.requestPaint() }
        }
        Component.onCompleted: requestPaint()
    }

    MouseArea {
        id: drag
        anchors.fill: ring
        cursorShape: Qt.SizeVerCursor
        property real startY: 0
        property real startNorm: 0

        onPressed: (m) => {
            startY = m.y
            startNorm = root.norm
            if (root.param) root.param.beginGesture()
        }
        onPositionChanged: (m) => {
            if (! root.param) return
            const fine = (m.modifiers & Qt.ShiftModifier) ? 4.0 : 1.0
            const n = startNorm + (startY - m.y) / (170 * fine)
            root.param.setNorm (Math.max (0, Math.min (1, n)))
        }
        onReleased: if (root.param) root.param.endGesture()
        onDoubleClicked: if (root.param) root.param.resetToDefault()

        // ---- wheel ------------------------------------------------------
        // Same target as dragging: the ring.  A notch moves a continuous
        // parameter by 0.04 of its travel, which is the step the EQ canvas
        // already uses for Q, so the two feel alike; Shift makes it 0.01.
        //
        // A discrete parameter moves exactly one item per notch instead, and it
        // is computed from the item INDEX rather than by adding 1/(n-1) to the
        // normalised value: that fraction is not exactly representable, so
        // repeated additions drift off an item boundary and a 3-way switch
        // eventually needs two notches to move.
        onWheel: (wheel) => {
            if (! root.param) { wheel.accepted = false; return }

            // angleDelta is in eighths of a degree and one detent is 120.
            // Devices that only report pixelDelta are accumulated against the
            // same threshold, so a trackpad moves more slowly instead of firing
            // a whole step for every pixel of travel.
            const dy = wheel.angleDelta.y !== 0 ? wheel.angleDelta.y
                                                : wheel.pixelDelta.y
            if (dy === 0) { wheel.accepted = false; return }

            root.wheelAccum += dy
            const notches = Math.trunc (root.wheelAccum / 120)
            if (notches === 0) { wheel.accepted = true; return }
            root.wheelAccum -= notches * 120

            if (! root.wheelHeld) {
                root.param.beginGesture()
                root.wheelHeld = true
            }
            // A flung wheel can deliver a large delta at once; the count is
            // capped so one event cannot spin a parameter through its range.
            const count = Math.min (Math.abs (notches), 32)
            const dir = notches > 0 ? 1 : -1
            const shift = (wheel.modifiers & Qt.ShiftModifier) !== 0
            for (let i = 0; i < count; ++i)
                root.applyWheel (dir, shift)
            wheelEnd.restart()
            wheel.accepted = true
        }
    }

    // Measured against the reference: the CSS .knob is a flex column
    // (ring 46 + gap 4 + .klab + gap 4 + .kval), and Qt's line boxes sit a
    // couple of pixels lower for the same ink.  These offsets put the glyphs
    // on the same rows as the page (label ink 731, value ink 750 at deck top).
    Text {
        id: lab
        // .knob gap is 4 px under a 46 px ring; with Inter the web puts the
        // label's ink on row 733 and this was on 730.
        y: 54
        anchors.horizontalCenter: parent.horizontalCenter
        width: root.width
        text: root.label
        color: OvTheme.faint
        font.family: OvTheme.uiFont
        font.pointSize: OvTheme.pt (10.5)
        font.letterSpacing: 0.6
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight
    }

    Text {
        id: val
        y: 71
        anchors.horizontalCenter: parent.horizontalCenter
        width: root.width
        text: root.shownText
        color: OvTheme.ink
        font.family: OvTheme.uiFont
        font.pointSize: OvTheme.pt (10.5)
        font.letterSpacing: 0.6
        horizontalAlignment: Text.AlignHCenter
    }
}
