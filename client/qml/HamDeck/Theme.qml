pragma Singleton
import QtQuick

// HamDeck palette and type — the tokens from ~/hamdeck-site/brand/BRAND.md,
// verbatim. Mirrored from src/theme.h so the C++ and QML halves cannot drift.
//
// ⚠️ Do not add a colour here that is not in the brand table. If a new one is
// needed, the brand doc is the place to change, not this file.
QtObject {
    // Colour
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

    readonly property int radius: 6
}
