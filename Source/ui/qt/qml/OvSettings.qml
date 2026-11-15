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
            width: 168
        }

        Repeater {
            model: segRow.options

            Rectangle {
                id: chip
                required property int index
                required property string modelData

                width: chipText.implicitWidth + 22
                height: 24
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

                Behavior on color { ColorAnimation { duration: 90 } }
            }
        }
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
        width: 560
        height: content.implicitHeight + 52
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
            anchors.margins: 26
            spacing: 16

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

            // ---- interpolation ------------------------------------------
            //
            // ---- one block per meter ------------------------------------
            //
            // Detector and interpolation belong to the METER, not to the panel.
            // "Raw" is not a mode of the whole UI: it is a choice about how one
            // meter's value is produced, in the same category as RMS vs peak —
            // so each meter carries both, and picking raw on the ring leaves the
            // two analysers easing as before.
            Repeater {
                model: [
                    { title: "HARMONIC MONITOR", det: "harmDetector",
                      itp: "harmInterp" },
                    { title: "UPPER SPECTRUM",   det: "upperDetector",
                      itp: "upperInterp" },
                    { title: "LOWER SPECTRUM",   det: "lowerDetector",
                      itp: "lowerInterp" }
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
                        label: "DETECTOR"
                        options: ["RMS", "TRUE PEAK"]
                        current: root.engine ? root.engine[modelData.det] : 0
                        onPicked: function (i) {
                            if (root.engine) root.engine[modelData.det] = i
                        }
                    }

                    Seg {
                        label: "INTERPOLATION"
                        options: ["SMOOTH", "NONE"]
                        current: root.engine ? root.engine[modelData.itp] : 0
                        onPicked: function (i) {
                            if (root.engine) root.engine[modelData.itp] = i
                        }
                    }
                }
            }

            Rectangle { width: parent.width; height: 1; color: OvTheme.line }

            // ---- what each choice means ----------------------------------
            Text {
                width: parent.width
                color: OvTheme.faint
                font.family: OvTheme.uiFont
                font.pixelSize: 9
                lineHeight: 1.35
                wrapMode: Text.WordWrap
                textFormat: Text.StyledText
                text: "<b>RMS</b> averages energy over the meter's release window — " +
                      "what a level normally shows.  <b>True peak</b> oversamples 4× " +
                      "(BS.1770 reconstruction) and holds the loudest bin, so an " +
                      "inter-sample peak reads where it actually is instead of several " +
                      "dB low.  Both use the same release, so the two traces decay " +
                      "together.  <b>Raw</b> removes the display ramp entirely: the " +
                      "value on screen is the last measurement, nothing interpolated."
            }
        }
    }

    Keys.onEscapePressed: root.close()
}
