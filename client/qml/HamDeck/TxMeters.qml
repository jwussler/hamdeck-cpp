import QtQuick
import QtQuick.Layouts
import HamDeck

// What the station is doing while it transmits: drive in, ALC, power out, SWR.
//
// ⚠️ THIS REPLACES THE S-METER WHILE KEYED, on purpose. Receive signal strength
// is meaningless mid-over - the receiver is muted - and the pinned head is the
// only thing an operator can see without leaving the tab they are working in.
//
// ⚠️ DRIVE IS MEASURED AT THE HOST, NOT HERE. This end knows what it sent; the
// host knows what arrived at the radio, and only the second one can tell a
// working microphone from a muted one. Frames accepted, a queue behaving and a
// steady sample rate all read identically for digital silence (WIP.md 8f), which
// is exactly the night this meter exists because of.
Item {
    id: root
    implicitHeight: col.implicitHeight + Theme.u(18)

    Rectangle {
        anchors.fill: parent
        color: Theme.ground
        radius: Theme.radius
        border.width: 1
        border.color: backend.tx ? Theme.txRed : Theme.line
    }

    ColumnLayout {
        id: col
        anchors { left: parent.left; right: parent.right; top: parent.top
                  leftMargin: Theme.pad; rightMargin: Theme.pad; topMargin: Theme.u(8) }
        spacing: Theme.u(4)

        MeterBar {
            Layout.fillWidth: true
            label: "Drive in"
            value: backend.txDrivePct / 100
            readout: backend.txDrivePct + "%"
            // ⚠️ Silence while keyed is the failure this catches, so it is the
            // thing marked - not a level that is merely low.
            warn: backend.tx && backend.txDrivePct === 0
        }

        // ⚠️ THE ONE AN OPERATOR SETS GAIN AGAINST, and the band behind it is
        // NOT this station's measurement. The host reports alc_pct from hamlib's
        // yaesu_default_alc_cal and says so at /api/meters/scale; 50-75% is
        // where the rig wants to sit, not a calibration of this radio. The TX
        // tab prints that provenance in full - a number an operator trusts to
        // set drive has to say where it came from.
        MeterBar {
            Layout.fillWidth: true
            label: "ALC"
            value: backend.alcPct / 100
            readout: backend.alcPct + "%"
            targetFrom: 0.50
            targetTo: 0.75
            warn: backend.alcPct > 90
        }

        MeterBar {
            Layout.fillWidth: true
            label: "Power out"
            value: backend.powerPct / 100
            readout: backend.powerPct + "%"
        }

        MeterBar {
            Layout.fillWidth: true
            label: "SWR"
            value: (parseFloat(backend.swr) - 1.0) / 2.0
            readout: backend.swr
            warn: parseFloat(backend.swr) >= 2.0
        }
    }
}
