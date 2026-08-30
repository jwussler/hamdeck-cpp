import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import HamDeck

// A labelled control: silkscreen caption, value readout, slider.
//
// ⚠️ `value` follows the RIG. Dragging emits `moved`; it does not set `value`
// directly. Otherwise the control latches to where the operator dropped it and
// disagrees with the radio the moment a command fails or another client moves
// it - the same rule the lit keys follow.
ColumnLayout {
    id: knob
    property string label: ""
    property string suffix: ""
    property int value: 0
    property int from: 0
    property int to: 100
    property bool live: true          // false while the operator is dragging
    signal moved(int v)

    spacing: 2

    RowLayout {
        Layout.fillWidth: true
        SilkLabel { text: knob.label }
        Item { Layout.fillWidth: true }
        Text {
            text: (slider.pressed ? Math.round(slider.value) : knob.value) + knob.suffix
            font.family: Theme.mono
            font.pixelSize: 12
            font.weight: Font.Medium
            color: slider.pressed ? Theme.cyan : Theme.text
        }
    }

    Slider {
        id: slider
        Layout.fillWidth: true
        from: knob.from
        to: knob.to
        stepSize: 1
        // Follow the rig unless the operator has hold of it.
        value: pressed ? value : knob.value
        onMoved: knob.moved(Math.round(value))

        background: Rectangle {
            x: slider.leftPadding
            y: slider.topPadding + slider.availableHeight / 2 - height / 2
            width: slider.availableWidth
            height: 5
            radius: 3
            color: Theme.ground
            border.width: 1
            border.color: Theme.line
            Rectangle {
                width: slider.visualPosition * parent.width
                height: parent.height
                color: Theme.cyan
                radius: 3
            }
        }
        handle: Rectangle {
            x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
            y: slider.topPadding + slider.availableHeight / 2 - height / 2
            width: 14
            height: 14
            radius: 7
            color: slider.pressed ? Theme.cyan : Theme.text
            border.width: 1
            border.color: Theme.line
        }
    }
}
