import QtQuick
import QtQuick.Controls
import HamDeck

// A dropdown drawn as panel hardware.
//
// ⚠️ Qt Quick Controls' Basic style draws a LIGHT control: white field, white
// popup, black text. On this panel that is three white boxes on near-black,
// and BRAND.md governs anything with a user interface. Styling it here means
// the three places that use one cannot drift apart.
//
// Scaled like everything else - see Theme.qml on why density and reflow are
// separate mechanisms.
ComboBox {
    id: combo
    implicitHeight: Theme.keyH
    font.family: Theme.body
    font.pixelSize: Theme.f(12)

    background: Rectangle {
        color: combo.pressed ? Qt.darker(Theme.panel, 1.25) : Theme.ground
        radius: Theme.radius
        border.width: 1
        border.color: combo.activeFocus || combo.hovered ? Theme.cyan : Theme.line
        Behavior on border.color { ColorAnimation { duration: 100 } }
    }

    contentItem: Text {
        leftPadding: Theme.u(10)
        rightPadding: combo.indicator.width + Theme.u(6)
        text: combo.displayText
        font: combo.font
        color: Theme.text
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight            // a device name is longer than the box
    }

    indicator: Canvas {
        x: combo.width - width - Theme.u(10)
        y: combo.topPadding + (combo.availableHeight - height) / 2
        width: Theme.u(9); height: Theme.u(5)
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.moveTo(0, 0); ctx.lineTo(width, 0); ctx.lineTo(width / 2, height)
            ctx.closePath()
            ctx.fillStyle = combo.pressed ? Theme.cyan : Theme.dim
            ctx.fill()
        }
        Connections { target: combo; function onPressedChanged() { parent.requestPaint() } }
    }

    delegate: ItemDelegate {
        width: combo.width
        height: Theme.keyH
        highlighted: combo.highlightedIndex === index
        contentItem: Text {
            text: combo.textRole ? modelData[combo.textRole] : modelData
            font: combo.font
            color: highlighted ? Theme.cyan : Theme.text
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            color: highlighted ? Theme.cyanFill : Theme.panel
        }
    }

    popup: Popup {
        y: combo.height
        width: combo.width
        // ⚠️ Never taller than the screen. A machine with a dozen audio devices
        // would otherwise open a list that runs off the display, and the item
        // the operator wants is the one they cannot reach.
        implicitHeight: Math.min(contentItem.implicitHeight + Theme.u(2),
                                 Screen.desktopAvailableHeight * 0.6)
        padding: 1
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: combo.popup.visible ? combo.delegateModel : null
            currentIndex: combo.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator {}
        }
        background: Rectangle {
            color: Theme.panel
            radius: Theme.radius
            border.width: 1
            border.color: Theme.line
        }
    }
}
