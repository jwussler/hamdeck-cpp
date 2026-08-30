import QtQuick
import QtQuick.Layouts
import HamDeck

// The frequency readout — the one thing read at a glance from across the room.
// Amber on near-black, the way the gear beside it does it.
ColumnLayout {
    id: readout
    property string freq: "—.———.———"
    property string mode: "—"
    property string vfo: "—"
    property int watts: 0
    property bool stale: false
    spacing: 6

    Rectangle {
        Layout.fillWidth: true
        implicitHeight: 96
        color: Theme.ground
        radius: 8
        border.width: 1
        border.color: Theme.line

        Text {
            anchors.centerIn: parent
            text: readout.freq
            // ⚠️ tabular-nums: without it the digits change width as they change
            // and the whole readout jitters, which looks broken.
            font.family: Theme.mono
            font.pixelSize: 54
            font.weight: Font.Medium
            font.letterSpacing: 1
            renderType: Text.NativeRendering
            // Grey when the host says its cache is stale, so an old frequency
            // never sits there looking live.
            color: readout.stale ? Theme.dim : Theme.amber
            Behavior on color { ColorAnimation { duration: 150 } }
        }
    }

    // ⚠️ Positioned by explicit fraction of width, not by a Layout.
    // A Repeater delegate collapsed to zero width and packed left; three
    // explicit ColumnLayouts with fillWidth did the same. Rather than keep
    // guessing at the layout engine, each column is simply one third of the
    // width. A three-item readout does not need a layout negotiation.
    Item {
        Layout.fillWidth: true
        implicitHeight: 42

        Repeater {
            model: [
                { k: "Mode",  v: readout.mode },
                { k: "VFO",   v: readout.vfo },
                { k: "Power", v: readout.watts + " W" }
            ]
            delegate: Item {
                width: readout.width / 3
                height: 42
                x: index * (readout.width / 3)
                SilkLabel {
                    text: modelData.k
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: 2
                }
                Text {
                    text: modelData.v
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: 18
                    font.family: Theme.mono
                    font.pixelSize: 17
                    font.weight: Font.Medium
                    color: Theme.text
                }
            }
        }
    }
}
