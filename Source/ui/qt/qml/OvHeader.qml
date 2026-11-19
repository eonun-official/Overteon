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

    // ---- one header control ---------------------------------------------
    //
    // Text, a hairline that marks the live state, and a hit area larger than
    // the glyphs.  Declared once so every label in the right-hand strip shares
    // exactly the same face, size, tracking and baseline.
    component Ctrl: Item {
        id: ctrl

        property alias text: ctrlText.text
        property bool active: false
        property color activeBar: OvTheme.pq
        signal clicked()

        width: ctrlText.implicitWidth + 4
        height: 22

        Text {
            id: ctrlText
            anchors.centerIn: parent
            color: ctrl.active ? OvTheme.ink
                 : (ctrlMa.containsMouse ? OvTheme.ink : OvTheme.dim)
            font.family: OvTheme.displayFont
            font.pixelSize: 13
            font.letterSpacing: 0.6
        }

        Rectangle {
            anchors.left: ctrlText.left
            anchors.right: ctrlText.right
            anchors.top: ctrlText.bottom
            anchors.topMargin: 2
            height: 1
            color: ctrl.activeBar
            visible: ctrl.active
        }

        MouseArea {
            id: ctrlMa
            anchors.fill: parent
            anchors.margins: -5
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: ctrl.clicked()
        }
    }

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
            // 只让中央谐波环成为 Settings 入口。此前热区覆盖整个
            // 550x121 仪表（含光晕留白），上下 EQ 边缘的节点经常被它抢走。
            width: 96
            height: 60
            anchors.centerIn: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.settingsRequested()
        }
    }

    // ---- global controls -------------------------------------------------
    // One right-aligned strip in the same display face (EDIX) at the same
    // size and tracking throughout.  Anchored to the right edge rather than a
    // fixed x, so INIT always sits against the frame however the labels grow.
    Row {
        id: globals
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        spacing: 9
        z: 5

        Ctrl {
            text: "BYP"
            active: root.engine ? root.engine.bypassed : false
            activeBar: OvTheme.amber
            onClicked: if (root.engine) root.engine.bypassed = ! root.engine.bypassed
        }

        // A|B — one control for two slots instead of two chips and a gap.
        // The live slot carries the hairline under its letter: teal for A,
        // pink for B.  Clicking switches to the other slot.
        Item {
            id: abCtl
            width: abRow.width + 6
            height: 22

            readonly property bool bLive: root.engine ? root.engine.abSlot === 1 : false

            Row {
                id: abRow
                anchors.centerIn: parent
                spacing: 7

                Text {
                    id: abA
                    text: "A"
                    color: abCtl.bLive ? (abMa.containsMouse ? OvTheme.dim : OvTheme.faint)
                                       : OvTheme.ink
                    font.family: OvTheme.displayFont
                    font.pixelSize: 13
                    font.letterSpacing: 0.6

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.bottom
                        anchors.topMargin: 2
                        height: 1
                        color: OvTheme.pq
                        visible: ! abCtl.bLive
                    }
                }

                Rectangle {
                    width: 1
                    height: 11
                    y: 3
                    color: OvTheme.line
                }

                Text {
                    id: abB
                    text: "B"
                    color: abCtl.bLive ? OvTheme.ink
                         : (abMa.containsMouse ? OvTheme.dim : OvTheme.faint)
                    font.family: OvTheme.displayFont
                    font.pixelSize: 13
                    font.letterSpacing: 0.6

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.bottom
                        anchors.topMargin: 2
                        height: 1
                        color: OvTheme.sc
                        visible: abCtl.bLive
                    }
                }
            }

            MouseArea {
                id: abMa
                anchors.fill: parent
                anchors.margins: -5
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: if (root.engine) root.engine.abSlot = abCtl.bLive ? 0 : 1
            }
        }

        // Copy the live slot into the other one.  Written "A>B" rather than a
        // drawn arrow: one ASCII glyph the display face already has is narrower
        // than any line-and-chevron construction, and it leaves the strip room
        // to carry a larger size.
        Ctrl {
            text: root.engine && root.engine.abSlot === 1 ? "B>A" : "A>B"
            onClicked: if (root.engine) root.engine.abCopyFrom (root.engine.abSlot)
        }

        Ctrl {
            text: "INIT"
            onClicked: if (root.engine) root.engine.initAll()
        }
    }
}
