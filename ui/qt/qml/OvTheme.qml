pragma Singleton
import QtQuick

// Colour + metric tokens, copied 1:1 from the :root block of web_fx/index.html.
QtObject {
    readonly property color bg:      "#16181d"
    readonly property color bg2:     "#1c1f26"
    readonly property color panel:   "#1f232b"
    readonly property color line:    "#2c313b"
    readonly property color ink:     "#e7ebf2"
    readonly property color dim:     "#8b93a3"
    readonly property color faint:   "#5b6473"

    readonly property color sc:      "#ff5c8a"   // sidechain accent
    readonly property color pq:      "#43d9c4"   // post-eq accent
    readonly property color amber:   "#f0a23b"
    readonly property color grid:    "#252a33"
    readonly property color gridM:   "#2e3440"
    readonly property color menuBg:  "#0d0f13"
    readonly property color dotOff:  "#39404b"

    // design space: #wrap is 1040 x 772 in the CSS
    readonly property real designW: 1040
    readonly property real designH: 772
    readonly property real deckH:   110
    readonly property real headerH: 56
    readonly property real barH:    32
    readonly property real pitW:    640

    readonly property string displayFont: "EDIX"
    readonly property string uiFont:      "Inter"

    // QML's font.pixelSize is an int, so a 10.5 px label would round to 10 and
    // come out ~8 % narrow.  pointSize is a real, so it is used instead — but
    // pointSize is NOT device independent: a font at pointSize p renders at
    //     p * logicalDpi / 72   logical pixels
    // while every rect in this design is in logical pixels.  At the usual
    // 96 dpi that is the p * 4/3 relation the CSS sizes were chosen for; on a
    // scaled display (logicalDpi = 96 * scale) the same pointSize would render
    // scale times too large and the text would overflow its layout.
    //
    // `ovUiScale` is set from C++ as logicalDpi / 96, so the conversion holds
    // on any display.  Without it (e.g. a bare QML viewer) it defaults to 1.
    readonly property real uiScale: (typeof ovUiScale !== "undefined" && ovUiScale > 0)
                                    ? ovUiScale : 1.0

    // Qt keeps logicalDotsPerInch at 96 when the desktop scales via
    // QT_SCALE_FACTOR / devicePixelRatio — it scales the whole item tree,
    // fonts included — so pointSize needs no correction there (measured: a
    // header label is 61 logical px at scale 1 and 62 at scale 2 with or
    // without this division).  Only a genuinely dense X server that reports,
    // say, 192 dpi with no Qt scaling needs it, and there pointSize would
    // otherwise render twice as tall as the rects around it.
    //
    // The deadband keeps ordinary servers (100, 109 dpi …) on the exact
    // CSS-derived sizes instead of drifting a few percent.
    readonly property bool  dpiCorrection: uiScale > 1.05 || uiScale < 0.95

    function pt (cssPx) { return cssPx * 0.75 / (dpiCorrection ? uiScale : 1.0) }

    function alpha (c, a) { return Qt.rgba (c.r, c.g, c.b, a) }
}
