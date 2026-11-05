import QtQuick

// #about modal: logo, wordmark, version, three paragraphs, credits, close.
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

        Column {
            id: content
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 26
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
                    text: "ONYX"
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
                text: "v0.9.14 · EONUN"
                color: OvTheme.faint
                font.family: OvTheme.uiFont
                font.pixelSize: 10
                font.letterSpacing: 1.5
                anchors.horizontalCenter: parent.horizontalCenter
            }

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                color: OvTheme.dim
                font.family: OvTheme.uiFont
                font.pixelSize: 10
                lineHeight: 1.7
                text: "The moment saturation occurs, the topology engine wakes. It extracts only the harmonics the saturator has just created — sample-accurate, inside the oversampled domain — while your dry signal passes completely untouched."
            }
            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                color: OvTheme.dim
                font.family: OvTheme.uiFont
                font.pixelSize: 10
                lineHeight: 1.7
                text: "This additive-only design solves phase interference and pre-ringing at the same time: the original signal is never subtracted from or re-phased, so there is no phase conflict to fix."
            }

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                color: OvTheme.ink
                font.family: OvTheme.uiFont
                font.pixelSize: 10
                textFormat: Text.StyledText
                text: "Produced by <b>Eur9ka</b> &amp; <b>Eonun</b> · Designed by <b>Eonun</b>"
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                color: OvTheme.dim
                font.family: OvTheme.uiFont
                font.pixelSize: 10
                text: "COPY QML UI · EONUN"
                opacity: 0.6
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "CLOSE"
                color: closeMa.containsMouse ? OvTheme.ink : OvTheme.dim
                font.family: OvTheme.uiFont
                font.pixelSize: 10
                font.letterSpacing: 1.5
                topPadding: 6

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
}
