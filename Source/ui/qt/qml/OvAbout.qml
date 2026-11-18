import QtQuick

// #about modal: the 0.9.10 layout and content, with Xyas added to the credits.
Item {
    id: root

    property var engine: null
    signal closed()

    anchors.fill: parent
    visible: false

    Rectangle {
        anchors.fill: parent
        color: Qt.rgba (8 / 255, 9 / 255, 12 / 255, 0.72)
        MouseArea {
            anchors.fill: parent
            onClicked: root.close()
        }
    }

    Rectangle {
        id: box
        width: 450
        height: content.implicitHeight + 48
        anchors.centerIn: parent
        color: "#14161c"
        border.color: OvTheme.line
        radius: 10

        MouseArea { anchors.fill: parent }

        Column {
            id: content
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.leftMargin: 30
            anchors.rightMargin: 30
            anchors.topMargin: 26
            spacing: 10

            Image {
                source: "qrc:/res/eonun_logo.png"
                width: 150
                fillMode: Image.PreserveAspectFit
                anchors.horizontalCenter: parent.horizontalCenter
            }

            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                Text {
                    text: "OVER"
                    color: OvTheme.ink
                    font.family: OvTheme.displayFont
                    font.pixelSize: 17
                    font.letterSpacing: 2
                }
                Text {
                    text: "TEON"
                    color: OvTheme.sc
                    font.family: OvTheme.displayFont
                    font.pixelSize: 17
                    font.letterSpacing: 2
                }
            }

            Text {
                text: "v" + ovVersion + " · EONUN"
                color: OvTheme.faint
                font.family: OvTheme.uiFont
                font.pixelSize: 10
                font.letterSpacing: 1.5
                anchors.horizontalCenter: parent.horizontalCenter
                bottomPadding: 4
            }

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                color: OvTheme.dim
                font.family: OvTheme.uiFont
                font.pixelSize: 10
                lineHeight: 1.7
                textFormat: Text.StyledText
                text: "OVERTEON is inspired by passive equalisers, taken one step further: colour that never costs dynamics. The harmonics created by saturation are separated from the dry signal and merged back only at the output, so the dry keeps its own transients and support while the Post EQ tunes the combined result."
            }
            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                color: OvTheme.dim
                font.family: OvTheme.uiFont
                font.pixelSize: 10
                lineHeight: 1.7
                textFormat: Text.StyledText
                topPadding: 2
                text: "<b>HARMONIC</b> — the full design. The sidechain EQ shapes what the saturator hears; only the newly generated harmonics are extracted, and the dry passes through an identical idle oversampler so the merge is phase-exact. The Post EQ then tunes the result."
            }
            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                color: OvTheme.dim
                font.family: OvTheme.uiFont
                font.pixelSize: 10
                lineHeight: 1.7
                textFormat: Text.StyledText
                text: "<b>SCHRANZ</b> — the sidechain EQ is the output. Hard, dramatic shaping goes straight into the limiter; the saturator and Post EQ are bypassed, and punch, compression and overload colour come from the limiter itself."
            }
            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                color: OvTheme.dim
                font.family: OvTheme.uiFont
                font.pixelSize: 10
                lineHeight: 1.7
                textFormat: Text.StyledText
                text: "<b>CLASSIC</b> — the conventional route. The whole signal is saturated, blended with the dry by MIX, then tuned by the Post EQ."
            }

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                color: OvTheme.ink
                font.family: OvTheme.uiFont
                font.pixelSize: 10
                textFormat: Text.StyledText
                topPadding: 2
                text: "Produced by <font color='" + OvTheme.pq + "'><b>Eur9ka</b></font>, <font color='" + OvTheme.pq + "'><b>Eonun</b></font> &amp; <font color='" + OvTheme.pq + "'><b>Xyas</b></font> &nbsp;·&nbsp; Designed by <font color='" + OvTheme.pq + "'><b>Eonun</b></font>"
            }

            Item {
                width: parent.width
                height: siteText.implicitHeight + 6

                Text {
                    id: siteText
                    anchors.centerIn: parent
                    text: "eonun-official.github.io"
                    color: siteMa.containsMouse ? OvTheme.pq : OvTheme.ink
                    font.family: OvTheme.uiFont
                    font.pixelSize: 10
                    font.letterSpacing: 1

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.bottom
                        anchors.topMargin: 1
                        height: 1
                        color: siteMa.containsMouse ? OvTheme.pq : "#48e7ebf2"
                    }

                    MouseArea {
                        id: siteMa
                        anchors.fill: parent
                        anchors.margins: -4
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: Qt.openUrlExternally ("https://eonun-official.github.io")
                    }
                }
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
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
    }

    function open()  { visible = true }
    function close() { visible = false; root.closed() }

    Keys.onEscapePressed: root.close()
}
