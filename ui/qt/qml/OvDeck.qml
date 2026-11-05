import QtQuick

// Bottom control deck — the .drow.dknobs row of the design:
// eight bare knobs with two hairline separators, laid out with the CSS rule
//   gap: 14px; justify-content: space-between; padding: 0 6px
// inside #deck's 10px 16px 11px padding.
//
// The slots are placed explicitly (no Row) so the row can never overflow the
// window: the first knob always starts at 22 px and the last one always ends
// at width - 22 px, whatever the width.
Item {
    id: root

    property var engine: null
    property int driveMode: 1

    readonly property bool schranz: driveMode === 2

    // slot widths: knob, knob, knob, knob, sep, knob, knob, sep, knob, knob
    readonly property var slotWidths: [ 58, 58, 58, 58, 1, 58, 58, 1, 58, 58 ]

    readonly property real totalWidth: {
        let t = 0
        for (const w of slotWidths) t += w
        return t
    }

    // CSS: 16 px side padding + 6 px row padding, 14 px minimum gap
    readonly property real innerWidth: Math.max (0, width - 32 - 12)
    readonly property real slotGap: {
        const gaps = slotWidths.length - 1
        const extra = (innerWidth - totalWidth - 14 * gaps) / gaps
        return 14 + Math.max (0, extra)
    }

    function slotX (index) {
        let x = 16 + 6
        for (let i = 0; i < index; ++i)
            x += slotWidths[i] + slotGap
        return x
    }

    readonly property real knobTop: Math.max (0, (height - 77) / 2)

    function p (id) { return engine ? engine.param (id) : null }



    // ---- four saturator-side knobs ------------------------------------
    OvKnob { x: root.slotX (0); y: root.knobTop
             param: root.p ("inGain");   label: "INPUT";     accent: OvTheme.sc
             fmt: v => (v >= 0 ? "+" : "") + v.toFixed (1) }
    OvKnob { x: root.slotX (1); y: root.knobTop
             param: root.p ("drive");    label: "DRIVE";     accent: OvTheme.sc
             dimmed: root.schranz;  fmt: v => v.toFixed (2) }
    OvKnob { x: root.slotX (2); y: root.knobTop
             param: root.p ("evenOdd");  label: "EVEN/ODD";  accent: OvTheme.amber
             dimmed: root.schranz
             fmt: v => Math.abs (v) < 0.05 ? "BAL"
                     : (v < 0 ? "E " + Math.round (-v * 100) : "O " + Math.round (v * 100)) }
    OvKnob { x: root.slotX (3); y: root.knobTop
             param: root.p ("mix");      label: "HARMONICS"; accent: OvTheme.sc
             dimmed: root.schranz;  fmt: v => Math.round (v * 100) + "%" }

    Rectangle { x: root.slotX (4); width: 1; height: 44; color: OvTheme.line
                anchors.verticalCenter: parent.verticalCenter }

    OvKnob { x: root.slotX (5); y: root.knobTop
             param: root.p ("harmGain"); label: "HARM GAIN"; accent: OvTheme.amber
             dimmed: root.schranz;  fmt: v => (v >= 0 ? "+" : "") + v.toFixed (1) }
    OvKnob { x: root.slotX (6); y: root.knobTop
             param: root.p ("deEmph");   label: "DE-EMPH";   accent: OvTheme.amber
             dimmed: root.schranz;  fmt: v => Math.round (v * 100) + "%" }

    Rectangle { x: root.slotX (7); width: 1; height: 44; color: OvTheme.line
                anchors.verticalCenter: parent.verticalCenter }

    OvKnob { x: root.slotX (8); y: root.knobTop
             param: root.p ("airProtect"); label: "AIR PROTECT"; accent: OvTheme.pq
             dimmed: root.schranz
             fmt: v => v >= 19500 ? "OFF"
                     : (v >= 1000 ? (v / 1000).toFixed (1) + "k" : "" + Math.round (v)) }
    OvKnob { x: root.slotX (9); y: root.knobTop
             param: root.p ("outGain");  label: "OUTPUT";    accent: OvTheme.pq
             fmt: v => (v >= 0 ? "+" : "") + v.toFixed (1) }
}
