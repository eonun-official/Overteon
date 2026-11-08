import QtQuick

// ============================================================================
//  The solo read-out: one rate per widget, measured by that widget.
//
//  ONYXTEON_SOLO=each (or a list) hides everything but the named widgets, and
//  this panel says how fast each of them is actually updating.  The numbers are
//  the widgets' own — a plot counts inside its paint(), the fog inside its own —
//  so they answer "how expensive is THIS", which a window frame rate cannot:
//  the window presents whether or not a given widget redrew.
//
//  The window's presented rate is shown too, but labelled, because it is a
//  different quantity: it is how often the scene was composited, and with
//  everything else hidden it is also the ceiling the widgets run under.
// ============================================================================
Item {
    id: root

    property var    rows: []          // [{name, fps, note, alpha}]
    property real   windowFps: 0      // presented frames, from a FrameAnimation
    property real   scale: 1.0        // design -> window, so it stays readable

    /// The α column only exists when something was actually faded, so it must
    /// not take width away from the rate in a normal solo run — and must get
    /// its own room when it does, rather than being clipped off the panel.
    readonly property bool anyAlpha: rows.some (r => r.alpha !== undefined)

    width: 250 + (anyAlpha ? 58 : 0)
    height: 26 + rows.length * 18
    visible: rows.length > 0
    z: 99998

    // Frames the scene actually composited.  FrameAnimation fires once per
    // frame the renderer produces, which is the only honest "presented" count.
    FrameAnimation {
        running: root.visible
        onTriggered: {
            root._n = (root._n || 0) + 1
            const now = Date.now()
            if (!root._t0) { root._t0 = now; return }
            const dt = (now - root._t0) / 1000
            if (dt >= 1.0) {
                root.windowFps = root._n / dt
                root._n = 0
                root._t0 = now
            }
        }
    }
    property int    _n: 0
    property double _t0: 0

    Rectangle {
        anchors.fill: parent
        color: "#cc0d0f13"
        border.color: "#3a4250"
        border.width: 1
        radius: 3
    }

    Column {
        x: 8; y: 6
        spacing: 2

        Text {
            text: "window " + root.windowFps.toFixed(1) + " fps"
            color: "#4a525f"
            font.pixelSize: 11
            font.family: "monospace"
        }

        Repeater {
            model: root.rows
            delegate: Row {
                spacing: 8
                Text {
                    text: modelData.name
                    color: "#7f8a9c"
                    font.pixelSize: 11
                    font.family: "monospace"
                    width: 96
                }
                Text {
                    // Two decimals below 1 so a widget that updates twice a
                    // second is distinguishable from one that never does.
                    text: modelData.fps === null
                          ? (modelData.note || "n/a")
                          : modelData.fps.toFixed(modelData.fps < 10 ? 2 : 1) + " fps"
                    color: modelData.fps === null
                           ? "#5a6473"
                           : (modelData.fps < 30 ? "#ff8a6a"
                                                 : (modelData.fps < 55 ? "#ffd166"
                                                                       : "#7ee08a"))
                    font.pixelSize: 11
                    font.family: "monospace"
                }
                Text {
                    // The alpha this widget is drawn at.  Only shown when
                    // ONYXTEON_SOLO_OPACITY faded something, so a capture
                    // always says how faint the picture was when it was
                    // taken — a diff against a full-strength shot would
                    // otherwise be noise.
                    visible: modelData.alpha !== undefined
                    text: visible ? "α " + modelData.alpha.toFixed(2) : ""
                    color: "#8a7bd8"
                    font.pixelSize: 11
                    font.family: "monospace"
                }
            }
        }
    }
}
