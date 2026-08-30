import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import HamDeck

// A labelled text field, styled as panel hardware rather than a form control.
ColumnLayout {
    id: field
    property string label: ""
    property string placeholder: ""
    property alias text: input.text
    property bool password: false
    signal accepted()

    spacing: Theme.u(3)

    SilkLabel { text: field.label }

    Rectangle {
        Layout.fillWidth: true
        implicitHeight: Theme.keyH
        color: Theme.ground
        radius: Theme.radius
        border.width: 1
        border.color: input.activeFocus ? Theme.cyan : Theme.line
        Behavior on border.color { ColorAnimation { duration: 100 } }

        TextInput {
            id: input
            anchors { fill: parent; leftMargin: Theme.u(10); rightMargin: Theme.u(10) }
            verticalAlignment: TextInput.AlignVCenter
            font.family: Theme.mono
            font.pixelSize: Theme.f(14)
            color: Theme.text
            selectionColor: Theme.cyanFill
            selectedTextColor: Theme.text
            clip: true
            echoMode: field.password ? TextInput.Password : TextInput.Normal
            onAccepted: field.accepted()

            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: input.text === "" && !input.activeFocus
                text: field.placeholder
                font.family: Theme.mono
                font.pixelSize: Theme.f(14)
                color: Theme.dim
            }
        }
    }

    function focusField() { input.forceActiveFocus() }
}
