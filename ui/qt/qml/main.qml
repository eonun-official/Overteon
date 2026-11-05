import QtQuick
import OnyxTEON 1.0

// Overteon — Qt6/QML front end.
// Layout numbers come straight from web_fx/index.html (#wrap is 1040x772);
// the whole design is scaled uniformly into the view, so every control keeps
// the exact proportions of the HTML original at any window size.
Item {
    id: root

    // ------------------------------------------------------------------
    //  Render isolation.
    //
    //  Qt re-renders every QQuickPaintedItem in the window whenever *any*
    //  item changes — even when the item itself never called update().
    //  Measured: the two 1040x386 EQ plots were re-uploaded at the meter's
    //  62 Hz although their own content updates at 30 Hz, which cost ~50 % of
    //  a core.  Wrapping the (static) plot layer in a layer.enabled item
    //  makes it a separate texture the renderer can reuse untouched.
    // ------------------------------------------------------------------

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

            // The plot layer holds ONLY the two EQ canvases.  Every frosted
            // surface samples this subtree, so it must not contain glass
            // items — a ShaderEffectSource that captures its own siblings
            // renders as a washed-out slab.
            Item {
                id: plotLayer
                anchors.fill: parent

                // Keep the plots in their own texture so the meter's 62 Hz
                // updates do not force them to be re-uploaded (they update at
                // 30 Hz).  Measured cost of not doing this: ~25 % of a core.
                layer.enabled: true
                layer.smooth: false
                layer.mipmap: false

                OvEqView {
                    id: scView
                    x: 0; y: 0
                    width: parent.width
                    height: parent.height / 2
                    engine: root.engine
                    prefix: "sc"
                    accent: OvTheme.sc
                    headerTop: true
                    glow: root.outLight * root.outLight   // --light = level^2
                }

                OvEqView {
                    id: pqView
                    x: 0; y: parent.height / 2
                    width: parent.width
                    height: parent.height / 2
                    engine: root.engine
                    prefix: "pq"
                    accent: OvTheme.pq
                    headerTop: false
                    glow: root.inLight * root.inLight
                }
            }

            // #hdr::before — the elliptical frost on the seam.  It spans both
            // plots, so it is painted from both of their images at once.
            OvSeamFrost {
                id: seamFrost
                width: 550
                height: 68
                x: (parent.width - width) / 2
                y: parent.height / 2 - height / 2
                topView: scView
                bottomView: pqView
            }

            // the bars, controls and tooltips sit above the plots
            Item {
                id: eqLayer
                anchors.fill: parent

                OvEqSection {
                    x: 0
                    y: 0
                    width: parent.width
                    height: parent.height / 2
                    view: scView
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
                    view: pqView
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
                sourceItem: plotLayer
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
