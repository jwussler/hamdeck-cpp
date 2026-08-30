import QtQuick
import HamDeck

// A silkscreen label — condensed uppercase, letter-spaced, dim.
// BRAND.md: "Condensed uppercase is the silkscreen on radio gear. Use it for
// labels; never for paragraphs."
Text {
    font.family: Theme.display
    font.weight: Font.DemiBold
    font.pixelSize: 11
    font.letterSpacing: 1.2
    font.capitalization: Font.AllUppercase
    color: Theme.dim
}
