import QtQuick
import HamDeck

// A small horizontal bar for PO and SWR.
//
// ⚠️ SWR turns red at 2:1, where an operator wants to stop and look at the
// antenna rather than keep transmitting into it.
Item {
    id: bar
    property string label: ""
    property real value: 0            // 0..1
    property string readout: ""
    property bool warn: false
    implicitHeight: 28

    SilkLabel { id: cap; text: bar.label; anchors.left: parent.left; y: 0 }
    Text {
        anchors.right: parent.right
        y: 0
        text: bar.readout
        font.family: Theme.mono; font.pixelSize: 11; font.weight: Font.Medium
        color: bar.warn ? Theme.txRed : Theme.text
    }
    Rectangle {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 8
        radius: 3
        color: Theme.ground
        border.width: 1
        border.color: Theme.line
        Rectangle {
            width: Math.max(0, Math.min(1, bar.value)) * (parent.width - 2)
            height: parent.height - 2
            x: 1; y: 1
            radius: 2
            color: bar.warn ? Theme.txRed : Theme.okGreen
        }
    }
}
