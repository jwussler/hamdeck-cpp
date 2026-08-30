pragma Singleton
import QtQuick

// HamDeck palette, type and metrics — the tokens from ~/hamdeck-site/brand/BRAND.md,
// verbatim. Mirrored from src/theme.h so the C++ and QML halves cannot drift.
//
// ⚠️ Do not add a colour here that is not in the brand table. If a new one is
// needed, the brand doc is the place to change, not this file.
QtObject {
    // ── Colour ──────────────────────────────────────────────────────────────
    readonly property color ground:    "#0E1013"   // panel face, background
    readonly property color panel:     "#171A1F"   // raised tiles and cards
    readonly property color panelDeep: "#141619"   // window background
    readonly property color line:      "#2A3038"   // hairlines, borders
    readonly property color text:      "#E8EAED"
    readonly property color dim:       "#8A929C"   // silkscreen labels, captions
    readonly property color amber:     "#FFB020"   // the VFO digits. The accent.
    readonly property color amberDim:  "#8A6320"   // inactive VFO-B readout
    readonly property color cyan:      "#3B82F6"   // lit keys, active state
    readonly property color cyanFill:  "#1E3A6B"   // fill behind a lit key
    readonly property color txRed:     "#B4232A"   // anything that transmits
    readonly property color okGreen:   "#32C765"   // healthy state, S-meter

    // Type. Bundled, not assumed present — see fonts/README.md.
    readonly property string display: "Barlow Condensed"   // labels, uppercase
    readonly property string body:    "IBM Plex Sans"
    readonly property string mono:    "IBM Plex Mono"      // anything numeric

    // ── Metrics ─────────────────────────────────────────────────────────────
    //
    // ⚠️ RESOLUTION AWARENESS IS TWO SEPARATE PROBLEMS AND THEY NEED TWO
    // SEPARATE MECHANISMS. Conflating them is what produces a panel that is
    // either unreadable on a 4K monitor or clipped on a laptop:
    //
    //   1. DENSITY — how big a thing should be drawn. That is `scale`, below,
    //      set once from Backend::uiScale. Every size in the UI goes through
    //      u() or f() so there is exactly one number to change.
    //   2. REFLOW — how much fits on a row. That is cols(), evaluated against
    //      the width actually available at the point of use. A key row that is
    //      six-across on a wide window must WRAP on a narrow one, not shrink
    //      until the legends are unreadable.
    //
    // Nothing here reads devicePixelRatio. Qt has already divided it out: the
    // sizes below are device-independent pixels, and multiplying by DPR again
    // is the double-scaling bug that makes a HiDPI panel twice the size it
    // should be.
    property real scale: 1.0

    // A size in device-independent pixels, scaled. Rounded, because half a
    // pixel on a border is a grey smear rather than a hairline.
    function u(px) { return Math.round(px * scale) }

    // ⚠️ Type has a FLOOR that sizes do not. At 0.8 scale an 11 px silkscreen
    // label would land at 8.8 px, which on a real panel at arm's length is not
    // readable — and an unreadable label is worse than a cramped layout,
    // because the operator cannot tell what the key does.
    function f(px) { return Math.max(9, Math.round(px * scale)) }

    // How many of an item `minW` wide fit across `w`, capped at `n`.
    // Used to reflow key rows rather than let them squeeze.
    function cols(w, minW, n) {
        if (w <= 0) return n
        const fit = Math.floor((w + gap) / (minW + gap))
        return Math.max(1, Math.min(n, fit))
    }

    readonly property int radius:  u(6)
    readonly property int gap:     u(8)     // between keys in a row
    readonly property int pad:     u(12)    // panel and group margins
    readonly property int keyH:    u(38)    // a panel key
    readonly property int minKeyW: u(58)    // ⚠️ also the touch target floor
    readonly property int rowH:    u(26)    // status bar
}
