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

    // ⚠️ AN OPTIONAL TARGET BAND, DRAWN INSIDE THE TRACK. The first attempt put
    // it behind the bar, where the track's own opaque ground painted straight
    // over it - a marker that renders perfectly and is invisible. 0..1, and
    // equal values mean no band.
    property real targetFrom: 0
    property real targetTo: 0

    implicitHeight: Theme.u(28)

    SilkLabel { id: cap; text: bar.label; anchors.left: parent.left; y: 0 }
    Text {
        anchors.right: parent.right
        y: 0
        text: bar.readout
        font.family: Theme.mono; font.pixelSize: Theme.f(11); font.weight: Font.Medium
        color: bar.warn ? Theme.txRed : Theme.text
    }
    Rectangle {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: Theme.u(8)
        radius: Theme.u(3)
        color: Theme.ground
        border.width: 1
        border.color: Theme.line
        // The band an operator is aiming for, under the reading itself.
        Rectangle {
            visible: bar.targetTo > bar.targetFrom
            x: 1 + bar.targetFrom * (parent.width - 2)
            width: (bar.targetTo - bar.targetFrom) * (parent.width - 2)
            y: 1
            height: parent.height - 2
            color: Theme.cyanFill
        }
        Rectangle {
            width: Math.max(0, Math.min(1, bar.value)) * (parent.width - 2)
            height: parent.height - 2
            x: 1; y: 1
            radius: Theme.u(2)
            color: bar.warn ? Theme.txRed : Theme.okGreen
        }
    }
}
