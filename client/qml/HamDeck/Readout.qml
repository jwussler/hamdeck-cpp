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
    property string band: "—"
    property double freqB: 0
    spacing: Theme.u(6)

    // ⚠️ THE FREQUENCY IS THE ONE THING READ FROM ACROSS THE ROOM, so it takes
    // its size from the WIDTH AVAILABLE as well as the panel scale - a fixed
    // 54 px is small on a 4K monitor and clips "14.200.000" on a narrow window.
    // Floored, because a frequency too small to read is the same as no readout,
    // and capped so it cannot dominate a wide panel.
    readonly property int digitPx:
        Math.max(Theme.f(24), Math.min(Theme.f(54), Math.round(readout.width * 0.115)))

    Rectangle {
        Layout.fillWidth: true
        implicitHeight: Math.round(readout.digitPx * 1.75)
        color: Theme.ground
        radius: Theme.u(8)
        border.width: 1
        border.color: Theme.line

        // Band name, top-left of the readout. ⚠️ Shows "—" out of band rather
        // than rounding to the nearest one: a wrong band label is worse than none.
        SilkLabel {
            text: readout.band
            anchors { left: parent.left; top: parent.top
                      leftMargin: Theme.pad; topMargin: Theme.u(8) }
            color: Theme.amberDim
            font.pixelSize: Theme.f(13)
        }

        // ⚠️ VFO-B in the DIM amber, so it is legible without competing with the
        // active VFO. The brand has a token for exactly this.
        Text {
            anchors { right: parent.right; top: parent.top
                      rightMargin: Theme.pad; topMargin: Theme.u(6) }
            visible: readout.freqB > 0
            text: "B  " + (readout.freqB / 1e6).toFixed(3)
            font.family: Theme.mono
            font.pixelSize: Theme.f(13)
            color: Theme.amberDim
        }

        Text {
            anchors.centerIn: parent
            text: readout.freq
            // ⚠️ tabular-nums: without it the digits change width as they change
            // and the whole readout jitters, which looks broken.
            font.family: Theme.mono
            font.pixelSize: readout.digitPx
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
        implicitHeight: Theme.u(42)

        Repeater {
            model: [
                { k: "Mode",  v: readout.mode },
                { k: "VFO",   v: readout.vfo },
                { k: "Power", v: readout.watts + " W" }
            ]
            delegate: Item {
                width: readout.width / 3
                height: Theme.u(42)
                x: index * (readout.width / 3)
                SilkLabel {
                    text: modelData.k
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: Theme.u(2)
                }
                Text {
                    text: modelData.v
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: Theme.u(18)
                    font.family: Theme.mono
                    font.pixelSize: Theme.f(17)
                    font.weight: Font.Medium
                    color: Theme.text
                }
            }
        }
    }
}
