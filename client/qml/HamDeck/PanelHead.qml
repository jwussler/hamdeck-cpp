import QtQuick
import QtQuick.Layouts
import HamDeck

// The frequency readout and the S-meter, as one block.
//
// ⚠️ ONE DEFINITION, TWO PLACEMENTS. On a desktop window this scrolls at the top
// of the panel column; on a phone it is PINNED above the tabs, because the two
// things an operator looks at constantly must never be the thing they scrolled
// away from. Declaring it twice in Main.qml would work today and drift the first
// time a binding changes on one copy and not the other.
ColumnLayout {
    // ⚠️ Passed in rather than reached out for: PanelHead is used in two places
    // and must not know which window it is inside.
    property bool keypadOpenRef: false
    signal toggleKeypad()

    spacing: Theme.u(10)

    // ⚠️ The keypad opens from the readout it types into. It was a 14-key group
    // sitting in the middle of the operating surface for something used a few
    // times a session.
    Item {
        Layout.fillWidth: true
        implicitHeight: rd.implicitHeight
        Readout {
            id: rd
            anchors.fill: parent
            anchors.leftMargin: Theme.pad; anchors.rightMargin: Theme.pad
        // The wheel tunes, in the step the tuning-step row is set to.
        stepHz: backend.stepHz
        onStepped: (up) => backend.send("/api/step/" + backend.stepHz +
                                        (up ? "/up" : "/down"))
        freq: backend.freqText
        mode: backend.mode
        vfo: backend.vfo
        watts: backend.power
        stale: backend.stale
        band: backend.bandName
        freqB: backend.freqB
        // Typing a frequency: the parser and the range check live in C++ so they
        // are shared and tested (ctest -R freq).
            seedText: () => backend.freqEditText()
            onCommit: (text) => backend.setFreqText(text)
        }
    }

    // ⚠️ BESIDE THE READOUT, NOT ON IT. Anchored inside, it landed on top of the
    // POWER reading at phone widths - the value was still there, drawn under a
    // key. A control that hides a reading is worse than one that costs a row.
    PanelKey {
        Layout.alignment: Qt.AlignRight
        Layout.rightMargin: Theme.pad
        Layout.preferredWidth: Theme.u(88)
        Layout.preferredHeight: Theme.u(28)
        text: "KEYPAD"
        lit: keypadOpenRef
        onClicked: toggleKeypad()
    }

    // ⚠️ THE HEAD SWAPS WHILE KEYED. The receiver is muted mid-over, so the
    // S-meter has nothing to say; drive, ALC and power are what the operator
    // needs in front of them, and on a phone this block is the only thing always
    // on screen.
    TxMeters {
        Layout.fillWidth: true
        Layout.leftMargin: Theme.pad; Layout.rightMargin: Theme.pad
        visible: backend.tx
    }

    // ⚠️ THE RECEIVE LEVEL, BESIDE THE SIGNAL METER AND NOT INSIDE IT. The
    // S-meter is what the RADIO hears; this is what arrived at this client
    // through the host and the socket, and the whole point is that they can
    // disagree - a strong signal on a meter with no audio behind it is exactly
    // the fault that had us guessing.
    MeterBar {
        Layout.fillWidth: true
        Layout.leftMargin: Theme.pad; Layout.rightMargin: Theme.pad
        visible: !backend.tx && backend.sessionActive
        label: backend.rxSilent ? "Receive · NO AUDIO FOR 20 s" : "Receive"
        value: backend.rxLevelPct / 100
        readout: backend.rxLevelPct + "%"
        warn: backend.rxSilent
    }

    SMeter {
        visible: !backend.tx
        Layout.fillWidth: true
        Layout.leftMargin: Theme.pad; Layout.rightMargin: Theme.pad
        raw: backend.sMeterRaw
        unit: backend.sUnit
        ticks: backend.meterTicks
        transmitting: backend.tx
    }
}
