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
    readonly property real deckH:   101
    readonly property real headerH: 56
    readonly property real barH:    32
    readonly property real pitW:    640

    readonly property string displayFont: "EDIX"
    readonly property string uiFont:      "Inter"

    function alpha (c, a) { return Qt.rgba (c.r, c.g, c.b, a) }
}
