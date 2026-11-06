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
    // Ground truth, straight from QML, for the --hud window: the size the
    // root item actually got, the scale that was computed, and every plot's
    // own geometry.  Reading these from C++ reports whatever findChild
    // returned; this reports what is drawn.
    readonly property string geomText: {
        let t = "root " + width.toFixed(0) + "x" + height.toFixed(0)
              + "  scaleFit " + scaleFit.toFixed(3)
              + "  design " + design.width.toFixed(0) + "x" + design.height.toFixed(0)
              + "  dscale " + design.scale.toFixed(3)
        t += "\nplotLayer " + plotLayer.width.toFixed(0) + "x" + plotLayer.height.toFixed(0)
        t += "  sc " + scView.width.toFixed(0) + "x" + scView.height.toFixed(0)
        t += "  pq " + pqView.width.toFixed(0) + "x" + pqView.height.toFixed(0)
        t += "\nsc node1 on screen: "
        const p = scView.mapToItem (null, scView.nodePosition (1))
        t += "(" + p.x.toFixed(0) + ", " + p.y.toFixed(0) + ")"
        return t
    }

    Component.onCompleted: OvMenu.host = design

    Item {
        id: design
        objectName: "design"
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
        objectName: "eqzone"
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
        objectName: "plotLayer"
                anchors.fill: parent

                // Do NOT set layer.enabled here.  A layered item renders its
                // children into an off-screen texture, and Qt then does not
                // deliver mouse events to those children — the EQ nodes
                // became impossible to grab.  It also made no measurable
                // difference to the frame cost.

                OvEqView {
                    id: scView
                    objectName: "scView"      // found by the --hud debug window
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
                    objectName: "pqView"
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
    // ---------------------------------------------------------------------------
    //  ONYXTEON_RULER=1 overlay: a labelled grid in WINDOW coordinates, drawn on
    //  top of everything.  A screenshot then shows exactly where each element sits
    //  without any inference from item geometry or mapping APIs.
    // ---------------------------------------------------------------------------
    Item {
        id: ruler
        visible: (typeof ovRuler !== "undefined") ? ovRuler : false
        anchors.fill: parent
        z: 99999

        Repeater {
            model: Math.ceil (ruler.height / 100)
            delegate: Item {
                y: index * 100
                width: ruler.width
                height: 1
                Rectangle { width: parent.width; height: 1; color: "#66ff8800" }
                Text {
                    x: 2; y: -14
                    text: "" + (index * 100)
                    color: "#ffcc00"; font.pixelSize: 13; font.bold: true
                }
            }
        }
        Repeater {
            model: Math.ceil (ruler.width / 200)
            delegate: Item {
                x: index * 200
                width: 1
                height: ruler.height
                Rectangle { width: 1; height: parent.height; color: "#6600ccff" }
                Text {
                    x: 2; y: 2
                    text: "" + (index * 200)
                    color: "#00ddff"; font.pixelSize: 13; font.bold: true
                }
            }
        }
    }
}
