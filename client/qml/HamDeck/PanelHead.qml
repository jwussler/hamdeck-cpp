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
    spacing: Theme.u(10)

    Readout {
        Layout.fillWidth: true
        Layout.leftMargin: Theme.pad; Layout.rightMargin: Theme.pad
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

    // ⚠️ THE HEAD SWAPS WHILE KEYED. The receiver is muted mid-over, so the
    // S-meter has nothing to say; drive, ALC and power are what the operator
    // needs in front of them, and on a phone this block is the only thing always
    // on screen.
    TxMeters {
        Layout.fillWidth: true
        Layout.leftMargin: Theme.pad; Layout.rightMargin: Theme.pad
        visible: backend.tx
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
