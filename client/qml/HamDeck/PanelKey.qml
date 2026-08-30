import QtQuick
import HamDeck

// A panel key. Unlit it is a raised tile; lit it takes the cyan fill.
//
// ⚠️ `lit` is driven by RIG STATE, never by whether the key was clicked. A key
// that latches on click lies the moment a command fails, or another client -
// or the radio's own front panel - changes something.
Rectangle {
    id: key
    property alias text: label.text
    property bool lit: false
    property bool danger: false          // red means RF, never decoration
    property bool enabledKey: true
    signal clicked()

    implicitWidth: Math.max(58, label.implicitWidth + 22)
    implicitHeight: 38
    radius: Theme.radius
    color: !enabledKey ? Theme.panelDeep
         : danger && lit ? Theme.txRed
         : lit ? Theme.cyanFill
         : mouse.pressed ? Qt.darker(Theme.panel, 1.25)
         : mouse.containsMouse ? Qt.lighter(Theme.panel, 1.18)
         : Theme.panel
    border.width: 1
    border.color: !enabledKey ? Theme.line
                : danger && lit ? Theme.txRed
                : lit ? Theme.cyan
                : Theme.line
    Behavior on color { ColorAnimation { duration: 90 } }

    Text {
        id: label
        anchors.centerIn: parent
        font.family: Theme.display
        font.weight: Font.DemiBold
        font.pixelSize: 13
        font.letterSpacing: 0.8
        font.capitalization: Font.AllUppercase
        color: !key.enabledKey ? Qt.darker(Theme.dim, 1.4)
             : key.danger && key.lit ? Theme.text
             : key.lit ? Theme.cyan
             : Theme.text
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        enabled: key.enabledKey
        onClicked: key.clicked()
    }
}
