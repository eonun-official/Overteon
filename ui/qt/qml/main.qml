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

    // ONYXTEON_CPUFROST=1 falls back to the QImage glass, for comparisons.
    property bool cpuFrost: (typeof ovCpuFrost !== "undefined") ? ovCpuFrost : false
    // ONYXTEON_NOFROST: no glass of any kind, for reference captures.
    property bool anyFrost: ! ((typeof ovNoFrost !== "undefined") && ovNoFrost)

    // The root fills the window, whatever size it is.
    //
    // A constant 1040x772 was the design size, and the view was supposed to
    // overwrite it (SizeRootObjectToView).  It does not always: with --size, or
    // when a host resizes the editor, the root stayed at the design size while
    // the window was smaller, and the UI was a cropped slab in the corner —
    // resizing the window changed nothing but the crop.  Binding to the parent
    // (the window's contentItem, which Qt does keep at window size) makes the
    // root follow the window by construction.
    width: (parent && parent.width > 0) ? parent.width : OvTheme.designW
    height: (parent && parent.height > 0) ? parent.height : OvTheme.designH

    Rectangle { anchors.fill: parent; color: OvTheme.bg }

    // The design box is the window divided by the fit scale.
    //
    // Two things have to be true at once: nothing is ever cut off, and nothing
    // is ever letterboxed.  scaleFit alone (min of the two ratios) guarantees
    // the first — the design is scaled until one axis exactly fits — but it
    // leaves the other axis short, and the window showed a strip of body
    // background beside the UI.  Growing the design box by 1/scaleFit in BOTH
    // axes fills the window exactly: the width and height that 1040x772 does
    // not cover become extra room for the fluid regions (the two EQ plots, the
    // deck strip), while every control keeps its exact proportions because the
    // scale itself never changes per-axis.  No non-uniform stretch, so no
    // distorted glyphs and no stretched knob rings.
    //
    // Concretely, at 1040x772 scale is 1 and the box is exactly the design; at
    // 1200x820 the box becomes 1130x772 and the plots are 90 px wider; at
    // 700x480 the box becomes 1126x772 and the deck stays put while the plots
    // grow.
    readonly property real fitScale: Math.max (1e-6,
        Math.min (width / OvTheme.designW, height / OvTheme.designH))

    // How far the EQ overflow fog reaches past the plot edge, in design-box
    // pixels.  224 is the page's `range`; dividing by the fit scale keeps it
    // 224 screen px at every window size — the reach and the blur profile are
    // a treatment, not layout, the same reasoning as the seam band below.
    readonly property real fogRange: 224 / fitScale

    // Snap a length in the design box's pixels to whole SCREEN pixels.
    //
    // The design box is scaled by a fractional fitScale, so a plot edge at
    // 309.4 design px lands at 331.06 screen px — a different sub-pixel offset
    // at every window size, which made the fog's edges crawl against the plot's
    // while a window was being dragged.  Rounding in screen space and dividing
    // back puts them on the pixel grid the plot is actually drawn on.
    //
    // Declared on the root on purpose: a function defined on a nested item is
    // NOT visible to that item's children (QML resolves unqualified names
    // against the component root), which silently made the bindings undefined.
    function snapPx (designPx) {
        return Math.round (designPx * fitScale) / fitScale
    }

    // ---- ONYXTEON_SOLO: show only the named widgets ----------------------
    //
    // A frame rate for the whole window cannot say WHERE the time goes.  This
    // lets a run hide everything except one widget, so the cost can be
    // attributed instead of guessed:
    //
    //     ONYXTEON_SOLO=plots      only the two EQ plots
    //     ONYXTEON_SOLO=fog        only the overflow fog
    //     ONYXTEON_SOLO=seam       only the seam glass
    //     ONYXTEON_SOLO=deck       only the bottom strip
    //
    // Empty means "everything", which is the normal case.
    readonly property var soloList: {
        const s = (typeof ovSolo !== "undefined") ? String (ovSolo) : ""
        if (!s) return null
        return s.split (",").map (x => x.trim()).filter (x => x.length > 0)
    }
    /// Is this widget allowed to be visible?  Invisible names are hidden, and
    /// the rest of the UI goes with them so nothing else is drawing.
    function soloed (name) {
        return soloList === null || soloList.indexOf (name) >= 0
    }

    // ---- ONYXTEON_SOLO_OPACITY: fade what survives the solo ---------------
    //
    // Solo HIDES every widget it was not given.  That is right for attributing
    // a frame cost, but useless for comparing two renders: one widget per run
    // means you can never see where two of them disagree.  An opacity keeps the
    // solo path exactly as it is — same composite, same probes, same numbers —
    // and lets the named widgets be seen through:
    //
    //     ONYXTEON_SOLO=plots,fog ONYXTEON_SOLO_OPACITY=0.5
    //         both at half strength, so a one-pixel disagreement reads as a
    //         doubled edge instead of being hidden behind the top one
    //
    //     ONYXTEON_SOLO_OPACITY=plots=1,fog=0.25,seam=0.4
    //         per widget; a bare number sets the default for the rest
    //
    // A value of 1 (the default) is no fade at all, and with no ONYXTEON_SOLO
    // the setting is ignored entirely — a normal run is untouched, because
    // fading the real UI is not something this switch should ever do by itself.
    readonly property var soloOpacityMap: {
        const spec = (typeof ovSoloOpacity !== "undefined") ? String (ovSoloOpacity) : ""
        const m = {}
        if (! spec) return m
        for (const part of spec.split (",")) {
            const s = part.trim ()
            if (! s) continue
            const eq = s.indexOf ("=")
            if (eq < 0) {
                const v = Number (s)
                if (! isNaN (v)) m["*"] = Math.max (0, Math.min (1, v))
            } else {
                const k = s.slice (0, eq).trim ()
                const v = Number (s.slice (eq + 1))
                if (k && ! isNaN (v)) m[k] = Math.max (0, Math.min (1, v))
            }
        }
        return m
    }

    /// The opacity a soloed widget is drawn at.  Outside a solo it is 1, which
    /// is what makes this a debug switch and not a feature.
    function soloOpacityFor (name) {
        if (soloList === null) return 1
        if (soloOpacityMap[name] !== undefined) return soloOpacityMap[name]
        if (soloOpacityMap["*"]     !== undefined) return soloOpacityMap["*"]
        return 1
    }

    /// One call per layer: soloed widgets get their configured opacity, the
    /// rest are fully transparent (they are already `visible: false`; the 0 is
    /// only so a binding cannot outlive the solo and leave half a picture).
    function soloAlpha (name) {
        return soloed (name) ? soloOpacityFor (name) : 0
    }

    // ONYXTEON_NOFOG=1: the fog off, for a reference capture.
    property bool anyFog: ! ((typeof ovNoFog !== "undefined") && ovNoFog)

    // The seam frost's height ON SCREEN, in logical pixels: 68 at 1:1, and 68
    // at every other window size too.  See the OvGpuGlass below for why.
    readonly property real seamBandH: 68 / fitScale

    // menus are plain items parented here (see OvMenu): Qt Popup would ignore
    // the design scale and appear at the wrong size and position
    // Ground truth, straight from QML, for the --hud window: the size the
    // root item actually got, the scale that was computed, and every plot's
    // own geometry.  Reading these from C++ reports whatever findChild
    // returned; this reports what is drawn.
    readonly property string geomText: {
        let t = "root " + width.toFixed(0) + "x" + height.toFixed(0)
              + "  fitScale " + fitScale.toFixed(3)
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
        width: root.width / root.fitScale
        height: root.height / root.fitScale
        x: 0
        y: 0
        scale: root.fitScale
        // Top-left, not centre: the box covers the window exactly, and centring
        // an exact fit only invites a half-pixel seam between the two edges.
        transformOrigin: Item.TopLeft

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
                visible: root.soloed ("plots")
                opacity: root.soloAlpha ("plots")
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
                    // The CPU seam glass reads this half's pixels back; the
                    // GPU one samples the texture and needs no readback.
                    wantImage: root.cpuFrost
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
                    wantImage: root.cpuFrost
                }

                // ONE surface for both halves.  Two would be two textures and
                // two chains, and those do not present in the same frame while
                // a window is dragged: the upper half shows a new frame and the
                // lower one the previous one, and that mismatch IS the tearing
                // at the crossing.  One surface cannot disagree with itself.
                //
                // It spans the seam, half a reach each way; the gradient runs
                // outward from the middle in both directions, because the two
                // plot edges ARE the seam — the upper curve escapes down from
                // its bottom edge, the lower escapes up from its top edge.
                //
                // The position is snapped to whole SCREEN pixels: the design box
                // is scaled by a fractional fitScale, so an unsnapped edge lands
                // on a different sub-pixel offset at every window size and the
                // fog crawls against the plot while a window is dragged.
                OvEqFogGpu {
                    id: eqFogItem
                    objectName: "eqFog"
                    active: root.anyFog
                    visible: root.anyFog && root.soloed ("fog")
                    opacity: root.soloAlpha ("fog")
                    topView: scView
                    bottomView: pqView
                    fitScale: root.fitScale
                    range: root.fogRange
                    // Half the distance between the two plot edges.  Both
                    // halves use the same padding rule, so it is that padding.
                    edgeGap: Math.max (0, (scView.height + pqView.fogEdge
                                            - scView.fogEdge) / 2)
                    // A reach each way, less the gap the two edges already
                    // cover: from (edge - reach) on one side to
                    // (edge + reach) on the other.
                    width: parent.width
                    height: root.snapPx (root.fogRange * 2 - edgeGap * 2)
                    y: root.snapPx ((scView.fogEdge
                                     + scView.height + pqView.fogEdge) / 2
                                    - root.fogRange + edgeGap)
                }
            }

            // #hdr::before — the elliptical frost on the seam.
            // GPU version; OvSeamFrost (below) is the verified CPU fallback.
            OvGpuGlass {
                id: seamGlass
                dbgName: "seam"
                active: root.anyFrost && ! root.cpuFrost
                visible: root.anyFrost && ! root.cpuFrost && root.soloed ("seam")
                opacity: root.soloAlpha ("seam")
                width: 550
                height: 68
                x: (parent.width - width) / 2
                y: parent.height / 2 - height / 2
                source: plotLayer
                ellipse: true

                OvFrameProbe {
                    id: probeSeam
                    objectName: "probe-seam"
                    width: 0; height: 0
                }
            }

            OvSeamFrost {
                id: seamFrost
                visible: root.cpuFrost && root.soloed ("seam")
                opacity: root.soloAlpha ("seam")
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
                visible: root.soloed ("bars")
                opacity: root.soloAlpha ("bars")
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
                    cpuFrost: root.cpuFrost
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
                    cpuFrost: root.cpuFrost
                }
            }

            OvHeader {
                id: headerItem
                OvFrameProbe { id: probeHeader; objectName: "probe-header"
                               width: 0; height: 0 }
                // Hidden by opacity, not by visible: the header lays itself out
                // from its own height, and collapsing it would move the plots.
                // The cost of a transparent item is ~0; the cost of a moved
                // layout is a wrong measurement.
                opacity: root.soloAlpha ("header")
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
                id: deckItem
                OvFrameProbe { id: probeDeck; objectName: "probe-deck"
                               width: 0; height: 0 }
                visible: root.soloed ("deck")
                opacity: root.soloAlpha ("deck")
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

    // SM OOTHED in OvEngine, not mapped here.
    //
    // These used to be a binding on the raw dB values, which meant they were
    // re-evaluated only when a new measurement arrived: the accent glow jumped
    // from one brightness to the next and never appeared to move.  A light
    // needs ballistics on its own clock; OvEngine::lightTick provides them and
    // emits lightChanged at 60 Hz.
    readonly property real outLight: engine ? engine.outLight : 0
    readonly property real inLight:  engine ? engine.inLight  : 0

    function level (db) { return Math.max (0, Math.min (1, (db + 60) / 60)) }

    Keys.onEscapePressed: { OvMenu.close(); about.close() }
    focus: true
    // ---- ONYXTEON_SOLO read-out ------------------------------------------
    // Each row is a widget's OWN rate, taken from that widget: the plots count
    // inside paint(), the fog inside its own paint.  Widgets that are pure QML
    // (the seam glass, the header, the deck) have no paint callback to count in,
    // so they are shown as measured-by-window instead — labelled, because
    // presenting the window is not the same as redrawing that item.
    // The probes, by the name they are reported under.
    readonly property var probes: ({
        "seam":   probeSeam,
        "header": probeHeader,
        "deck":   probeDeck
    })

    readonly property var soloRows: {
        if (soloList === null) return []
        const rows = []
        const want = (n) => soloList.indexOf ("each") >= 0 || soloList.indexOf (n) >= 0
        // Only attach an alpha when it actually fades something: the read-out is
        // also the record of how a capture was taken, and a column that always
        // says 1.00 is noise.
        const alpha = (n) => { const a = soloOpacityFor (n); return a < 1 ? a : undefined }
        if (want ("plots")) {
            rows.push ({ name: "sc plot", fps: scView.paintFps, alpha: alpha ("plots") })
            rows.push ({ name: "pq plot", fps: pqView.paintFps, alpha: alpha ("plots") })
        }
        if (want ("fog"))
            rows.push ({ name: "fog", fps: eqFogItem ? eqFogItem.paintFps : null,
                         note: eqFogItem ? "" : "not created",
                         alpha: alpha ("fog") })
        // The QML-only widgets mark their own frames through an OvFrameProbe:
        // the scene graph calls into it for every frame that widget's subtree
        // is rendered, which is that widget's rate rather than the window's.
        for (const n of ["seam", "header", "deck", "bars"]) {
            if (!want (n)) continue
            const p = probes[n]
            rows.push ({ name: n, fps: p ? p.fps : null,
                         note: p ? "" : "no probe",
                         alpha: alpha (n) })
        }
        return rows
    }

    OvSoloHud {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 8
        rows: root.soloRows
    }

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
