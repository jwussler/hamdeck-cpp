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
        // ⚠️ THE SLIDER MUST HAVE A HEIGHT OR IT CANNOT BE TOUCHED.
        //
        // Neither delegate below declared an implicit size, so the control's
        // implicitHeight was 0; in a ColumnLayout with only Layout.fillWidth it
        // got height 0 and swallowed no mouse events ANYWHERE. The background
        // draws at an explicit height, so a full-looking slider rendered that
        // could not be grabbed - volume, mic gain, RF gain, RF power, CW speed,
        // all dead, including the one that turns down a microphone pinning ALC
        // at 100%.
        //
        // ⚠️ EVERY EXISTING CHECK PASSED. qml_selftest loads the QML and
        // --check-resolutions measures geometry; neither ever PRESSES anything,
        // and a zero-height item still lays out and paints. tests_knob.cpp drags
        // it with real mouse events, which is the only thing that would have
        // caught this - the same lesson the global PTT hotkey taught.
        implicitHeight: Theme.u(16)
        implicitWidth: Theme.u(120)

        // Follow the rig unless the operator has hold of it.
        //
        // Separately wrong before, though NOT the reason the sliders were dead:
        // `value: pressed ? value : knob.value` referred to the Slider's own
        // value, which is a binding loop the first time pressed goes true. A
        // Binding with `when` expresses the intent without referring to the
        // property it drives.
        Binding on value {
            when: !slider.pressed
            value: knob.value
        }
        onMoved: knob.moved(Math.round(value))

        background: Rectangle {
            implicitWidth: Theme.u(120)
            implicitHeight: Theme.u(14)
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
            implicitWidth: Theme.u(14)
            implicitHeight: Theme.u(14)
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
