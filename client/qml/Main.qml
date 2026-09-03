import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Controls
import HamDeck

ApplicationWindow {
    id: win
    visible: true
    title: "HamDeck"

    // ⚠️ THE PHONE IS A DIFFERENT SHAPE, NOT A SMALLER DESKTOP. The panel below
    // is one long column of groups, which is right on a monitor and is exactly
    // what the operator called "really hard to work on a phone": every control
    // sits behind a scroll, including the three looked at constantly - the
    // frequency, the S-meter and the PTT.
    //
    // On a handset the same groups become: pinned head, a tab row, ONE group at
    // a time, and a PTT bar under the thumb. NOTHING IS REMOVED and no control
    // is redrawn - a tab only decides which group is visible - so there stays
    // exactly one definition of every key on both shapes.
    //
    // ⚠️ MEASURED IN THEME UNITS, NOT RAW PIXELS. Theme.u() carries the density
    // scale, so this threshold means the same thing on a 4K monitor as on a
    // handset, which is the entire point of that scale (WIP.md 8d).
    // ⚠️ FROM C++, NOT FROM width < Theme.u(600). That test is circular - Theme.u
    // IS the scale, and the scale now depends on being a phone - so it settles on
    // whichever answer it started from. Backend::phoneLayout reads the screen's
    // logical width, which nothing here can move.
    readonly property bool phone: backend.phoneLayout
    property string tab: "band"
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
    // ⚠️ BOTH ROOTS GET THE INSETS, not just the panel. The connect screen is
    // where the operator types a password on a phone, and a text field under the
    // home indicator is one the software keyboard fights for.
    ConnectPanel {
        anchors.fill: parent
        // On a phone the pinned head and the PTT bar already sit inside the safe
        // area, so re-applying the insets here would only add a second gap.
        anchors.topMargin: win.phone ? Theme.u(6) : backend.safeTop
        anchors.bottomMargin: win.phone ? Theme.u(4) : backend.safeBottom
        anchors.leftMargin: backend.safeLeft
        anchors.rightMargin: backend.safeRight
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
    // ── The phone head: pinned, never scrolled away ──────────────────────────
    PanelHead {
        id: phoneHead
        visible: win.phone && backend.sessionActive
        anchors {
            top: parent.top; left: parent.left; right: parent.right
            topMargin: backend.safeTop + Theme.u(2)
            leftMargin: backend.safeLeft; rightMargin: backend.safeRight
        }
    }

    // ── The tab row ───────────────────────────────────────────────────────────
    // ⚠️ CHROME, NOT CONTROLS. These were PanelKeys, drawn exactly like the band
    // and mode keys below them, so the navigation competed with the panel it
    // navigates - eight lit boxes shouting as loudly as the radio's own state.
    // A tab is flat, and the only thing that marks the current one is an
    // underline in the accent, which is what the group titles already do.
    //
    // ⚠️ ONE ROW. Two rows of blocks was most of what "the layout looks off"
    // was: the tabs took a fifth of the screen and read as a second control
    // panel. Eight equal columns fit a 375-point phone at 44 points wide, which
    // is the touch floor, and the row is 40 tall so the target is honest even
    // though the paint is light.
    Row {
        id: tabBar
        visible: win.phone && backend.sessionActive
        anchors {
            top: phoneHead.bottom; left: parent.left; right: parent.right
            topMargin: Theme.u(10)
            leftMargin: backend.safeLeft + Theme.pad
            rightMargin: backend.safeRight + Theme.pad
        }
        height: visible ? Theme.u(40) : 0

        Repeater {
            model: [
                { key: "band", label: "BAND" },
                { key: "mode", label: "MODE" },
                { key: "vfo",  label: "VFO" },
                { key: "rx",   label: "RX" },
                { key: "ant",  label: "ANT" },
                { key: "keys", label: "FREQ" },
                { key: "tx",   label: "TX" },
                { key: "set",  label: "SET" }
            ]
            delegate: Item {
                required property var modelData
                width: tabBar.width / 8
                height: tabBar.height

                Text {
                    anchors.centerIn: parent
                    text: modelData.label
                    font.family: Theme.display
                    font.pixelSize: Theme.f(12)
                    font.letterSpacing: 0.6
                    color: win.tab === modelData.key ? Theme.cyan : Theme.dim
                }
                Rectangle {
                    anchors { bottom: parent.bottom; horizontalCenter: parent.horizontalCenter }
                    width: parent.width - Theme.u(8)
                    height: 2
                    color: win.tab === modelData.key ? Theme.cyan : "transparent"
                }
                MouseArea { anchors.fill: parent; onClicked: win.tab = modelData.key }
            }
        }
    }

    // The hairline the tabs sit on, so the head reads as one instrument face and
    // the panel below it as another.
    Rectangle {
        visible: tabBar.visible
        anchors { top: tabBar.bottom; left: parent.left; right: parent.right
                  leftMargin: backend.safeLeft; rightMargin: backend.safeRight }
        height: 1
        color: Theme.line
    }

    // ── The always-there strip ───────────────────────────────────────────────
    // ⚠️ THESE ARE PINNED BECAUSE THE OPERATOR SAID SO, and the reason holds up:
    // a tune is reached for mid-over, and LSB/USB/CW is the switch actually made
    // on the air. Everything else can cost a tab; these two cannot.
    //
    // ⚠️ EVERY KEY HERE IS THE SAME CONTROL AS THE ONE IN ITS GROUP - same call,
    // same lit-from-the-rig binding - so a tune started from the ANT tab shows
    // as running here too.
    //
    // ⚠️ ONE GRID FOR BOTH ROWS. They were two independent rows with their own
    // widths, so nothing lined up: three equal keys and a wide one above a
    // narrow ARM and a wide PTT, none of the edges meeting. Four columns, shared
    // by both, is most of the difference between "off" and "an instrument".
    GridLayout {
        id: quickStrip
        visible: win.phone && backend.sessionActive
        columns: 4
        columnSpacing: Theme.gap
        rowSpacing: Theme.gap
        anchors {
            bottom: parent.bottom; left: parent.left; right: parent.right
            bottomMargin: backend.safeBottom + Theme.u(6)
            leftMargin: backend.safeLeft + Theme.pad
            rightMargin: backend.safeRight + Theme.pad
        }
        height: visible ? implicitHeight : 0

        Repeater {
            model: [["LSB","lsb"],["USB","usb"],["CW","cw"]]
            delegate: PanelKey {
                required property var modelData
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.u(42)
                text: modelData[0]
                // Lit from the RIG's reported mode, never from the tap.
                lit: backend.mode === modelData[0]
                onClicked: backend.send("/api/mode/" + modelData[1])
            }
        }

        // ⚠️ It transmits, so it wears the transmit colour, and it says STOP
        // while it runs - a tune key that looks the same running as idle is one
        // an operator presses twice.
        PanelKey {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.u(42)
            text: backend.tunerActive ? "STOP" : "TUNE"
            enabledKey: backend.tunerAvailable
            danger: true
            lit: backend.tunerActive
            onClicked: backend.tuneTgxl()
        }

        // Arming claims the host's single transmitter; PTT keys the rig. Kept
        // separate for the same reason as on the desktop panel: connecting must
        // never land at the start of an over.
        PanelKey {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.u(60)
            text: backend.armed ? "ARMED" : "ARM"
            lit: backend.armed
            danger: backend.testTone
            onClicked: backend.toggleArm()
        }
        PanelKey {
            Layout.fillWidth: true
            Layout.columnSpan: 3
            Layout.preferredHeight: Theme.u(60)
            text: backend.tx ? "ON AIR" : "PTT"
            lit: backend.tx
            danger: true
            onClicked: backend.send(backend.tx ? "/api/ptt/off" : "/api/ptt/on")
        }
    }

    Flickable {
        id: flick
        // ⚠️ EXPLICIT ANCHORS, NOT anchors.fill WITH OVERRIDES. Mixing fill with
        // individual anchors is how an item ends up with a height nothing set
        // and a panel that renders zero pixels tall - which lays out and paints
        // perfectly well, and shows the operator an empty screen.
        anchors.top: win.phone ? tabBar.bottom : parent.top
        anchors.bottom: win.phone ? quickStrip.top : parent.bottom
        // ⚠️ On a phone the column is stretched to the viewport when it is
        // SHORTER than it, so the open group's card fills the space instead of
        // floating above a hole. Taller content still scrolls: contentHeight
        // below is the larger of the two.
        anchors.left: parent.left
        anchors.right: parent.right
        // ⚠️ THE WINDOW IS NOT THE USABLE AREA ON A PHONE. The notch, the
        // rounded corners and the home indicator are inside the window and
        // outside what a thumb can reach - so every key can measure as present
        // and on-screen while the top row sits under the camera housing.
        // win.color still paints edge to edge behind this, so the ground runs
        // under the notch the way an instrument face should.
        anchors.topMargin: backend.safeTop
        anchors.bottomMargin: backend.safeBottom
        anchors.leftMargin: backend.safeLeft
        anchors.rightMargin: backend.safeRight
        clip: true
        visible: backend.sessionActive
        contentWidth: width
        contentHeight: Math.max(panelCol.implicitHeight, height)
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        ColumnLayout {
            id: panelCol
            // ⚠️ Named, because --check-resolutions measures THIS item's
            // implicitWidth against the viewport. Its width is forced to the
            // viewport, so measuring width would be a test that cannot fail;
            // implicitWidth is what the content actually needs.
            objectName: "panelColumn"
            // ⚠️ THE SCROLLBAR GUTTER IS A DESKTOP THING. Reserving 16 units on
            // a phone pulled every group card 16 units left of the tabs and the
            // PTT row, which is exactly the kind of half-alignment that reads as
            // "off" without naming itself. A touch scrollbar overlays.
            width: win.phone ? flick.width : flick.width - Theme.u(16)
            // ⚠️ NO SPACING ON A PHONE. Exactly one group is visible there and it
            // is sized to the viewport, so column spacing and the leading spacer
            // only pushed the card down away from the tab it belongs to - 14 px
            // of it, measured off the render.
            spacing: win.phone ? 0 : Theme.u(10)

            Item { Layout.preferredHeight: win.phone ? 0 : Theme.u(2) }

            // ⚠️ ONE DEFINITION - PanelHead.qml. The phone pins its own copy above
            // the tabs; this is the desktop's.
            //
            // ⚠️ A LOADER, NOT visible:false. An invisible head still left a
            // 40 px hole between the tabs and the first card on the phone -
            // measured off the rendered PNG, not guessed at - and a gap with no
            // cause in the layout is exactly the kind of thing that reads as
            // "off". Not loading it at all cannot leave a gap, and it also stops
            // the phone building a second readout it never shows.
            Loader {
                active: !win.phone
                visible: active
                Layout.fillWidth: true
                sourceComponent: PanelHead {}
            }

            Group {
                title: "Band"
                // ⚠️ On a phone a tab hides this group; on a desktop nothing does.
                visible: !win.phone || win.tab === "band"
                Layout.fillWidth: true
                // ⚠️ SIZED AGAINST THE VIEWPORT, NOT BY Layout.fillHeight - see
                // the note on the other groups.
                Layout.preferredHeight: win.phone
                    ? Math.max(implicitHeight, flick.height - Theme.u(6))
                    : implicitHeight
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
                // ⚠️ On a phone a tab hides this group; on a desktop nothing does.
                visible: !win.phone || win.tab === "mode"
                Layout.fillWidth: true
                // ⚠️ SIZED AGAINST THE VIEWPORT, NOT BY Layout.fillHeight.
                // Stretching the column to the viewport and letting the open
                // group fill it looked right and was not: measured off the
                // rendered PNG, the card sat 39 px below the tabs and 50 px
                // above the strip, centred in a cell it never grew into. This
                // binding reads only the Flickable's height, which depends on
                // the pinned chrome and never on the column - so it cannot loop,
                // and a group taller than the screen still scrolls.
                Layout.preferredHeight: win.phone
                    ? Math.max(implicitHeight, flick.height - Theme.u(6))
                    : implicitHeight
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
                // ⚠️ On a phone a tab hides this group; on a desktop nothing does.
                visible: !win.phone || win.tab === "vfo"
                Layout.fillWidth: true
                // ⚠️ SIZED AGAINST THE VIEWPORT, NOT BY Layout.fillHeight.
                // Stretching the column to the viewport and letting the open
                // group fill it looked right and was not: measured off the
                // rendered PNG, the card sat 39 px below the tabs and 50 px
                // above the strip, centred in a cell it never grew into. This
                // binding reads only the Flickable's height, which depends on
                // the pinned chrome and never on the column - so it cannot loop,
                // and a group taller than the screen still scrolls.
                Layout.preferredHeight: win.phone
                    ? Math.max(implicitHeight, flick.height - Theme.u(6))
                    : implicitHeight
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
                // ⚠️ On a phone a tab hides this group; on a desktop nothing does.
                visible: !win.phone || win.tab === "vfo"
                Layout.fillWidth: true
                // ⚠️ SIZED AGAINST THE VIEWPORT, NOT BY Layout.fillHeight.
                // Stretching the column to the viewport and letting the open
                // group fill it looked right and was not: measured off the
                // rendered PNG, the card sat 39 px below the tabs and 50 px
                // above the strip, centred in a cell it never grew into. This
                // binding reads only the Flickable's height, which depends on
                // the pinned chrome and never on the column - so it cannot loop,
                // and a group taller than the screen still scrolls.
                Layout.preferredHeight: win.phone
                    ? Math.max(implicitHeight, flick.height - Theme.u(6))
                    : implicitHeight
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
                // ⚠️ On a phone a tab hides this group; on a desktop nothing does.
                visible: !win.phone || win.tab === "rx"
                Layout.fillWidth: true
                // ⚠️ SIZED AGAINST THE VIEWPORT, NOT BY Layout.fillHeight.
                // Stretching the column to the viewport and letting the open
                // group fill it looked right and was not: measured off the
                // rendered PNG, the card sat 39 px below the tabs and 50 px
                // above the strip, centred in a cell it never grew into. This
                // binding reads only the Flickable's height, which depends on
                // the pinned chrome and never on the column - so it cannot loop,
                // and a group taller than the screen still scrolls.
                Layout.preferredHeight: win.phone
                    ? Math.max(implicitHeight, flick.height - Theme.u(6))
                    : implicitHeight
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
                // ⚠️ On a phone a tab hides this group; on a desktop nothing does.
                visible: !win.phone || win.tab === "ant"
                Layout.fillWidth: true
                // ⚠️ SIZED AGAINST THE VIEWPORT, NOT BY Layout.fillHeight.
                // Stretching the column to the viewport and letting the open
                // group fill it looked right and was not: measured off the
                // rendered PNG, the card sat 39 px below the tabs and 50 px
                // above the strip, centred in a cell it never grew into. This
                // binding reads only the Flickable's height, which depends on
                // the pinned chrome and never on the column - so it cannot loop,
                // and a group taller than the screen still scrolls.
                Layout.preferredHeight: win.phone
                    ? Math.max(implicitHeight, flick.height - Theme.u(6))
                    : implicitHeight
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
                // ⚠️ On a phone a tab hides this group; on a desktop nothing does.
                visible: !win.phone || win.tab === "keys"
                Layout.fillWidth: true
                // ⚠️ SIZED AGAINST THE VIEWPORT, NOT BY Layout.fillHeight.
                // Stretching the column to the viewport and letting the open
                // group fill it looked right and was not: measured off the
                // rendered PNG, the card sat 39 px below the tabs and 50 px
                // above the strip, centred in a cell it never grew into. This
                // binding reads only the Flickable's height, which depends on
                // the pinned chrome and never on the column - so it cannot loop,
                // and a group taller than the screen still scrolls.
                Layout.preferredHeight: win.phone
                    ? Math.max(implicitHeight, flick.height - Theme.u(6))
                    : implicitHeight
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
                // ⚠️ On a phone a tab hides this group; on a desktop nothing does.
                visible: !win.phone || win.tab === "tx"
                Layout.fillWidth: true
                // ⚠️ SIZED AGAINST THE VIEWPORT, NOT BY Layout.fillHeight.
                // Stretching the column to the viewport and letting the open
                // group fill it looked right and was not: measured off the
                // rendered PNG, the card sat 39 px below the tabs and 50 px
                // above the strip, centred in a cell it never grew into. This
                // binding reads only the Flickable's height, which depends on
                // the pinned chrome and never on the column - so it cannot loop,
                // and a group taller than the screen still scrolls.
                Layout.preferredHeight: win.phone
                    ? Math.max(implicitHeight, flick.height - Theme.u(6))
                    : implicitHeight
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
                            // The press count turns "it does nothing" into one
                            // of two answers: the key never registered, or it
                            // registered and Windows is not delivering it.
                            text: backend.globalHotkeyStatus +
                                  (backend.globalHotkeyPresses > 0
                                   ? " · " + backend.globalHotkeyPresses + " presses"
                                   : "")
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

            // ⚠️ THE DRIVE TEST. One of these keys the transmitter and the other
            // does not, and the buttons say which - a control that transmits must
            // never be discoverable only by pressing it.
            Group {
                title: "Drive test"
                visible: !win.phone || win.tab === "tx"
                Layout.fillWidth: true
                Layout.preferredHeight: win.phone
                    ? Math.max(implicitHeight, flick.height - Theme.u(6))
                    : implicitHeight
                Layout.leftMargin: Theme.pad; Layout.rightMargin: Theme.pad

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.u(8)

                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: "A tone sweep TRANSMITS: it keys the rig and walks the mic gain " +
                              "up a ladder, recording what the radio reports at each step. " +
                              "A voice check keys nothing - you transmit as usual and it watches " +
                              "your peaks. A steady tone holds ALC where speech only touches it, " +
                              "so the sweep tells you the chain works and over what range, and " +
                              "the voice check tells you where YOUR peaks land."
                        font.family: Theme.body; font.pixelSize: Theme.f(12)
                        color: Theme.dim
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: Theme.gap; rowSpacing: Theme.gap

                        PanelKey {
                            Layout.fillWidth: true
                            Layout.preferredHeight: Theme.u(46)
                            text: backend.driveTestMode === "sweep" ? "STOP" : "TONE SWEEP · TX"
                            danger: true
                            lit: backend.driveTestMode === "sweep"
                            enabledKey: !backend.driveTestActive || backend.driveTestMode === "sweep"
                            onClicked: backend.driveTestMode === "sweep"
                                       ? backend.stopDriveTest() : backend.startToneSweep()
                        }
                        PanelKey {
                            Layout.fillWidth: true
                            Layout.preferredHeight: Theme.u(46)
                            text: backend.driveTestMode === "voice" ? "STOP" : "VOICE CHECK"
                            lit: backend.driveTestMode === "voice"
                            enabledKey: !backend.driveTestActive || backend.driveTestMode === "voice"
                            onClicked: backend.driveTestMode === "voice"
                                       ? backend.stopDriveTest() : backend.startVoiceCheck()
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: backend.driveTestStatus !== ""
                        wrapMode: Text.WordWrap
                        text: backend.driveTestStatus
                        font.family: Theme.body; font.pixelSize: Theme.f(13)
                        color: backend.driveTestMode === "sweep" ? Theme.txRed : Theme.text
                    }

                    // ⚠️ THE WHOLE CURVE, not just the pick. A single recommended
                    // number hides whether the radio responded gently across the
                    // range or slammed from nothing to pinned between two steps,
                    // and those want different things done about them.
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: backend.driveTestRows.length > 0
                        spacing: 2

                        RowLayout {
                            Layout.fillWidth: true
                            SilkLabel { text: "Gain"; Layout.preferredWidth: Theme.u(52) }
                            SilkLabel { text: "Drive"; Layout.preferredWidth: Theme.u(52) }
                            SilkLabel { text: "ALC"; Layout.preferredWidth: Theme.u(52) }
                            SilkLabel { text: "Power"; Layout.fillWidth: true }
                        }
                        Repeater {
                            model: backend.driveTestRows
                            delegate: RowLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                Text {
                                    text: modelData.gain + "%"
                                    Layout.preferredWidth: Theme.u(52)
                                    font.family: Theme.mono; font.pixelSize: Theme.f(12)
                                    color: Theme.text
                                }
                                Text {
                                    text: modelData.drive + "%"
                                    Layout.preferredWidth: Theme.u(52)
                                    font.family: Theme.mono; font.pixelSize: Theme.f(12)
                                    color: modelData.drive === 0 ? Theme.txRed : Theme.text
                                }
                                Text {
                                    text: modelData.alc + "%"
                                    Layout.preferredWidth: Theme.u(52)
                                    font.family: Theme.mono; font.pixelSize: Theme.f(12)
                                    // In the band the rig wants, or not.
                                    color: modelData.alc >= 50 && modelData.alc <= 75
                                           ? Theme.okGreen
                                           : (modelData.alc > 90 ? Theme.txRed : Theme.dim)
                                }
                                Text {
                                    text: modelData.pwr + "%"
                                    Layout.fillWidth: true
                                    font.family: Theme.mono; font.pixelSize: Theme.f(12)
                                    color: Theme.text
                                }
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: backend.driveTestResult !== ""
                        wrapMode: Text.WordWrap
                        text: backend.driveTestResult
                        font.family: Theme.body; font.pixelSize: Theme.f(13)
                        color: Theme.text
                    }

                    PanelKey {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.u(42)
                        visible: backend.driveTestBestGain > 0 && !backend.driveTestActive
                        text: "SET MIC GAIN TO " + backend.driveTestBestGain + "%"
                        onClicked: backend.applyBestGain()
                    }
                }
            }

            // ⚠️ WHERE THE ALC NUMBER COMES FROM, said out loud next to the
            // control it is used to set. The host derives alc_pct from hamlib's
            // yaesu_default_alc_cal (see /api/meters/scale), which is a default
            // table, not a measurement of this radio. An operator setting drive
            // against it is entitled to know that before they trust the last
            // few percent of it.
            Group {
                title: "Drive · where the numbers come from"
                visible: win.phone && win.tab === "tx"
                Layout.fillWidth: true
                Layout.leftMargin: Theme.pad; Layout.rightMargin: Theme.pad
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: "Drive in is measured at the HOST - what actually reached the radio, " +
                          "not what this phone sent. ALC % and power % come from hamlib's default " +
                          "Yaesu tables, not from a calibration of this rig, so treat the 50-75% " +
                          "band as where the radio wants to sit rather than as a measurement. " +
                          "SWR is a ratio from the same source."
                    font.family: Theme.body; font.pixelSize: Theme.f(12)
                    color: Theme.dim
                }
            }

            Group {
                title: "Levels"
                // ⚠️ On a phone a tab hides this group; on a desktop nothing does.
                visible: !win.phone || win.tab === "tx"
                Layout.fillWidth: true
                // ⚠️ SIZED AGAINST THE VIEWPORT, NOT BY Layout.fillHeight.
                // Stretching the column to the viewport and letting the open
                // group fill it looked right and was not: measured off the
                // rendered PNG, the card sat 39 px below the tabs and 50 px
                // above the strip, centred in a cell it never grew into. This
                // binding reads only the Flickable's height, which depends on
                // the pinned chrome and never on the column - so it cannot loop,
                // and a group taller than the screen still scrolls.
                Layout.preferredHeight: win.phone
                    ? Math.max(implicitHeight, flick.height - Theme.u(6))
                    : implicitHeight
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
                // ⚠️ On a phone a tab hides this group; on a desktop nothing does.
                visible: !win.phone || win.tab === "tx"
                Layout.fillWidth: true
                // ⚠️ SIZED AGAINST THE VIEWPORT, NOT BY Layout.fillHeight.
                // Stretching the column to the viewport and letting the open
                // group fill it looked right and was not: measured off the
                // rendered PNG, the card sat 39 px below the tabs and 50 px
                // above the strip, centred in a cell it never grew into. This
                // binding reads only the Flickable's height, which depends on
                // the pinned chrome and never on the column - so it cannot loop,
                // and a group taller than the screen still scrolls.
                Layout.preferredHeight: win.phone
                    ? Math.max(implicitHeight, flick.height - Theme.u(6))
                    : implicitHeight
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
                        // ⚠️ THE MICROPHONE'S STATE BELONGS NEXT TO THE
                        // MICROPHONE. This lived only in the status strip along
                        // the bottom edge, where on a shorter window it is off
                        // screen entirely - so a client that armed, said
                        // "armed", and sent no audio at all looked completely
                        // healthy, and the transmitter keyed into silence.
                        //
                        // Nobody should have to run a command to find out
                        // whether their microphone is working.
                        Text {
                            Layout.fillWidth: true
                            visible: backend.armed
                            text: backend.txStatus.indexOf("NO MICROPHONE") >= 0
                                  ? "⚠ " + backend.txStatus.replace("tx: ", "")
                                  : backend.txStatus.replace("tx: ", "")
                            wrapMode: Text.WordWrap
                            font.family: Theme.body; font.pixelSize: Theme.f(11)
                            color: backend.txStatus.indexOf("NO MICROPHONE") >= 0
                                   ? Theme.txRed : Theme.dim
                        }
                    }
                }
            }

            Group {
                title: "Recording"
                // ⚠️ On a phone a tab hides this group; on a desktop nothing does.
                visible: !win.phone || win.tab === "set"
                Layout.fillWidth: true
                // ⚠️ SIZED AGAINST THE VIEWPORT, NOT BY Layout.fillHeight.
                // Stretching the column to the viewport and letting the open
                // group fill it looked right and was not: measured off the
                // rendered PNG, the card sat 39 px below the tabs and 50 px
                // above the strip, centred in a cell it never grew into. This
                // binding reads only the Flickable's height, which depends on
                // the pinned chrome and never on the column - so it cannot loop,
                // and a group taller than the screen still scrolls.
                Layout.preferredHeight: win.phone
                    ? Math.max(implicitHeight, flick.height - Theme.u(6))
                    : implicitHeight
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

            // ⚠️ THE PHONE'S STATUS BAR, AT A SIZE A PERSON CAN READ. Same facts
            // the desktop footer carries - which host, how fresh the data is,
            // what the audio session actually did - but as panel rows instead of
            // 9 px type along the bottom edge.
            Group {
                title: "Connection"
                visible: win.phone && win.tab === "set"
                Layout.fillWidth: true
                Layout.leftMargin: Theme.pad; Layout.rightMargin: Theme.pad
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.u(8)

                    Repeater {
                        model: [
                            { k: "Host",  v: backend.connectionText },
                            { k: "Data",  v: backend.cacheAgeMs + " ms old" },
                            { k: "Audio", v: backend.audioSessionText !== ""
                                             ? backend.audioSessionText : backend.audioStatus },
                            { k: "TX",    v: backend.txStatus.replace("tx: ", "") }
                        ]
                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: Theme.u(10)
                            SilkLabel {
                                text: modelData.k
                                Layout.preferredWidth: Theme.u(54)
                            }
                            Text {
                                Layout.fillWidth: true
                                text: modelData.v
                                wrapMode: Text.WrapAnywhere
                                font.family: Theme.body; font.pixelSize: Theme.f(13)
                                color: backend.stale ? Theme.amber : Theme.text
                            }
                        }
                    }

                    // ⚠️ A DELIBERATE ACT, AT THE BOTTOM OF A TAB, IN THE
                    // TRANSMIT COLOUR. Disconnecting drops the session and hands
                    // the station back - the host drops to the local power cap
                    // and puts MOD SOURCE back to MIC - so it must not be
                    // something a thumb does on the way past.
                    PanelKey {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.u(46)
                        Layout.topMargin: Theme.u(6)
                        text: "DISCONNECT"
                        danger: true
                        onClicked: backend.disconnectSession()
                    }
                }
            }

            Group {
                title: "Display"
                // ⚠️ On a phone a tab hides this group; on a desktop nothing does.
                visible: !win.phone || win.tab === "set"
                Layout.fillWidth: true
                // ⚠️ SIZED AGAINST THE VIEWPORT, NOT BY Layout.fillHeight.
                // Stretching the column to the viewport and letting the open
                // group fill it looked right and was not: measured off the
                // rendered PNG, the card sat 39 px below the tabs and 50 px
                // above the strip, centred in a cell it never grew into. This
                // binding reads only the Flickable's height, which depends on
                // the pinned chrome and never on the column - so it cannot loop,
                // and a group taller than the screen still scrolls.
                Layout.preferredHeight: win.phone
                    ? Math.max(implicitHeight, flick.height - Theme.u(6))
                    : implicitHeight
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
    // ⚠️ DESKTOP ONLY. On a handset this was a row of 9 px text along the bottom
    // edge - the operator's words: "useless to see on the phone" - ending in a
    // "· disconnect" link the size of a hairline, which is a hard thing to hit
    // deliberately and an easy one to hit by accident. The phone gets the same
    // information at a readable size, and a real key, in the SET tab.
    footer: Rectangle {
        visible: !win.phone
        height: visible ? Theme.rowH : 0
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
                        + (backend.audioSessionText !== ""
                           ? " · audio " + backend.audioSessionText : "")
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
