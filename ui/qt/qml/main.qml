import QtQuick

// Overteon — Qt6/QML front end.
// Layout numbers come straight from web_fx/index.html (#wrap is 1040x772);
// the whole design is scaled uniformly into the view, so every control keeps
// the exact proportions of the HTML original at any window size.
Item {
    id: root

    property var engine: null

    width: 1040
    height: 772

    Rectangle { anchors.fill: parent; color: OvTheme.bg }

    readonly property real scaleFit: Math.min (width / OvTheme.designW,
                                              height / OvTheme.designH)

    // menus are plain items parented here (see OvMenu): Qt Popup would ignore
    // the design scale and appear at the wrong size and position
    Component.onCompleted: OvMenu.host = design

    Item {
        id: design
        width: OvTheme.designW
        height: OvTheme.designH
        anchors.centerIn: parent
        scale: root.scaleFit
        transformOrigin: Item.Center

        readonly property real deckH: OvTheme.deckH
        readonly property real eqH: height - deckH

        // --------------------------------------------------------- EQ zone
        Item {
            id: eqzone
            x: 0
            y: 0
            width: parent.width
            height: parent.eqH

            // the layer the header frost samples from (no header in here)
            Item {
                id: eqLayer
                anchors.fill: parent

                OvEqSection {
                    x: 0
                    y: 0
                    width: parent.width
                    height: parent.height / 2
                    engine: root.engine
                    prefix: "sc"
                    headerTop: true
                    accent: OvTheme.sc
                    title: "SIDECHAIN EQ"
                    driveMode: root.driveMode
                    meterLight: root.outLight
                }

                OvEqSection {
                    x: 0
                    y: parent.height / 2
                    width: parent.width
                    height: parent.height / 2
                    engine: root.engine
                    prefix: "pq"
                    headerTop: false
                    accent: OvTheme.pq
                    title: "POST EQ"
                    driveMode: root.driveMode
                    meterLight: root.inLight
                }
            }

            OvHeader {
                x: 0
                y: parent.height / 2 - height / 2
                width: parent.width
                engine: root.engine
                sourceItem: eqLayer
                schranz: root.driveMode === 2
                onAboutRequested: about.open()
            }
        }

        // ------------------------------------------------------------- deck
        Rectangle {
            x: 0
            y: parent.eqH
            width: parent.width
            height: parent.deckH
            color: OvTheme.bg2

            Rectangle { width: parent.width; height: 1; color: OvTheme.line }

            OvDeck {
                anchors.fill: parent
                anchors.topMargin: 1
                engine: root.engine
                driveMode: root.driveMode
            }
        }

        // transparent catcher: a click anywhere outside closes the open menu
        MouseArea {
            anchors.fill: parent
            z: 9000
            visible: OvMenu.open !== null
            onClicked: OvMenu.close()
            onWheel: OvMenu.close()
        }
    }

    OvAbout { id: about; engine: root.engine }

    readonly property int driveMode: {
        if (! engine) return 1
        const p = engine.param ("driveMode")
        return p ? Math.round (p.norm * 2) : 1
    }

    readonly property real outLight: Math.max (root.level (engine ? engine.outL : -60),
                                               root.level (engine ? engine.outR : -60))
    readonly property real inLight:  Math.max (root.level (engine ? engine.inL : -60),
                                               root.level (engine ? engine.inR : -60))

    function level (db) { return Math.max (0, Math.min (1, (db + 60) / 60)) }

    Keys.onEscapePressed: { OvMenu.close(); about.close() }
    focus: true
}
