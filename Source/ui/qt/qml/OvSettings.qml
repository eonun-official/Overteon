import QtQuick
import Overteon 1.0

// #settings modal — the display settings, reached by clicking the harmonic
// ring in the header.
//
// Everything here is a DISPLAY choice.  None of it reaches the DSP: the
// analyser smoothing (Source/ui/qt/OvSpectrum.h) and the meter ballistics
// (Source/ui/qt/OvBallistics.h) both run in the UI on the tables the engine
// produces, so switching a detector changes what is drawn and nothing else.
// That is why these are not parameters — a parameter would appear in a host's
// automation list and claim to be part of the sound.
//
// Structure follows OvAbout.qml: a full-size dim layer whose MouseArea closes,
// with a centred card.  Filled = the whole screen is a click target, so a stray
// click outside the card dismisses instead of hitting the EQ underneath.
Item {
    id: root

    property var engine: null
    signal closed()

    anchors.fill: parent
    visible: false

    function open()  { visible = true; forceActiveFocus() }
    function close() { visible = false; root.closed() }

    // ---- one selectable row --------------------------------------------
    //
    // A row is a label on the left and N segments on the right, one of which is
    // lit.  Not a ComboBox: with two or three options a set of segments shows
    // the current value AND the alternatives at once, which is what makes a
    // settings panel readable at a glance.
    component Seg: Row {
        id: segRow

        property string label: ""
        property var    options: []
        property int    current: 0
        property bool   isBool: false
        signal picked (int index)

        spacing: 6

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: segRow.label
            color: OvTheme.dim
            font.family: OvTheme.uiFont
            font.pixelSize: 11
            font.letterSpacing: 0.6
            width: 150
        }

        Repeater {
            model: segRow.options

            Rectangle {
                id: chip
                required property int index
                required property string modelData

                width: chipText.implicitWidth + 20
                height: 20
                radius: 5
                color: segRow.current === index ? OvTheme.panel : "transparent"
                border.width: 1
                border.color: segRow.current === index
                              ? (chipMa.containsMouse ? OvTheme.ink : OvTheme.line)
                              : OvTheme.line

                Text {
                    id: chipText
                    anchors.centerIn: parent
                    text: chip.modelData
                    color: segRow.current === chip.index ? OvTheme.ink : OvTheme.faint
                    font.family: OvTheme.uiFont
                    font.pixelSize: 10
                    font.letterSpacing: 0.8
                }

                MouseArea {
                    id: chipMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: segRow.picked (chip.index)
                }

            }
        }
    }

    // ---- one action chip ------------------------------------------------
    //
    // Unlike Seg's options, these are ACTIONS, not a persistent mode: ORIGINAL
    // and DEFAULT resize the window now.  The lit state only says "the window
    // is currently exactly this preset", which is why nothing lights while the
    // user has an arbitrary stretched size.
    component WinChip: Rectangle {
        id: winChip

        property string label: ""
        property bool   lit: false
        signal clicked()

        width: winChipText.implicitWidth + 20
        height: 20
        radius: 5
        color: winChip.lit ? OvTheme.panel : "transparent"
        border.width: 1
        border.color: winChip.lit
                      ? (chipMouse.containsMouse ? OvTheme.ink : OvTheme.line)
                      : OvTheme.line

        Text {
            id: winChipText
            anchors.centerIn: parent
            text: winChip.label
            color: winChip.lit ? OvTheme.ink : OvTheme.faint
            font.family: OvTheme.uiFont
            font.pixelSize: 10
            font.letterSpacing: 0.8
        }

        MouseArea {
            id: chipMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: winChip.clicked()
        }
    }

    // The limiter ring's RANGE, as a chip index.  Stored as the dB value so a
    // future option does not renumber the existing ones.
    readonly property var grRangeValues: [ 6, 12, 24, 48 ]
    readonly property int grRangeIndex: {
        const r = engine ? engine.grRange : 12
        const i = grRangeValues.indexOf (r)
        return i < 0 ? 1 : i
    }

    // Is the window currently exactly one of the presets?  tolerance 1.5 px:
    // the host may round the size it gives us.
    readonly property bool atOriginal: Math.abs (width  - OvTheme.designW) < 1.5
                                    && Math.abs (height - OvTheme.designH) < 1.5
    readonly property bool atDefault: engine !== null && engine.windowDefaultSaved
                                   && Math.abs (width  - engine.defaultWindowW) < 1.5
                                   && Math.abs (height - engine.defaultWindowH) < 1.5

    property bool defaultFlash: false
    Timer {
        id: defaultFlashTimer
        interval: 1200
        onTriggered: root.defaultFlash = false
    }

    // ---- dim layer -------------------------------------------------------
    Rectangle {
        anchors.fill: parent
        color: Qt.rgba (8 / 255, 9 / 255, 12 / 255, 0.72)
        MouseArea {
            anchors.fill: parent
            onClicked: root.close()
        }
    }

    // ---- card ------------------------------------------------------------
    Rectangle {
        id: box
        width: 520
        height: content.implicitHeight + 44
        anchors.centerIn: parent
        color: "#14161c"
        border.color: OvTheme.line
        radius: 10

        // Absorb clicks so the dim layer underneath does not close us.
        MouseArea { anchors.fill: parent }

        Column {
            id: content
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 22
            spacing: 12

            // ---- title ---------------------------------------------------
            Row {
                width: parent.width
                spacing: 10

                Text {
                    text: "DISPLAY"
                    color: OvTheme.ink
                    font.family: OvTheme.displayFont
                    font.pixelSize: 17
                    font.letterSpacing: 3
                }
                Text {
                    text: "SETTINGS"
                    color: OvTheme.sc
                    font.family: OvTheme.displayFont
                    font.pixelSize: 17
                    font.letterSpacing: 3
                }
                Item { width: parent.width - 260; height: 1 }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "CLOSE"
                    color: closeMa.containsMouse ? OvTheme.ink : OvTheme.dim
                    font.family: OvTheme.uiFont
                    font.pixelSize: 10
                    font.letterSpacing: 1.5

                    MouseArea {
                        id: closeMa
                        anchors.fill: parent
                        anchors.margins: -6
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.close()
                    }
                }
            }

            Rectangle { width: parent.width; height: 1; color: OvTheme.line }

            // ---- one MODE per meter --------------------------------------
            //
            // Detector and interpolation used to be two independent rows.  They
            // are one now: RMS means the power mean AND the eased display,
            // TRANSIENT means the loudest bin AND the raw display.  The mixed
            // combinations were not choices anyone wanted, and the harmonic ring
            // has no display smoothing at all — its mode only picks the
            // measurement (DSP-smoothed ratio vs. this block's ratio).
            Repeater {
                model: [
                    { title: "HARMONIC MONITOR", mode: "harmMode" },
                    { title: "UPPER SPECTRUM",   mode: "upperMode" },
                    { title: "LOWER SPECTRUM",   mode: "lowerMode" }
                ]

                Column {
                    required property var modelData
                    width: parent.width
                    spacing: 6

                    Text {
                        text: modelData.title
                        color: OvTheme.dim
                        font.family: OvTheme.uiFont
                        font.pixelSize: 10
                        font.letterSpacing: 1.4
                    }

                    Seg {
                        label: "MODE"
                        options: ["RMS", "TRANSIENT"]
                        current: root.engine ? root.engine[modelData.mode] : 1
                        onPicked: function (i) {
                            if (root.engine) root.engine[modelData.mode] = i
                        }
                    }
                }
            }

            Rectangle { width: parent.width; height: 1; color: OvTheme.line }

            // ---- level meters --------------------------------------------
            //
            // The composite meter's bright bar reads each block's SAMPLE PEAK
            // (not the 300 ms RMS), so INSTANT really is instant: stop the
            // audio and it is gone on the next block.  VU is the standard
            // movement — 300 ms to 99 % of a step, i.e. a 65 ms time constant.
            Column {
                width: parent.width
                spacing: 6

                Text {
                    text: "LEVEL METERS"
                    color: OvTheme.dim
                    font.family: OvTheme.uiFont
                    font.pixelSize: 10
                    font.letterSpacing: 1.4
                }

                Seg {
                    label: "RELEASE"
                    options: ["INSTANT", "VU 300 ms"]
                    current: root.engine ? root.engine.meterRelease : 0
                    onPicked: function (i) {
                        if (root.engine) root.engine.meterRelease = i
                    }
                }
            }

            Rectangle { width: parent.width; height: 1; color: OvTheme.line }

            // ---- limiter ring ---------------------------------------------
            //
            // The red arc on the harmonic ring is limiter gain reduction; this
            // is the dB a full counter-clockwise turn represents.  The two
            // scale labels on the ring are range/4 (9 o'clock) and 3*range/4
            // (3 o'clock).
            Column {
                width: parent.width
                spacing: 6

                Text {
                    text: "LIMITER RING"
                    color: OvTheme.dim
                    font.family: OvTheme.uiFont
                    font.pixelSize: 10
                    font.letterSpacing: 1.4
                }

                Seg {
                    label: "RANGE"
                    options: ["6 dB", "12 dB", "24 dB", "48 dB"]
                    current: root.grRangeIndex
                    onPicked: function (i) {
                        if (root.engine) root.engine.grRange = root.grRangeValues[i]
                    }
                }
            }

            Rectangle { width: parent.width; height: 1; color: OvTheme.line }

            // ---- window size --------------------------------------------
            //
            // ORIGINAL is the fixed 1040x772 design.  DEFAULT is the preset
            // written by SAVE AS DEFAULT (until one exists it behaves exactly
            // like ORIGINAL).  SAVE AS DEFAULT only records the CURRENT size:
            // the window still opens at the LAST-USED size, so a later free
            // stretch is what comes back next time unless the preset is saved
            // again.  Nothing here is persisted as a "mode".
            Column {
                width: parent.width
                spacing: 6

                Text {
                    text: "WINDOW SIZE"
                    color: OvTheme.dim
                    font.family: OvTheme.uiFont
                    font.pixelSize: 10
                    font.letterSpacing: 1.4
                }

                Row {
                    spacing: 6

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "PRESET"
                        color: OvTheme.dim
                        font.family: OvTheme.uiFont
                        font.pixelSize: 11
                        font.letterSpacing: 0.6
                        width: 150
                    }

                    WinChip {
                        label: "ORIGINAL"
                        lit: root.atOriginal
                        onClicked: if (root.engine) root.engine.windowSizeOriginal()
                    }
                    WinChip {
                        label: "DEFAULT"
                        lit: root.atDefault
                        onClicked: if (root.engine) root.engine.windowSizeDefault()
                    }
                    WinChip {
                        label: "SAVE AS DEFAULT"
                        lit: root.defaultFlash
                        onClicked: {
                            if (root.engine) root.engine.windowSizeSaveDefault()
                            root.defaultFlash = true
                            defaultFlashTimer.restart()
                        }
                    }
                }
            }

            // ---- diagnostics (only in -DOVERTEON_FILE_LOG=ON builds) -----
            //
            // Uploads this session's runtime log to a GitHub issue, or — when
            // no token is configured — copies the log to the clipboard and
            // opens the prefilled new-issue page.  windowSize* hides nothing
            // else here: an OFF build has no uploader and no row.
            Rectangle {
                width: parent.width; height: 1; color: OvTheme.line
                visible: root.engine ? root.engine.diagnosticsAvailable() : false
            }

            Column {
                width: parent.width
                spacing: 6
                visible: root.engine ? root.engine.diagnosticsAvailable() : false

                Text {
                    text: "DIAGNOSTICS"
                    color: OvTheme.dim
                    font.family: OvTheme.uiFont
                    font.pixelSize: 10
                    font.letterSpacing: 1.4
                }

                Row {
                    spacing: 6

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "PROBLEM"
                        color: OvTheme.dim
                        font.family: OvTheme.uiFont
                        font.pixelSize: 11
                        font.letterSpacing: 0.6
                        width: 150
                    }

                    WinChip {
                        label: "REPORT ISSUE (LOG)"
                        lit: false
                        onClicked: if (root.engine) root.engine.reportIssue()
                    }
                }

                Text {
                    width: parent.width
                    text: "Uploads this session's log to path86/bugreportREP under " +
                          "Ot/<version>/. Review it first: it contains your preset names " +
                          "and local paths."
                    color: OvTheme.faint
                    font.family: OvTheme.uiFont
                    font.pixelSize: 9
                    wrapMode: Text.WordWrap
                }
            }
        }
    }

    Keys.onEscapePressed: root.close()
}
