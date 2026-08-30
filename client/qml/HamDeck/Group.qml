import QtQuick
import QtQuick.Layouts
import HamDeck

// A labelled section of the panel.
Rectangle {
    id: group
    property string title: ""
    default property alias content: inner.data

    color: Theme.panel
    radius: Theme.radius
    border.width: 1
    border.color: Theme.line
    implicitHeight: inner.implicitHeight + caption.height + 22

    SilkLabel {
        id: caption
        text: group.title
        x: 12
        y: 8
    }
    ColumnLayout {
        id: inner
        anchors { left: parent.left; right: parent.right; top: caption.bottom
                  leftMargin: 12; rightMargin: 12; topMargin: 8 }
        spacing: 8
    }
}
