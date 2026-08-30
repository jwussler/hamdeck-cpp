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

    // ⚠️ THE MINIMUM SIZE SCALES WITH THE PANEL. A fixed 560x400 minimum is
    // meaningless once every key inside it is being drawn 1.75x larger - the
    // window would allow a size the panel cannot be drawn at, and Qt resolves
    // that by ignoring the request and growing the window instead.
    // Never larger than the screen: a minimum bigger than the display is a
    // window that cannot be placed.
    minimumWidth: Math.min(Theme.u(560), Screen.desktopAvailableWidth)
    minimumHeight: Math.min(Theme.u(400), Screen.desktopAvailableHeight)

    // ── Resolution ──────────────────────────────────────────────────────────
    //
    // Density comes from the backend as ONE number and is applied through
    // Theme.u()/f(). Reflow is decided here, against the width actually
    // available, because it is a different question with a different answer:
    // a 4K monitor wants BIGGER keys, a narrow window wants FEWER PER ROW, and
    // a panel that only does one of the two is wrong on half the desks it
    // lands on.
    Binding { target: Theme; property: "scale"; value: backend.uiScale }

    // ⚠️ Re-reported when the window is dragged to another monitor. Screen is
    // an attached property that follows the window, so these fire on the move -
    // a scale computed once at startup is wrong the moment somebody drags the
    // panel from a laptop panel to the 4K on the desk, which is exactly what
    // happens every morning.
    Screen.onDesktopAvailableWidthChanged: win.reportScreen()
    Screen.onDesktopAvailableHeightChanged: win.reportScreen()
    Screen.onDevicePixelRatioChanged: win.reportScreen()
    function reportScreen() {
        backend.setScreen(Screen.desktopAvailableWidth,
                          Screen.desktopAvailableHeight,
                          Screen.devicePixelRatio)
    }

    // Width available to the contents of a Group, after the panel's own margins
    // and the group's padding. Every key row decides how many columns it can
    // take from this.
    //
    // ⚠️ Derived from the panel column's REAL width, not from the window's.
    // They are not the same number - a scrollbar, a margin and, as it turned
    // out, a container that resized the panel behind our back all sit between
    // them - and reflowing against a width the keys are not actually given is
    // how a row ends up one column too wide.
    readonly property int contentW: panelCol.width - Theme.pad * 4

    // ⚠️ Geometry is clamped by the backend and re-centred if it would land off
    // the work area. A window taller than the display puts its title bar out of
    // reach and the app cannot be closed.
    Component.onCompleted: {
        win.reportScreen()
        const g = backend.restoreGeometry(Screen.desktopAvailableWidth,
                                          Screen.desktopAvailableHeight)
        win.x = g.x; win.y = g.y; win.width = g.width; win.height = g.height
    }
    onClosing: {
        backend.saveGeometry(win.x, win.y, win.width, win.height)
        backend.shutdown()
    }
    onActiveChanged: if (!active) backend.focusLost()

    // ⚠️ THE PTT HOTKEY IS NOT HANDLED HERE ANY MORE, AND THAT IS THE FIX.
    // This used to be an Item with `focus: true` and Keys handlers. A QML Keys
    // handler fires only on the item that holds focus, and this panel is full
    // of things that take it — the connect screen's password field on startup,
    // then every dropdown, slider and the scroll area. The hotkey was dead in
    // the running application while its unit test passed. It is filtered at the
    // application in C++ now (Backend::eventFilter), which sees the key
    // whatever has focus, and consumes only the configured PTT key.

    // ⚠️ Shown until a SESSION EXISTS, not until a host is configured. A
    // remembered host proves nothing about whether the credentials still work
    // or the host is reachable.
    ConnectPanel {
        anchors.fill: parent
        visible: !backend.sessionActive
        onConnectRequested: (host, port, user, password) =>
            backend.connectTo(host, port, user, password)
    }

    // ⚠️ A FLICKABLE, NOT A ScrollView, AND THAT IS NOT A STYLE CHOICE.
    // ScrollView sizes its content item to the content's own natural width and
    // ignores a width binding on it: the panel came out 691 px wide inside both
    // a 1024 px and a 448 px window, so every row was laid out for a window
    // that did not exist and the right-hand column fell off the edge. Measured,
    // not guessed - --check-resolutions prints the panel width beside the
    // window width, which is how it was caught.
    // A Flickable leaves its children's geometry alone.
    Flickable {
        id: flick
        anchors.fill: parent
        clip: true
        visible: backend.sessionActive
        contentWidth: width
        contentHeight: panelCol.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        ColumnLayout {
            id: panelCol
            // ⚠️ Named, because --check-resolutions measures THIS item's
            // implicitWidth against the viewport. Its width is forced to the
            // viewport, so measuring width would be a test that cannot fail;
            // implicitWidth is what the content actually needs.
            objectName: "panelColumn"
            width: flick.width - Theme.u(16)   // room for the scrollbar
            spacing: Theme.u(10)

            Item { Layout.preferredHeight: Theme.u(2) }

            Readout {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.pad; Layout.rightMargin: Theme.pad
                // The wheel tunes, in the step the row below is set to. The
                // reference panel does this and it is the fastest way to move.
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
                // Typing a frequency: the parser and the range check live in
                // C++ so they are shared and tested (ctest -R freq).
                seedText: () => backend.freqEditText()
                onCommit: (text) => backend.setFreqText(text)
            }

            SMeter {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.pad; Layout.rightMargin: Theme.pad
                raw: backend.sMeterRaw
                unit: backend.sUnit
                ticks: backend.meterTicks
                transmitting: backend.tx
            }

            Group {
                title: "Band"
                Layout.fillWidth: true
                Layout.leftMargin: Theme.pad; Layout.rightMargin: Theme.pad
                GridLayout {
                    // Wraps rather than shrinks. Eleven band keys on a 1024-wide
                    // netbook become two rows of readable keys; squeezed onto
                    // one row they would be 40 px wide with the legend clipped.
                    columns: Theme.cols(win.contentW, Theme.minKeyW, 11)
                    columnSpacing: Theme.gap; rowSpacing: Theme.gap
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
                Layout.leftMargin: Theme.pad; Layout.rightMargin: Theme.pad
                GridLayout {
                    columns: Theme.cols(win.contentW, Theme.u(66), 6)
                    columnSpacing: Theme.gap; rowSpacing: Theme.gap
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
                Layout.leftMargin: Theme.pad; Layout.rightMargin: Theme.pad
                GridLayout {
                    // "−1 kHz" needs more room than a band number, so this row
                    // wraps earlier than the band row. One minimum width for
                    // every row would either waste space or clip these two.
                    columns: Theme.cols(win.contentW, Theme.u(78), 6)
                    columnSpacing: Theme.gap; rowSpacing: Theme.gap
                    Layout.fillWidth: true
                    PanelKey { Layout.fillWidth: true; text: "A"; lit: backend.vfo === "A"
                               onClicked: backend.send("/api/vfo/a") }
                    PanelKey { Layout.fillWidth: true; text: "B"; lit: backend.vfo === "B"
                               onClicked: backend.send("/api/vfo/b") }
                    PanelKey { Layout.fillWidth: true; text: "Swap"
                               onClicked: backend.send("/api/vfo/swap") }
                    PanelKey { Layout.fillWidth: true; text: "Split"
                               onClicked: backend.send("/api/split/toggle") }
                    // ⚠️ These follow the STEP SIZE below, so the label
                    // cannot say one thing while the rig moves another.
                    PanelKey { Layout.fillWidth: true
                               text: "− " + backend.stepLabel
                               onClicked: backend.send("/api/step/" + backend.stepHz + "/down") }
                    PanelKey { Layout.fillWidth: true
                               text: "+ " + backend.stepLabel
                               onClicked: backend.send("/api/step/" + backend.stepHz + "/up") }
                    PanelKey { Layout.fillWidth: true; text: "A▸B"
                               onClicked: backend.send("/api/vfo-copy/a2b") }
                    // One press: copy A to B and go split. The compound
                    // sequence runs ON the poller thread host-side, so a status
                    // poll cannot land mid-sequence and cache a half-applied
                    // state.
                    PanelKey { Layout.fillWidth: true; text: "Quick split"
                               onClicked: backend.send("/api/quick-split") }
                    // ⚠️ TWO DIFFERENT LOCKS, and they are not interchangeable.
                    // VFO LOCK is this host's software lock: it blocks every
                    // frequency-changing route for every caller, including
                    // local ones. LOCK is the rig's own CAT lock. Labelled
                    // separately because confusing them means an operator
                    // thinks their frequency is protected when it is not.
                    PanelKey {
                        Layout.fillWidth: true
                        text: "VFO lock"
                        lit: backend.vfoLocked
                        onClicked: backend.send("/api/vfo-lock/toggle")
                    }
                    PanelKey {
                        Layout.fillWidth: true
                        text: "Rig lock"
                        lit: backend.rigLocked
                        onClicked: backend.send("/api/toggle/lock")
                    }
                }
            }

            Group {
                title: "Tuning step"
                Layout.fillWidth: true
                Layout.leftMargin: Theme.pad; Layout.rightMargin: Theme.pad
                GridLayout {
                    columns: Theme.cols(win.contentW, Theme.u(70), 5)
                    columnSpacing: Theme.gap; rowSpacing: Theme.gap
                    Layout.fillWidth: true
                    Repeater {
                        model: [[10,"10 Hz"],[100,"100 Hz"],[1000,"1 kHz"],
                                [5000,"5 kHz"],[10000,"10 kHz"]]
                        delegate: PanelKey {
                            Layout.fillWidth: true
                            text: modelData[1]
                            lit: backend.stepHz === modelData[0]
                            onClicked: backend.stepHz = modelData[0]
                        }
                    }
                }
            }

            Group {
                title: "Receiver"
                Layout.fillWidth: true
                Layout.leftMargin: Theme.pad; Layout.rightMargin: Theme.pad
                GridLayout {
                    columns: Theme.cols(win.contentW, Theme.u(64), 9)
                    columnSpacing: Theme.gap; rowSpacing: Theme.gap
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
                    // IPO / AMP1 / AMP2 - cycled on the rig, so the label
                    // follows what the rig reports rather than a click count.
                    PanelKey {
                        Layout.fillWidth: true
                        text: "PRE " + backend.preampName
                        onClicked: backend.send("/api/preamp/cycle")
                    }
                    PanelKey {
                        Layout.fillWidth: true
                        text: "XIT"
                        lit: backend.dsp["xit"] === true
                        onClicked: backend.send("/api/xit/toggle")
                    }
                    PanelKey {
                        Layout.fillWidth: true
                        text: "Mute"
                        onClicked: backend.send("/api/mute/toggle")
                    }
                    PanelKey {
                        Layout.fillWidth: true
                        text: "Div"
                        lit: backend.diversity
                        onClicked: backend.send("/api/diversity/toggle")
                    }
                }
            }

            Group {
                title: "Antenna · Filter · RIT · Tuner"
                Layout.fillWidth: true
                Layout.leftMargin: Theme.pad; Layout.rightMargin: Theme.pad
                // ⚠️ A Flow, not a GridLayout: these keys are NATURAL width and
                // grouped by meaning, and a grid would break the groups across
                // rows at an arbitrary column. A Flow wraps between them and
                // keeps ANT, filter and RIT reading as three clusters.
                Flow {
                    Layout.fillWidth: true
                    spacing: Theme.gap

                    Repeater {
                        model: [1, 2, 3]
                        delegate: PanelKey {
                            text: "ANT " + modelData
                            // Lit from the rig's reported antenna, not the click.
                            lit: backend.ant === modelData
                            onClicked: backend.send("/api/ant/" + modelData)
                        }
                    }
                    Rectangle { width: 1; height: Theme.keyH; color: Theme.line }

                    Repeater {
                        model: [["Narrow","narrow"],["Med","medium"],["Wide","wide"]]
                        delegate: PanelKey {
                            text: modelData[0]
                            onClicked: backend.send("/api/width/" + modelData[1])
                        }
                    }
                    Rectangle { width: 1; height: Theme.keyH; color: Theme.line }

                    // RIT ON/OFF is separate from nudging it: clearing the
                    // offset and switching RIT off are different acts.
                    PanelKey { text: "RIT"; lit: backend.dsp["rit"] === true
                               onClicked: backend.send("/api/rit/toggle") }
                    PanelKey { text: "RIT −"; onClicked: backend.send("/api/rit/down") }
                    PanelKey { text: "RIT +"; onClicked: backend.send("/api/rit/up") }
                    PanelKey { text: "Clr";   onClicked: backend.send("/api/rit/clear") }

                    // ⚠️ THE TGXL, not the rig's internal ATU. They are different
                    // tuners and this station uses this one; the button says which.
                    // ⚠️ Lit from the HOST's tgxl_tuning, never from the click,
                    // and it says STOP while a carrier is up: the route is a
                    // toggle and the second press ends the transmission.
                    PanelKey {
                        text: backend.tunerActive ? "STOP TUNE" : "TUNE TGXL"
                        enabledKey: backend.tunerAvailable
                        danger: true
                        lit: backend.tunerActive
                        onClicked: backend.tuneTgxl()
                    }
                    Text {
                        text: backend.tunerActive ? "transmitting 15 W CW"
                                                  : backend.tunerStatus
                        height: Theme.keyH
                        verticalAlignment: Text.AlignVCenter
                        font.family: Theme.body; font.pixelSize: Theme.f(11)
                        color: backend.tunerAvailable ? Theme.dim : Theme.amber
                    }
                }
            }

            Group {
                title: "Frequency entry"
                Layout.fillWidth: true
                Layout.leftMargin: Theme.pad; Layout.rightMargin: Theme.pad
                Flow {
                    Layout.fillWidth: true
                    spacing: Theme.u(6)
                    Rectangle {
                        width: Theme.u(120)
                        height: Theme.keyH
                        color: Theme.ground
                        radius: Theme.radius
                        border.width: 1; border.color: Theme.line
                        Text {
                            anchors.centerIn: parent
                            text: backend.freqBuffer === "" ? "—" : backend.freqBuffer
                            font.family: Theme.mono; font.pixelSize: Theme.f(18)
                            font.weight: Font.Medium
                            color: Theme.amber
                        }
                    }
                    Repeater {
                        model: 10
                        delegate: PanelKey {
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
                Layout.leftMargin: Theme.pad; Layout.rightMargin: Theme.pad
                Flow {
                    Layout.fillWidth: true
                    spacing: Theme.u(10)

                    // ⚠️ Reflects the RIG's tx state, never its own click. Red
                    // means RF and nothing else in this app uses it.
                    //
                    // ⚠️ PTT AND ARM KEEP THEIR SIZE AT EVERY RESOLUTION - they
                    // scale, but they never wrap to something small. The one key
                    // that must be hit without looking is not the place to save
                    // space on a narrow window.
                    PanelKey {
                        width: Theme.u(170)
                        implicitHeight: Theme.u(62)
                        text: backend.tx ? "On Air" : "PTT"
                        lit: backend.tx
                        danger: true
                        onClicked: backend.send(backend.tx ? "/api/ptt/off" : "/api/ptt/on")
                    }

                    // Arming takes the host's single-transmitter claim; PTT keys
                    // the rig. Separate, so a connect never lands at the start
                    // of an over.
                    PanelKey {
                        width: Theme.u(104)
                        implicitHeight: Theme.u(62)
                        text: backend.armed ? "Armed" : "Arm TX"
                        lit: backend.armed
                        // A test tone must be unmistakable: it wears the
                        // transmit colour, not the ordinary lit colour.
                        danger: backend.testTone
                        onClicked: backend.toggleArm()
                    }

                    // SWR and power as bars; ALC stays a number because it has
                    // no useful scale to draw against - it is "is it hitting the
                    // limit", not a magnitude.
                    ColumnLayout {
                        width: Theme.u(150)
                        spacing: Theme.u(6)
                        MeterBar {
                            Layout.fillWidth: true
                            label: "SWR"
                            // 1.0-3.0 across the bar; beyond 3 it pins and reddens.
                            value: (parseFloat(backend.swr) - 1.0) / 2.0
                            readout: backend.swr
                            warn: parseFloat(backend.swr) >= 2.0
                        }
                        MeterBar {
                            Layout.fillWidth: true
                            label: "PWR"
                            value: backend.powerPct / 100
                            readout: backend.powerPct + "%"
                        }
                    }

                    ColumnLayout {
                        width: Theme.u(52)
                        spacing: 1
                        SilkLabel { text: "ALC"; Layout.alignment: Qt.AlignHCenter }
                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            text: backend.alcPct + "%"
                            font.family: Theme.mono; font.pixelSize: Theme.f(17)
                            font.weight: Font.Medium
                            color: Theme.text
                        }
                    }

                    // ⚠️ TWO PTT KEYS, AND THE DIFFERENCE IS STATED. The first
                    // works only while this window has focus and offers
                    // hold-to-talk. The second works anywhere - including with
                    // the logging program in front - but is press-to-TOGGLE,
                    // because Windows' RegisterHotKey has no key-up message.
                    ColumnLayout {
                        width: Theme.u(230)
                        spacing: Theme.u(2)
                        SilkLabel { text: "Global PTT key (works anywhere)" }
                        RowLayout {
                            spacing: Theme.u(6)
                            PanelCombo {
                                Layout.preferredWidth: Theme.u(150)
                                model: backend.globalHotkeyChoices
                                currentIndex: backend.globalHotkeyIndex
                                onActivated: backend.globalHotkeyIndex = currentIndex
                                ToolTip.visible: hovered
                                ToolTip.text: "Press to key, press again to unkey. " +
                                              "Works while another program is in front. " +
                                              "The host watchdog is what stops a toggle " +
                                              "left on."
                                ToolTip.delay: 400
                            }
                        }
                        // Armed, or exactly why not. A PTT key that silently
                        // does nothing is worse than no PTT key, and the usual
                        // cause - another program already holds the key - is
                        // not a fault in this app.
                        Text {
                            Layout.preferredWidth: Theme.u(230)
                            text: backend.globalHotkeyStatus
                            wrapMode: Text.WordWrap
                            font.family: Theme.body; font.pixelSize: Theme.f(10)
                            color: backend.globalHotkeyStatus.indexOf("armed") === 0
                                   ? Theme.okGreen : Theme.dim
                        }
                    }

                    ColumnLayout {
                        width: Theme.u(210)
                        spacing: Theme.u(2)
                        SilkLabel { text: "PTT key (this window only)" }
                        RowLayout {
                            spacing: Theme.u(6)
                            PanelCombo {
                                id: hkBox
                                Layout.preferredWidth: Theme.u(132)
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

            Group {
                title: "Levels"
                Layout.fillWidth: true
                Layout.leftMargin: Theme.pad; Layout.rightMargin: Theme.pad
                GridLayout {
                    // A knob below about 120 px is not usable with a mouse and
                    // its readout starts to clip, so these wrap to two rows on a
                    // narrow window rather than shrink.
                    columns: Theme.cols(win.contentW, Theme.u(120), 4)
                    columnSpacing: Theme.u(18); rowSpacing: Theme.u(12)
                    Layout.fillWidth: true
                    Knob {
                        Layout.fillWidth: true
                        label: "AF gain"; suffix: ""
                        // The rig reports 0-255; the route takes percent.
                        value: Math.round(backend.afGain * 100 / 255)
                        onMoved: (v) => backend.send("/api/volume/set/" + v)
                    }
                    Knob {
                        Layout.fillWidth: true
                        label: "RF gain"
                        value: Math.round(backend.rfGain * 100 / 255)
                        onMoved: (v) => backend.send("/api/rf-gain/set/" + v)
                    }
                    Knob {
                        Layout.fillWidth: true
                        label: "RF power"; suffix: " W"
                        from: 5; to: 200
                        value: backend.power
                        onMoved: (v) => backend.send("/api/power/set/" + v)
                    }
                    Knob {
                        Layout.fillWidth: true
                        label: "CW speed"; suffix: " wpm"
                        from: 4; to: 60
                        value: backend.cwSpeed
                        onMoved: (v) => backend.send("/api/cw-speed/set/" + v)
                    }
                }
            }

            Group {
                title: "Audio"
                Layout.fillWidth: true
                Layout.leftMargin: Theme.pad; Layout.rightMargin: Theme.pad
                GridLayout {
                    // A device name needs far more room than a knob, so this row
                    // takes the wider minimum: a combo box showing "USB Audio…"
                    // and nothing else does not tell an operator which device
                    // their audio is going to.
                    columns: Theme.cols(win.contentW, Theme.u(180), 4)
                    columnSpacing: Theme.u(16); rowSpacing: Theme.u(12)
                    Layout.fillWidth: true

                    Knob {
                        Layout.fillWidth: true
                        label: "Volume"; suffix: "%"
                        value: backend.volume
                        onMoved: (v) => backend.volume = v
                    }
                    Knob {
                        Layout.fillWidth: true
                        label: "Mic gain"; suffix: "%"
                        from: 0; to: 200
                        value: backend.micGain
                        onMoved: (v) => backend.micGain = v
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.u(2)
                        SilkLabel { text: "Speaker" }
                        PanelCombo {
                            Layout.fillWidth: true
                            model: backend.outputDevices
                            currentIndex: backend.outputIndex
                            onActivated: backend.outputIndex = currentIndex
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.u(2)
                        SilkLabel { text: "Microphone" }
                        PanelCombo {
                            Layout.fillWidth: true
                            model: backend.inputDevices
                            currentIndex: backend.inputIndex
                            onActivated: backend.inputIndex = currentIndex
                        }
                    }
                }
            }

            Group {
                title: "Recording"
                Layout.fillWidth: true
                Layout.leftMargin: Theme.pad; Layout.rightMargin: Theme.pad
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.gap
                    // ⚠️ Lit from the HOST's record status, not the click - the
                    // recorder can be started by another client, and a button
                    // that latches would claim a recording that never began.
                    PanelKey {
                        text: backend.recording ? "Stop recording" : "Record"
                        enabledKey: backend.recordAvailable
                        danger: backend.recording
                        lit: backend.recording
                        onClicked: backend.toggleRecording()
                    }
                    // The replay buffer is always running; this writes the last
                    // of it to a file. It is how you keep an over you did not
                    // know you wanted until it had happened.
                    PanelKey {
                        text: "Save replay"
                        enabledKey: backend.recordAvailable
                        onClicked: backend.saveReplay()
                    }
                    Text {
                        Layout.fillWidth: true
                        text: backend.recordStatus
                        elide: Text.ElideRight
                        font.family: Theme.body; font.pixelSize: Theme.f(11)
                        color: backend.recording ? Theme.txRed : Theme.dim
                    }
                }
            }

            Group {
                title: "Display"
                Layout.fillWidth: true
                Layout.leftMargin: Theme.pad; Layout.rightMargin: Theme.pad
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.gap
                    ColumnLayout {
                        spacing: Theme.u(2)
                        SilkLabel { text: "Panel size" }
                        PanelCombo {
                            Layout.preferredWidth: Theme.u(120)
                            model: backend.uiScaleModes
                            currentIndex: backend.uiScaleIndex
                            onActivated: backend.uiScaleIndex = currentIndex
                            ToolTip.visible: hovered
                            ToolTip.text: "Auto fits the panel to this screen. " +
                                          "A fixed size overrides it - useful when " +
                                          "the panel shares a monitor with a logging program."
                            ToolTip.delay: 400
                        }
                    }
                    // ⚠️ Says what was MEASURED and what was chosen from it. A
                    // scale with no stated basis cannot be told apart from one
                    // somebody typed in, and this is the line an operator will
                    // be asked to read out when the panel looks wrong.
                    Text {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignBottom
                        text: backend.displayInfo
                        elide: Text.ElideRight
                        font.family: Theme.mono; font.pixelSize: Theme.f(11)
                        color: Theme.dim
                    }
                }
            }

            Item { Layout.preferredHeight: Theme.u(4) }
        }
    }

    // Status bar
    footer: Rectangle {
        height: Theme.rowH
        color: Theme.ground
        border.width: 0
        Rectangle { width: parent.width; height: 1; color: Theme.line }
        RowLayout {
            anchors { fill: parent; leftMargin: Theme.pad; rightMargin: Theme.pad }
            spacing: Theme.u(6)
            Text {
                text: backend.tx && backend.txTimeoutIn > 0
                      ? "transmitting · watchdog drops PTT in " + backend.txTimeoutIn + " s"
                      : backend.connectionText + " · cache " + backend.cacheAgeMs + " ms"
                font.family: Theme.body; font.pixelSize: Theme.f(11)
                color: backend.tx ? Theme.txRed : (backend.stale ? Theme.amber : Theme.dim)
            }
            Item { Layout.fillWidth: true }
            // ⚠️ THE STATUS BAR DROPS ITEMS RIGHT TO LEFT AS THE WINDOW NARROWS,
            // in reverse order of how much they matter. Elided text in a status
            // bar is worse than absent text - "audio: strea…" reads as a
            // problem - and the transmit state, on the left, never drops.
            Text {
                visible: backend.sessionActive && win.width > Theme.u(720)
                text: "tx: " + backend.txStatus.replace("tx: ", "")
                font.family: Theme.body; font.pixelSize: Theme.f(11)
                color: backend.testTone && backend.armed ? Theme.txRed : Theme.dim
            }
            Text {
                visible: backend.sessionActive && win.width > Theme.u(860)
                text: "· audio: " + backend.audioStatus
                font.family: Theme.body; font.pixelSize: Theme.f(11)
                color: Theme.dim
            }
            Text {
                // Say plainly that the hotkey is focus-only. Calling it global
                // when it is not is the same lie as a status route reporting ok
                // for something it never did.
                visible: backend.sessionActive && win.width > Theme.u(1040)
                text: backend.globalHotkeyStatus.indexOf("armed") === 0
                      ? "· PTT key: " + backend.globalHotkeyStatus.replace("armed: ", "")
                      : "· PTT key: window focus only"
                font.family: Theme.body; font.pixelSize: Theme.f(11)
                color: Theme.dim
            }
            Text {
                // Nothing to disconnect from until there is a session.
                visible: backend.sessionActive
                text: "· disconnect"
                font.family: Theme.body; font.pixelSize: Theme.f(11)
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
