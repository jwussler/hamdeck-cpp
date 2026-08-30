import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import HamDeck

// The connect screen.
//
// ⚠️ THERE IS NO DEFAULT HOST, AND THERE MUST NOT BE. A hostname compiled into a
// public repo points every install at that station (CARRYOVER.md section 6). The
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
        width: Math.min(420, root.width - 48)
        spacing: 14

        // Wordmark. BRAND.md: HAM in text, DECK in the accent — and inside the
        // app the accent is CYAN, not amber, matching the surrounding UI.
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 0
            Text {
                text: "HAM"
                font.family: Theme.display; font.weight: Font.Bold
                font.pixelSize: 34; font.letterSpacing: 1.4
                color: Theme.text
            }
            Text {
                text: "DECK"
                font.family: Theme.display; font.weight: Font.Bold
                font.pixelSize: 34; font.letterSpacing: 1.4
                color: Theme.cyan
            }
        }

        Text {
            Layout.alignment: Qt.AlignHCenter
            Layout.bottomMargin: 6
            text: "Connect to a HamDeck host"
            font.family: Theme.body; font.pixelSize: 12
            color: Theme.dim
        }

        Field {
            id: hostField
            label: "Host"
            placeholder: "hostname or address"
            text: backend.savedHost
            Layout.fillWidth: true
            onAccepted: portField.focusField()
        }
        Field {
            id: portField
            label: "Port"
            placeholder: "5002"
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
            Layout.preferredHeight: 44
            Layout.topMargin: 4
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
            Layout.topMargin: 2
            visible: backend.lastError !== ""
            text: backend.lastError
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
            font.family: Theme.body; font.pixelSize: 12
            color: Theme.txRed
        }

        Text {
            Layout.fillWidth: true
            Layout.topMargin: 8
            horizontalAlignment: Text.AlignHCenter
            text: "Pre-release · tested against one radio · this controls a transmitter"
            wrapMode: Text.WordWrap
            font.family: Theme.body; font.pixelSize: 10
            color: Theme.dim
        }
    }

    function doConnect() {
        root.connectRequested(hostField.text, parseInt(portField.text) || 5002,
                              userField.text, passField.text)
        // Never keep the password in a property once it has been used.
        passField.text = ""
    }

    Component.onCompleted: {
        if (hostField.text === "") hostField.focusField()
        else if (userField.text === "") userField.focusField()
        else passField.focusField()
    }
}
