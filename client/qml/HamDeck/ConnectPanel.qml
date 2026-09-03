import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import HamDeck

// The connect screen.
//
// ⚠️ THERE IS NO DEFAULT HOST, AND THERE MUST NOT BE. A hostname compiled into a
// public repo points every install at that station (docs/internal/CARRYOVER.md section 6). The
// field starts empty on a fresh install and is remembered afterwards.
//
// ⚠️ THE PASSWORD IS NEVER REMEMBERED. Host and username are; the password costs
// one login to re-enter and costs a credential on disk to keep.
Item {
    id: root
    signal connectRequested(string host, int port, string user, string password)

    Rectangle {
        anchors.fill: parent
        color: Theme.panelDeep
    }

    ColumnLayout {
        anchors.centerIn: parent
        // ⚠️ Fits the window it is given: on a 1024x600 netbook a fixed 420 px
        // form with fixed spacing runs off the bottom, and the Connect button -
        // the only control on the screen - is the part that goes.
        width: Math.min(Theme.u(420), root.width - Theme.u(48))
        spacing: Math.min(Theme.u(14), Math.max(Theme.u(6), root.height / 40))

        // Wordmark. BRAND.md: HAM in text, DECK in the accent — and inside the
        // app the accent is CYAN, not amber, matching the surrounding UI.
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 0
            Text {
                text: "HAM"
                font.family: Theme.display; font.weight: Font.Bold
                font.pixelSize: Theme.f(34); font.letterSpacing: 1.4
                color: Theme.text
            }
            Text {
                text: "DECK"
                font.family: Theme.display; font.weight: Font.Bold
                font.pixelSize: Theme.f(34); font.letterSpacing: 1.4
                color: Theme.cyan
            }
        }

        Text {
            Layout.alignment: Qt.AlignHCenter
            Layout.bottomMargin: Theme.u(6)
            text: "Connect to a HamDeck host"
            font.family: Theme.body; font.pixelSize: Theme.f(12)
            color: Theme.dim
        }

        // ⚠️ A SCHEME MAY BE TYPED HERE, and when it is, it decides the port.
        // `https://radio.wa0o.com` is TLS on 443 whatever the port box says;
        // a bare name or address is plain http on the port box. Settings::
        // ParseTarget is the one place that rule lives.
        Field {
            id: hostField
            label: "Host"
            placeholder: "hostname, address, or https://host"
            text: backend.savedHost
            Layout.fillWidth: true
            onAccepted: portField.focusField()
        }
        Field {
            id: portField
            label: "Port"
            placeholder: "5002 · ignored for https://"
            text: backend.savedPort > 0 ? String(backend.savedPort) : "5002"
            Layout.fillWidth: true
            onAccepted: userField.focusField()
        }
        Field {
            id: userField
            label: "Username"
            placeholder: ""
            text: backend.savedUser
            Layout.fillWidth: true
            onAccepted: passField.focusField()
        }
        Field {
            id: passField
            label: "Password"
            placeholder: "not saved"
            password: true
            Layout.fillWidth: true
            onAccepted: doConnect()
        }

        PanelKey {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.u(44)
            Layout.topMargin: Theme.u(4)
            text: backend.connecting ? "Connecting…" : "Connect"
            lit: !backend.connecting
            enabledKey: !backend.connecting
            onClicked: doConnect()
        }

        // ⚠️ The host's own message, passed through. It distinguishes bad
        // credentials from the five-minute lockout, and flattening both to
        // "login failed" leaves an operator retrying into the lockout.
        Text {
            Layout.fillWidth: true
            Layout.topMargin: Theme.u(2)
            visible: backend.lastError !== ""
            text: backend.lastError
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
            font.family: Theme.body; font.pixelSize: Theme.f(12)
            color: Theme.txRed
        }

        Text {
            Layout.fillWidth: true
            Layout.topMargin: Theme.u(8)
            horizontalAlignment: Text.AlignHCenter
            text: "Pre-release · tested against one radio · this controls a transmitter"
            wrapMode: Text.WordWrap
            font.family: Theme.body; font.pixelSize: Theme.f(10)
            color: Theme.dim
        }
    }

    // ⚠️ AN EMPTY PASSWORD IS NEVER SENT, AND THIS IS WHY. The password is wiped
    // the moment it has been used, and on a phone the software keyboard stays up
    // after Connect - so a second Return, or a stray tap on Go, re-submitted the
    // same host and username with NOTHING in the password. The host answered 401,
    // which dropped the live session, and the operator was thrown back to this
    // screen to log in again mid-session. It reads as the app randomly logging
    // out and it was this.
    function doConnect() {
        if (backend.connecting || backend.sessionActive) return
        if (passField.text === "") {
            passField.focusField()
            return
        }
        root.connectRequested(hostField.text, parseInt(portField.text) || 5002,
                              userField.text, passField.text)
        // Never keep the password in a property once it has been used.
        passField.text = ""
    }

    // ⚠️ AND THE KEYBOARD GOES AWAY ON SUCCESS. It covers half a handset, it is
    // over the panel the operator just asked for, and every key it hides is a
    // key on a transmitter.
    Connections {
        target: backend
        function onSessionChanged() {
            if (backend.sessionActive) Qt.inputMethod.hide()
        }
    }

    Component.onCompleted: {
        if (hostField.text === "") hostField.focusField()
        else if (userField.text === "") userField.focusField()
        else passField.focusField()
    }
}
