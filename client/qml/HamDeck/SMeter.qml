import QtQuick
import HamDeck

// The signal meter.
//
// ⚠️ THE SCALE COMES FROM THE HOST. `ticks` is fetched from /api/meters/scale,
// so the face follows the radio rather than every client carrying its own
// calibration table. When the host supplies none, the meter draws UNLABELLED
// ticks and says "uncalibrated" — painting S1..S9 on a scale we were not given
// would look authoritative and be a fabrication, and a signal report is a thing
// operators pass on to other people.
Item {
    id: meter
    property int raw: 0
    property string unit: ""
    property var ticks: []
    property bool transmitting: false

    // Scales with the panel: the meter face is read at the same glance as the
    // frequency above it, and a fixed 72 px next to a scaled readout looks
    // like a bug rather than a decision.
    implicitHeight: Theme.u(72)

    Rectangle {
        anchors.fill: parent
        color: Theme.ground
        radius: Theme.radius
        border.width: 1
        border.color: Theme.line
    }

    Canvas {
        id: face
        anchors { fill: parent; margins: 1 }
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const pad = Theme.u(10)
            const trackY = Theme.u(20)
            const trackH = height - Theme.u(46)
            const trackW = width - pad * 2

            ctx.fillStyle = Theme.ground
            ctx.fillRect(pad, trackY, trackW, trackH)

            const frac = Math.max(0, Math.min(1, meter.raw / 255))
            if (frac > 0) {
                const g = ctx.createLinearGradient(pad, 0, pad + trackW, 0)
                // ⚠️ Brand tokens only. An earlier version invented a lighter
                // red and a mid-green for the gradient stops; BRAND.md says not
                // to invent colours, and it has tokens for both ends.
                if (meter.transmitting) {
                    g.addColorStop(0.0, Theme.txRed)
                    g.addColorStop(1.0, Theme.txRed)
                } else {
                    g.addColorStop(0.0, Theme.okGreen)
                    g.addColorStop(1.0, Theme.amber)
                }
                ctx.fillStyle = g
                ctx.fillRect(pad, trackY, trackW * frac, trackH)
            }

            ctx.font = Theme.f(9) + "px '" + Theme.mono + "'"
            ctx.textAlign = "center"
            if (meter.ticks.length === 0) {
                // No scale from the host: evenly spaced, unlabelled.
                ctx.strokeStyle = Theme.line
                for (let i = 0; i <= 10; ++i) {
                    const x = pad + trackW * i / 10
                    ctx.beginPath()
                    ctx.moveTo(x, trackY + trackH + Theme.u(2))
                    ctx.lineTo(x, trackY + trackH + (i % 5 === 0 ? Theme.u(7) : Theme.u(4)))
                    ctx.stroke()
                }
            } else {
                for (const t of meter.ticks) {
                    const overS9 = String(t.label).charAt(0) === "+"
                    const x = pad + trackW * (t.raw / 255)
                    ctx.strokeStyle = overS9 ? Theme.txRed : Theme.line
                    ctx.beginPath()
                    ctx.moveTo(x, trackY + trackH + Theme.u(2))
                    ctx.lineTo(x, trackY + trackH + Theme.u(7))
                    ctx.stroke()
                    ctx.fillStyle = overS9 ? Theme.txRed : Theme.dim
                    // Keep the last label inside the face; the one at the far
                    // right is the one an operator most wants to read.
                    const lx = Math.max(pad + Theme.u(8), Math.min(width - pad - Theme.u(8), x))
                    ctx.fillText(String(t.label), lx, trackY + trackH + Theme.u(18))
                }
            }
        }
        // ⚠️ A Canvas does not repaint because a size changed underneath it.
        // Without this the meter face keeps the geometry it was painted at and
        // the ticks sit in the wrong place after a scale or a resize - which
        // looks like a calibration bug and is not one.
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        Connections {
            target: Theme
            function onScaleChanged() { face.requestPaint() }
        }
        Connections {
            target: meter
            function onRawChanged() { face.requestPaint() }
            function onTicksChanged() { face.requestPaint() }
            function onTransmittingChanged() { face.requestPaint() }
        }
    }

    SilkLabel { text: "Signal"; x: Theme.pad
                anchors.bottom: parent.bottom; anchors.bottomMargin: Theme.u(3) }

    // ⚠️ The reading sits ABOVE the bar. Along the bottom it collided with the
    // +60 tick label - which is the one an operator most wants to read.
    Text {
        anchors { right: parent.right; rightMargin: Theme.pad
                  top: parent.top; topMargin: Theme.u(3) }
        text: meter.unit !== "" ? meter.unit : ("raw " + meter.raw + " / 255 · uncalibrated")
        font.family: meter.unit !== "" ? Theme.display : Theme.mono
        font.pixelSize: meter.unit !== "" ? Theme.f(15) : Theme.f(9)
        font.weight: Font.Bold
        font.letterSpacing: meter.unit !== "" ? 1 : 0
        color: meter.transmitting ? Theme.txRed : (meter.unit !== "" ? Theme.amber : Theme.dim)
    }
}
