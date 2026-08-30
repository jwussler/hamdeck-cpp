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
    property bool editing: false
    property string entryError: ""
    // Handed the typed text; returns "" if it was accepted, or the refusal.
    property var onCommit: null
    property var seedText: null
    spacing: Theme.u(6)

    function startEdit() {
        if (!seedText) return
        editor.text = seedText()
        entryError = ""
        editing = true
        editor.selectAll()
        editor.forceActiveFocus()
    }
    function cancelEdit() {
        editing = false
        entryError = ""
    }
    function commit() {
        const why = onCommit ? onCommit(editor.text) : ""
        editing = false
        // ⚠️ The refusal is SHOWN, not swallowed. A frequency that silently did
        // not go is worse than an error: the operator carries on believing the
        // rig moved.
        entryError = why ? why : ""
    }

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

        // ⚠️ CLICK THE FREQUENCY TO TYPE ONE. The reference panel does this and
        // this one could not: the on-screen keypad was the only way in, which is
        // twelve clicks to change band-edge to band-edge. Enter commits, Escape
        // cancels, and clicking away cancels - a half-typed frequency left in a
        // box is a frequency nobody meant to send.
        MouseArea {
            anchors.fill: parent
            visible: !readout.editing
            enabled: !readout.editing
            cursorShape: Qt.IBeamCursor
            onClicked: readout.startEdit()
        }

        Text {
            anchors.centerIn: parent
            visible: !readout.editing
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

        TextInput {
            id: editor
            anchors.centerIn: parent
            width: parent.width - Theme.u(48)
            visible: readout.editing
            horizontalAlignment: TextInput.AlignHCenter
            font.family: Theme.mono
            font.pixelSize: readout.digitPx
            font.weight: Font.Medium
            color: Theme.amber
            selectionColor: Theme.cyanFill
            selectedTextColor: Theme.text
            // 14.200, 14200, 14.200.000 and 14,200,000 all mean the same thing.
            // Validation is the parser's job, not a regex's - see freq_input.h.
            onAccepted: readout.commit()
            Keys.onEscapePressed: readout.cancelEdit()
            onActiveFocusChanged: if (!activeFocus && readout.editing) readout.cancelEdit()
        }
    }

    // The refusal, under the readout where the eye already is.
    Text {
        Layout.fillWidth: true
        visible: readout.entryError !== ""
        text: readout.entryError
        horizontalAlignment: Text.AlignHCenter
        font.family: Theme.body
        font.pixelSize: Theme.f(12)
        color: Theme.txRed
        wrapMode: Text.WordWrap
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
