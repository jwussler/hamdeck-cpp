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

    spacing: Theme.u(2)

    RowLayout {
        Layout.fillWidth: true
        SilkLabel { text: knob.label }
        Item { Layout.fillWidth: true }
        Text {
            text: (slider.pressed ? Math.round(slider.value) : knob.value) + knob.suffix
            font.family: Theme.mono
            font.pixelSize: Theme.f(12)
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
        //
        // ⚠️ NOT `value: pressed ? value : knob.value`. That referred to the
        // Slider's OWN value, so the moment `pressed` went true QML saw a
        // binding loop, broke the binding, and the control FROZE. Every slider
        // in the panel - volume, mic gain, RF gain, RF power, CW speed - was
        // undraggable, and the operator could not turn down a microphone that
        // was pinning ALC at 100%.
        //
        // ⚠️ The startup checks could not see it: the loop only forms on the
        // first press, so qml_selftest and --check-resolutions both passed. A
        // control that is never TOUCHED by a test is a control nobody has tested,
        // which is the same lesson the global PTT hotkey taught.
        //
        // A Binding with `when` is the right shape: it follows knob.value while
        // the operator is not holding the control, and stops asserting itself the
        // moment they are - without ever referring to the property it drives.
        Binding on value {
            when: !slider.pressed
            value: knob.value
        }
        onMoved: knob.moved(Math.round(value))

        background: Rectangle {
            x: slider.leftPadding
            y: slider.topPadding + slider.availableHeight / 2 - height / 2
            width: slider.availableWidth
            height: Theme.u(5)
            radius: Theme.u(3)
            color: Theme.ground
            border.width: 1
            border.color: Theme.line
            Rectangle {
                width: slider.visualPosition * parent.width
                height: parent.height
                color: Theme.cyan
                radius: Theme.u(3)
            }
        }
        handle: Rectangle {
            x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
            y: slider.topPadding + slider.availableHeight / 2 - height / 2
            // ⚠️ The grab handle scales with everything else. A 14 px handle
            // beside a 1.5x slider is a target the operator misses, and this is
            // the control that sets transmit power.
            width: Theme.u(14)
            height: Theme.u(14)
            radius: Theme.u(7)
            color: slider.pressed ? Theme.cyan : Theme.text
            border.width: 1
            border.color: Theme.line
        }
    }
}
