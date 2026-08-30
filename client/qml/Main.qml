import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Controls
import HamDeck

ApplicationWindow {
    id: win
    visible: true
    title: "HamDeck"
    color: Theme.panelDeep
    minimumWidth: 560
    minimumHeight: 400

    // ⚠️ Geometry is clamped by the backend and re-centred if it would land off
    // the work area. A window taller than the display puts its title bar out of
    // reach and the app cannot be closed.
    Component.onCompleted: {
        const g = backend.restoreGeometry(Screen.desktopAvailableWidth,
                                          Screen.desktopAvailableHeight)
        win.x = g.x; win.y = g.y; win.width = g.width; win.height = g.height
    }
    onClosing: {
        backend.saveGeometry(win.x, win.y, win.width, win.height)
        backend.shutdown()
    }
    onActiveChanged: if (!active) backend.focusLost()

    // The PTT hotkey. Auto-repeat is passed through so the backend can suppress
    // it — a held key repeats at the OS rate and would flap the transmitter.
    Item {
        anchors.fill: parent
        focus: true
        Keys.onPressed:  (e) => backend.keyPressed(e.key, e.isAutoRepeat)
        Keys.onReleased: (e) => backend.keyReleased(e.key, e.isAutoRepeat)
    }

    // ⚠️ Shown until a SESSION EXISTS, not until a host is configured. A
    // remembered host proves nothing about whether the credentials still work
    // or the host is reachable.
    ConnectPanel {
        anchors.fill: parent
        visible: !backend.sessionActive
        onConnectRequested: (host, port, user, password) =>
            backend.connectTo(host, port, user, password)
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true
        visible: backend.sessionActive

        ColumnLayout {
            width: win.width
            spacing: 10
            anchors.margins: 12

            Item { Layout.preferredHeight: 2 }

            Readout {
                Layout.fillWidth: true
                Layout.leftMargin: 12; Layout.rightMargin: 12
                freq: backend.freqText
                mode: backend.mode
                vfo: backend.vfo
                watts: backend.power
                stale: backend.stale
            }

            SMeter {
                Layout.fillWidth: true
                Layout.leftMargin: 12; Layout.rightMargin: 12
                raw: backend.sMeterRaw
                unit: backend.sUnit
                ticks: backend.meterTicks
                transmitting: backend.tx
            }

            Group {
                title: "Band"
                Layout.fillWidth: true
                Layout.leftMargin: 12; Layout.rightMargin: 12
                GridLayout {
                    columns: 6
                    columnSpacing: 8; rowSpacing: 8
                    Layout.fillWidth: true
                    Repeater {
                        model: ["160","80","60","40","30","20","17","15","12","10","6"]
                        delegate: PanelKey {
                            Layout.fillWidth: true
                            text: modelData + "m"
                            onClicked: backend.send("/api/band/" + modelData)
                        }
                    }
                }
            }

            Group {
                title: "Mode"
                Layout.fillWidth: true
                Layout.leftMargin: 12; Layout.rightMargin: 12
                RowLayout {
                    spacing: 8
                    Layout.fillWidth: true
                    Repeater {
                        model: [["LSB","lsb"],["USB","usb"],["CW","cw"],
                                ["AM","am"],["FM","fm"],["DATA","data"]]
                        delegate: PanelKey {
                            Layout.fillWidth: true
                            text: modelData[0]
                            // Lit from the RIG's reported mode, not from clicks.
                            lit: backend.mode === modelData[0]
                                 || (modelData[0] === "DATA" && backend.mode.indexOf("DATA") === 0)
                            onClicked: backend.send("/api/mode/" + modelData[1])
                        }
                    }
                }
            }

            Group {
                title: "VFO"
                Layout.fillWidth: true
                Layout.leftMargin: 12; Layout.rightMargin: 12
                RowLayout {
                    spacing: 8
                    Layout.fillWidth: true
                    PanelKey { Layout.fillWidth: true; text: "A"; lit: backend.vfo === "A"
                               onClicked: backend.send("/api/vfo/a") }
                    PanelKey { Layout.fillWidth: true; text: "B"; lit: backend.vfo === "B"
                               onClicked: backend.send("/api/vfo/b") }
                    PanelKey { Layout.fillWidth: true; text: "Swap"
                               onClicked: backend.send("/api/vfo/swap") }
                    PanelKey { Layout.fillWidth: true; text: "Split"
                               onClicked: backend.send("/api/split/toggle") }
                    PanelKey { Layout.fillWidth: true; text: "−1 kHz"
                               onClicked: backend.send("/api/step/1000/down") }
                    PanelKey { Layout.fillWidth: true; text: "+1 kHz"
                               onClicked: backend.send("/api/step/1000/up") }
                }
            }

            Group {
                title: "Receiver"
                Layout.fillWidth: true
                Layout.leftMargin: 12; Layout.rightMargin: 12
                RowLayout {
                    spacing: 8
                    Layout.fillWidth: true
                    Repeater {
                        model: [["NB","nb"],["NR","nr"],["Notch","notch"],["Att","att"],
                                ["VOX","vox"],["Comp","comp"],["Mon","mon"],["RIT","rit"]]
                        delegate: PanelKey {
                            Layout.fillWidth: true
                            text: modelData[0]
                            lit: backend.dsp[modelData[1]] === true
                            onClicked: backend.send("/api/" + modelData[1] + "/toggle")
                        }
                    }
                    PanelKey {
                        Layout.fillWidth: true
                        text: "AGC " + backend.agc
                        onClicked: backend.send("/api/agc/cycle")
                    }
                }
            }

            Group {
                title: "Frequency entry"
                Layout.fillWidth: true
                Layout.leftMargin: 12; Layout.rightMargin: 12
                RowLayout {
                    spacing: 6
                    Layout.fillWidth: true
                    Rectangle {
                        Layout.preferredWidth: 120
                        Layout.preferredHeight: 38
                        color: Theme.ground
                        radius: Theme.radius
                        border.width: 1; border.color: Theme.line
                        Text {
                            anchors.centerIn: parent
                            text: backend.freqBuffer === "" ? "—" : backend.freqBuffer
                            font.family: Theme.mono; font.pixelSize: 18
                            font.weight: Font.Medium
                            color: Theme.amber
                        }
                    }
                    Repeater {
                        model: 10
                        delegate: PanelKey {
                            Layout.fillWidth: true
                            text: String(index)
                            onClicked: backend.send("/api/freq/digit/" + index)
                        }
                    }
                    PanelKey { text: "⌫"; onClicked: backend.send("/api/freq/backspace") }
                    PanelKey { text: "Clr"; onClicked: backend.send("/api/freq/clear") }
                    PanelKey { text: "Go"; onClicked: backend.send("/api/freq/send") }
                }
            }

            Group {
                title: "Transmit"
                Layout.fillWidth: true
                Layout.leftMargin: 12; Layout.rightMargin: 12
                RowLayout {
                    spacing: 10
                    Layout.fillWidth: true

                    // ⚠️ Reflects the RIG's tx state, never its own click. Red
                    // means RF and nothing else in this app uses it.
                    PanelKey {
                        Layout.preferredWidth: 170
                        Layout.preferredHeight: 62
                        text: backend.tx ? "On Air" : "PTT"
                        lit: backend.tx
                        danger: true
                        onClicked: backend.send(backend.tx ? "/api/ptt/off" : "/api/ptt/on")
                    }

                    // Arming takes the host's single-transmitter claim; PTT keys
                    // the rig. Separate, so a connect never lands at the start
                    // of an over.
                    PanelKey {
                        Layout.preferredWidth: 104
                        Layout.preferredHeight: 62
                        text: backend.armed ? "Armed" : "Arm TX"
                        lit: backend.armed
                        // A test tone must be unmistakable: it wears the
                        // transmit colour, not the ordinary lit colour.
                        danger: backend.testTone
                        onClicked: backend.toggleArm()
                    }

                    ColumnLayout {
                        spacing: 1
                        SilkLabel { text: "SWR"; Layout.alignment: Qt.AlignHCenter }
                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            text: backend.swr
                            font.family: Theme.mono; font.pixelSize: 17; font.weight: Font.Medium
                            color: parseFloat(backend.swr) >= 2.0 ? Theme.txRed : Theme.text
                        }
                    }
                    ColumnLayout {
                        spacing: 1
                        SilkLabel { text: "ALC"; Layout.alignment: Qt.AlignHCenter }
                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            text: backend.alcPct + "%"
                            font.family: Theme.mono; font.pixelSize: 17; font.weight: Font.Medium
                            color: Theme.text
                        }
                    }
                    ColumnLayout {
                        spacing: 1
                        SilkLabel { text: "Pwr"; Layout.alignment: Qt.AlignHCenter }
                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            // Percent of rated output, never watts: the only
                            // watt table available is for a 100 W radio.
                            text: backend.powerPct + "%"
                            font.family: Theme.mono; font.pixelSize: 17; font.weight: Font.Medium
                            color: Theme.text
                        }
                    }

                    Item { Layout.fillWidth: true }

                    ColumnLayout {
                        spacing: 2
                        SilkLabel { text: "PTT key" }
                        RowLayout {
                            spacing: 6
                            ComboBox {
                                id: hkBox
                                Layout.preferredWidth: 132
                                model: backend.hotkeyChoices.map(c => c.label)
                                currentIndex: backend.hotkeyIndex
                                onActivated: backend.hotkeyIndex = currentIndex
                                ToolTip.visible: hovered
                                ToolTip.text: backend.hotkeyChoices[currentIndex]
                                              ? backend.hotkeyChoices[currentIndex].note : ""
                                ToolTip.delay: 400
                            }
                            PanelKey {
                                text: backend.hotkeyHold ? "Hold" : "Toggle"
                                onClicked: backend.hotkeyHold = !backend.hotkeyHold
                            }
                        }
                    }
                }
            }

            Item { Layout.preferredHeight: 4 }
        }
    }

    // Status bar
    footer: Rectangle {
        height: 26
        color: Theme.ground
        border.width: 0
        Rectangle { width: parent.width; height: 1; color: Theme.line }
        RowLayout {
            anchors { fill: parent; leftMargin: 12; rightMargin: 12 }
            Text {
                text: backend.tx && backend.txTimeoutIn > 0
                      ? "transmitting · watchdog drops PTT in " + backend.txTimeoutIn + " s"
                      : backend.connectionText + " · cache " + backend.cacheAgeMs + " ms"
                font.family: Theme.body; font.pixelSize: 11
                color: backend.tx ? Theme.txRed : (backend.stale ? Theme.amber : Theme.dim)
            }
            Item { Layout.fillWidth: true }
            Text {
                visible: backend.sessionActive
                text: "tx: " + backend.txStatus.replace("tx: ", "")
                font.family: Theme.body; font.pixelSize: 11
                color: backend.testTone && backend.armed ? Theme.txRed : Theme.dim
            }
            Text {
                visible: backend.sessionActive
                text: "· audio: " + backend.audioStatus
                font.family: Theme.body; font.pixelSize: 11
                color: Theme.dim
            }
            Text {
                // Say plainly that the hotkey is focus-only. Calling it global
                // when it is not is the same lie as a status route reporting ok
                // for something it never did.
                visible: backend.sessionActive
                text: "· PTT key: window focus only"
                font.family: Theme.body; font.pixelSize: 11
                color: Theme.dim
            }
            Text {
                // Nothing to disconnect from until there is a session.
                visible: backend.sessionActive
                text: "· disconnect"
                font.family: Theme.body; font.pixelSize: 11
                font.underline: discMouse.containsMouse
                color: discMouse.containsMouse ? Theme.cyan : Theme.dim
                MouseArea {
                    id: discMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    // Returns to the connect screen and drops the session, so
                    // the panel cannot sit there showing a rig it no longer has.
                    onClicked: backend.disconnectSession()
                }
            }
        }
    }
}
