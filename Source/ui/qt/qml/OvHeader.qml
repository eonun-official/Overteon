import QtQuick
import Overteon 1.0

// The floating header that sits on the seam between the two EQ halves:
// OVER|TEON wordmark, the curved meter pod with the harmonic ring, INIT.
// The soft frost behind the pod is .hdr::before in the design: an elliptical
// backdrop blur of the EQ canvas underneath.
Item {
    id: root

    property var engine: null
    property Item sourceItem: null      // the EQ layer to frost
    property bool schranz: false

    signal aboutRequested()
    signal settingsRequested()

    height: OvTheme.headerH
    width: parent ? parent.width : 0

    // The elliptical frost on the seam (#hdr::before) is painted by the two
    // OvEqView halves — each one draws its own half of the 550x68 strip, so
    // the blur comes from the same pixels as the plot and cannot fall out of
    // sync.  Nothing is drawn here.

    // ---- wordmark ------------------------------------------------------
    Item {
        id: logo
        anchors.left: parent.left
        anchors.leftMargin: 32
        anchors.verticalCenter: parent.verticalCenter
        width: logoRow.width
        height: logoRow.height

        Row {
            id: logoRow
            Text {
                text: "OVER"
                color: OvTheme.ink
                font.family: OvTheme.displayFont
                font.pixelSize: 18
                font.letterSpacing: 2
            }
            Text {
                text: "TEON"
                color: OvTheme.sc
                font.family: OvTheme.displayFont
                font.pixelSize: 18
                font.letterSpacing: 2
            }
        }

        // ---- hover tip ---------------------------------------------------
        //
        // A short About beside the pointer, so the wordmark explains itself
        // without a click.  Deliberately a Rectangle rather than a popup: a
        // real Popup is a second window (on some platforms), takes focus and
        // sits above the whole scene; this must appear next to the logo and
        // vanish the moment the pointer leaves.
        //
        // The 260 ms delay is the point of it existing as a delay: a tip that
        // appeared instantly would flash every time the pointer crossed the
        // wordmark on the way somewhere else.
        Rectangle {
            id: tip
            visible: opacity > 0
            opacity: 0

            x: 0
            y: logo.height + 10
            width: tipText.implicitWidth + 22
            height: tipText.implicitHeight + 16
            radius: 6
            color: "#0d0f13"
            border.color: OvTheme.line
            border.width: 1

            Behavior on opacity { NumberAnimation { duration: 110 } }

            Text {
                id: tipText
                anchors.centerIn: parent
                text: "ABOUT OVER|TEON"
                color: OvTheme.dim
                font.family: OvTheme.uiFont
                font.pixelSize: 9
                font.letterSpacing: 1.2
            }

            Timer {
                id: tipDelay
                interval: 260
                onTriggered: tip.opacity = 1
            }
        }

        MouseArea {
            id: logoMa
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onEntered: tipDelay.start()
            onExited: { tipDelay.stop(); tip.opacity = 0 }
            onClicked: root.aboutRequested()
        }
    }

    // ---- meter pod -----------------------------------------------------
    //
    // The pod doubles as the Settings entry.  Hovering it turns the monitor
    // ring green and swaps the percentage read-out for the word SETTINGS;
    // leaving restores both.  The ring keeps drawing behind the label, so it
    // still reads as a meter while it advertises the panel — an entry that
    // blanked the meter would be a button wearing the meter's clothes.
    OvMeterView {
        id: pod
        settingsHover: podMa.containsMouse
        width: Math.min (550, root.width * 0.55)
        // The art is 64/480 of the width, but its halo is a real Gaussian
        // (stdDeviation 2.5 in viewBox units) that spills past the art — the
        // SVG's #hpod carries overflow:visible for exactly that.  A painted
        // item clips at its own bounds, so the item is 24 px taller at each
        // end with the art centred inside it.
        height: width * 64.0 / 480.0 + 48
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        engine: root.engine

        MouseArea {
            id: podMa
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.settingsRequested()
        }
    }

    // ---- INIT ----------------------------------------------------------
    Text {
        id: initBtn
        anchors.right: parent.right
        anchors.rightMargin: 16
        anchors.verticalCenter: parent.verticalCenter
        text: "INIT"
        color: initMa.containsMouse ? OvTheme.ink : OvTheme.dim
        font.family: OvTheme.uiFont
        font.pixelSize: 10
        font.letterSpacing: 1.5

        MouseArea {
            id: initMa
            anchors.fill: parent
            anchors.margins: -4
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: if (root.engine) root.engine.initAll()
        }
    }

    // ---- global controls -------------------------------------------------
    //
    // Bypass, the A/B slot, and the copy button.  Parked on the header's right
    // edge beside INIT rather than in the deck: the deck's eight knobs already
    // divide its width exactly, and these three are switches, not parameters —
    // they belong with the other whole-plug-in controls.
    //
    // Laid out right-to-left so INIT stays at the same margin whatever these
    // come to; a Row anchored right would push INIT around as labels change
    // width, and the copy button's label does change width ("A → B" / "B → A").
    Row {
        id: globals
        // Explicit x, not anchored to INIT's left edge.  The Row's width depends
        // on the copy button's label ("A → B" and "B → A" are different widths),
        // so anchoring its right edge made BYP drift left and right as the slot
        // changed — far enough to land under the pod.  The header is a fixed
        // 1040-wide design scaled uniformly, so a fixed left edge is the stable
        // choice, and it keeps the whole row clear of the pod's 245..795 span.
        x: 800
        anchors.verticalCenter: parent.verticalCenter
        spacing: 12

        // Above the pod, not merely after it in the file.  The pod's MouseArea
        // fills its whole 550 px width, which reaches to x = 795, and the Row's
        // leftmost control used to sit inside that — so a click meant for the
        // control opened the settings panel instead.  Drawing order alone did
        // not settle it: the pod is a sibling declared earlier, so its MouseArea
        // took the press first.  z decides hit-testing between siblings.
        z: 5

        // ---- bypass ------------------------------------------------------
        // A toggle, not a momentary: the state is what matters, and it is the
        // same switch the host sees through getBypassParameter().
        Text {
            id: bypBtn
            anchors.verticalCenter: parent.verticalCenter
            text: "BYP"
            color: (root.engine && root.engine.bypassed) ? OvTheme.amber
                   : (bypMa.containsMouse ? OvTheme.ink : OvTheme.dim)
            font.family: OvTheme.uiFont
            font.pixelSize: 10
            font.letterSpacing: 1.5

            Rectangle {
                // A hairline under the word while bypass is engaged: colour alone
                // is not enough at this size, and the underline survives a
                // monochrome screenshot.
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.bottom
                anchors.topMargin: 2
                height: 1
                color: OvTheme.amber
                visible: root.engine ? root.engine.bypassed : false
            }

            MouseArea {
                id: bypMa
                anchors.fill: parent
                anchors.margins: -6
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: if (root.engine) root.engine.bypassed = ! root.engine.bypassed
            }
        }

        // ---- A / B -------------------------------------------------------
        //
        // Two buttons, both always on screen; you pick one.  A single letter
        // that toggles on click is a different control -- it hides the fact that
        // two slots exist.
        //
        //   left click   select that slot
        //   right click  copy THAT slot onto the other one
        //
        // Right-click carries the copy because it destroys what the other slot
        // holds, so it must not be a stray left click away.  The copy source is
        // the chip that was right-clicked, not "whatever is live".
        Row {
            id: abGroup
            anchors.verticalCenter: parent.verticalCenter
            spacing: 5

            Repeater {
                model: ["A", "B"]

                Text {
                    id: abChip
                    required property int index
                    required property string modelData

                    readonly property bool live: root.engine
                                                 ? root.engine.abSlot === abChip.index
                                                 : abChip.index === 0

                    anchors.verticalCenter: parent.verticalCenter
                    text: abChip.modelData
                    color: abChip.live ? OvTheme.ink
                         : (abMa.containsMouse ? OvTheme.dim : OvTheme.faint)
                    font.family: OvTheme.displayFont
                    font.pixelSize: 14
                    font.letterSpacing: 1

                    // The live slot is the one with a line under it.  Both
                    // letters keep their position, so picking the other reads as
                    // a selection rather than a relabelling.
                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.bottom
                        anchors.topMargin: 1
                        height: 1
                        color: OvTheme.pq
                        visible: abChip.live
                    }

                    MouseArea {
                        id: abMa
                        anchors.fill: parent
                        anchors.margins: -5
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onClicked: function (m) {
                            if (! root.engine) return
                            if (m.button === Qt.RightButton) {
                                root.engine.abCopyFrom (abChip.index)
                                flashTimer.chipX = abChip.x
                                abFlash.opacity = 1
                                flashTimer.restart()
                            } else {
                                root.engine.abSlot = abChip.index
                            }
                        }
                    }
                }
            }

            // A frame around the chip that was copied FROM.  The copy writes to
            // the slot that is NOT on screen, so without this the right-click
            // looks like it did nothing at all.
            Rectangle {
                id: abFlash
                width: 16
                height: 20
                y: 0
                x: flashTimer.chipX - 3
                radius: 3
                color: "transparent"
                border.color: OvTheme.amber
                border.width: 1
                opacity: 0
                Behavior on opacity { NumberAnimation { duration: 180 } }
            }

            Timer {
                id: flashTimer
                property real chipX: 0
                interval: 520
                onTriggered: abFlash.opacity = 0
            }
        }
    }
}
