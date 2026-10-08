// Centered display popup: mode switcher + drag-to-arrange canvas.

import QtQuick
import QtQuick.Layouts
import Quickshell
import Quickshell.Hyprland
import Quickshell.Wayland

PanelWindow {
    id: win

    required property var beamer
    signal closeRequested

    readonly property Theme theme: Theme {}
    readonly property bool hasExternal: win.beamer.externals.length > 0
    readonly property bool confirming: win.beamer.revertAt > 0
    property real now: Date.now()
    readonly property int secondsLeft: Math.max(0, Math.ceil((win.beamer.revertAt - win.now) / 1000))

    readonly property var modes: [
        { id: "mirror", label: "Mirror", hint: "Same picture" },
        { id: "extend", label: "Extend", hint: "Second desktop" },
        { id: "external", label: "Projector only", hint: "Laptop off" },
        { id: "internal", label: "Laptop only", hint: "Projector off" },
    ]

    // the focused monitor on Hyprland, otherwise the laptop panel if it is on
    screen: {
        const screens = Quickshell.screens;
        const lap = win.beamer.internal;
        const ext = win.beamer.externals.find(e => e.enabled);
        const want = Quickshell.env("HYPRLAND_INSTANCE_SIGNATURE") && Hyprland.focusedMonitor
            ? Hyprland.focusedMonitor.name
            : lap && lap.enabled ? lap.name : ext ? ext.name : "";
        return screens.find(s => s.name === want) ?? screens[0];
    }

    anchors {
        top: true
        bottom: true
        left: true
        right: true
    }
    exclusionMode: ExclusionMode.Ignore
    WlrLayershell.layer: WlrLayer.Overlay
    WlrLayershell.namespace: "beamer"
    WlrLayershell.keyboardFocus: WlrKeyboardFocus.Exclusive
    color: "transparent"

    function pick(index: int): void {
        const m = win.modes[index];
        if (!m || (!win.hasExternal && m.id !== "internal") || win.confirming) return;
        if (m.id === "mirror" || m.id === "extend") {
            win.beamer.setMode(m.id);
            return;
        }
        // these switch a screen off, maybe the one this popup is on: close first.
        // Projector only must be confirmed from the projector, else it reverts.
        win.closeRequested();
        win.beamer.runAfterClose(m.id === "external" ? ["external", "--revert=15"] : [m.id]);
    }

    function keep(): void {
        win.beamer.keep();
        win.closeRequested();
    }

    function revert(): void {
        win.closeRequested();
        win.beamer.runAfterClose(["revert"]);
    }

    Timer {
        running: win.confirming
        repeat: true
        interval: 200
        onTriggered: {
            win.now = Date.now();
            // the daemon reverts at 0 and may switch this screen off: be gone by then
            if (win.beamer.revertAt - win.now < 800) win.closeRequested();
        }
    }

    Rectangle {
        id: scrim
        anchors.fill: parent
        color: win.theme.scrim
        opacity: 0
        Component.onCompleted: opacity = 1
        Behavior on opacity { NumberAnimation { duration: 140 } }

        // click outside the card closes
        MouseArea {
            anchors.fill: parent
            onClicked: if (!win.confirming) win.closeRequested()
        }
    }

    Rectangle {
        id: card
        anchors.centerIn: parent
        width: 600
        height: win.confirming ? 190 : content.implicitHeight + 40
        radius: win.theme.radius
        color: win.theme.card
        border.color: win.theme.cardBorder
        border.width: 1

        scale: 0.94
        opacity: 0
        Component.onCompleted: {
            scale = 1;
            opacity = 1;
        }
        Behavior on scale { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
        Behavior on opacity { NumberAnimation { duration: 140 } }

        MouseArea {
            anchors.fill: parent // swallow clicks so they don't reach the scrim
        }

        Item {
            id: keys
            anchors.fill: parent
            focus: true
            Keys.onPressed: event => {
                const sides = { [Qt.Key_Left]: "left", [Qt.Key_Right]: "right", [Qt.Key_Up]: "above", [Qt.Key_Down]: "below" };
                if (win.confirming) {
                    if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Y) win.keep();
                    else if (event.key === Qt.Key_Escape || event.key === Qt.Key_N) win.revert();
                    else return;
                } else if (event.key === Qt.Key_Escape || event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                    win.closeRequested();
                } else if (event.key >= Qt.Key_1 && event.key <= Qt.Key_4) {
                    win.pick(event.key - Qt.Key_1);
                } else if (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab) {
                    const i = win.modes.findIndex(m => m.id === win.beamer.mode);
                    const step = event.key === Qt.Key_Tab ? 1 : win.modes.length - 1;
                    win.pick((Math.max(i, 0) + step) % win.modes.length);
                } else if (sides[event.key] !== undefined) {
                    arrange.moveFirst(sides[event.key]);
                } else {
                    return;
                }
                event.accepted = true;
            }
        }

        ColumnLayout {
            id: confirm
            visible: win.confirming
            anchors.fill: parent
            anchors.margins: 20
            spacing: 14

            Text {
                text: "Keep this display setup?"
                color: win.theme.text
                font.pixelSize: 20
                font.weight: Font.DemiBold
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: `The laptop screen is off. Switching back in ${win.secondsLeft} s unless you keep it.`
                color: win.theme.textDim
                font.pixelSize: 13
            }
            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                Repeater {
                    model: [
                        { label: "Revert", hint: "Esc", primary: false },
                        { label: "Keep", hint: "Enter", primary: true },
                    ]
                    Rectangle {
                        id: cbtn
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.preferredHeight: 48
                        radius: 12
                        color: modelData.primary ? win.theme.accent : chover.hovered ? win.theme.surfaceHover : win.theme.surface
                        HoverHandler { id: chover; cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: cbtn.modelData.primary ? win.keep() : win.revert() }
                        Text {
                            anchors.centerIn: parent
                            text: `${cbtn.modelData.label}  ·  ${cbtn.modelData.hint}`
                            color: cbtn.modelData.primary ? win.theme.accentText : win.theme.text
                            font.pixelSize: 14
                            font.weight: Font.Medium
                        }
                    }
                }
            }
        }

        ColumnLayout {
            id: content
            visible: !win.confirming
            anchors.fill: parent
            anchors.margins: 20
            spacing: 16

            // header
            ColumnLayout {
                spacing: 2
                Text {
                    text: "Display"
                    color: win.theme.text
                    font.pixelSize: 20
                    font.weight: Font.DemiBold
                }
                Text {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    color: win.theme.textDim
                    font.pixelSize: 13
                    text: {
                        if (!win.beamer.state) return "Waiting for the beamer daemon…";
                        if (!win.hasExternal) return "No external screen connected";
                        return win.beamer.externals.map(e => `${e.description || e.name} · ${e.width}×${e.height}`).join("   ");
                    }
                }
            }

            // mode switcher
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Repeater {
                    model: win.modes

                    Rectangle {
                        id: btn
                        required property var modelData
                        required property int index
                        readonly property bool active: win.beamer.mode === modelData.id && win.hasExternal
                        readonly property bool usable: win.hasExternal || modelData.id === "internal"

                        Layout.fillWidth: true
                        Layout.preferredHeight: 62
                        radius: 12
                        color: active ? win.theme.accent : hover.hovered && usable ? win.theme.surfaceHover : win.theme.surface
                        opacity: usable ? 1 : 0.4
                        Behavior on color { ColorAnimation { duration: 120 } }

                        HoverHandler {
                            id: hover
                            cursorShape: btn.usable ? Qt.PointingHandCursor : Qt.ArrowCursor
                        }
                        TapHandler {
                            onTapped: win.pick(btn.index)
                        }

                        Column {
                            anchors.centerIn: parent
                            spacing: 3
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: btn.modelData.label
                                color: btn.active ? win.theme.accentText : win.theme.text
                                font.pixelSize: 14
                                font.weight: Font.Medium
                            }
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: `${btn.index + 1} · ${btn.modelData.hint}`
                                color: btn.active ? win.theme.accentText : win.theme.textDim
                                font.pixelSize: 11
                                opacity: 0.85
                            }
                        }
                    }
                }
            }

            Arrange {
                id: arrange
                Layout.fillWidth: true
                Layout.preferredHeight: 250
                beamer: win.beamer
                theme: win.theme
            }

            Text {
                Layout.fillWidth: true
                visible: win.beamer.state !== null && !win.beamer.state.mirrorAvailable
                text: "Mirroring on this compositor needs wl-mirror — install it to use Mirror."
                color: win.theme.warn
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }

            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: "Drag the screen to place it  ·  ←↑→↓ move  ·  1–4 / Tab mode  ·  Esc close"
                color: win.theme.textDim
                font.pixelSize: 11
            }
        }
    }
}
