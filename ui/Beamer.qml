// Live display state from `beamer watch`, and actions via the beamer CLI.

import QtQuick
import Quickshell
import Quickshell.Io

Scope {
    id: svc

    // see `beamer status --json` for the shape; null while the daemon is unreachable
    property var state: null
    readonly property string bin: Quickshell.env("BEAMER_BIN") || "beamer"

    readonly property var internal: state ? state.internal : null
    readonly property var externals: state ? state.externals : []
    readonly property string mode: state ? state.mode : ""
    // epoch ms when an unconfirmed change gets undone, 0 if none is pending
    readonly property real revertAt: state && state.revertAt ? state.revertAt : 0

    function setMode(mode: string): void {
        if (svc.state) svc.state = Object.assign({}, svc.state, { mode: mode }); // optimistic
        Quickshell.execDetached([svc.bin, mode]);
    }

    // Run a command after the popup has had time to unmap. Hyprland crashes
    // if the monitor under a focused layer surface is disabled, so modes that
    // switch a screen off must only run once the popup is gone.
    function runAfterClose(args: var): void {
        deferred.args = args;
        deferred.restart();
    }

    function keep(): void {
        Quickshell.execDetached([svc.bin, "keep"]);
    }

    function place(output: string, side: string, offset: int): void {
        Quickshell.execDetached([svc.bin, "extend", `${output}=${side}:${offset}`]);
    }

    Process {
        id: watcher
        command: [svc.bin, "watch"]
        running: true
        stdout: SplitParser {
            onRead: line => {
                try {
                    svc.state = JSON.parse(line);
                } catch (e) {
                    console.warn("beamer: bad state line", line);
                }
            }
        }
        onExited: {
            svc.state = null;
            retry.start();
        }
    }

    Timer {
        id: deferred
        property var args: []
        interval: 250
        onTriggered: Quickshell.execDetached([svc.bin].concat(args))
    }

    // the daemon may still be starting, or was restarted
    Timer {
        id: retry
        interval: 1000
        onTriggered: watcher.running = true
    }
}
