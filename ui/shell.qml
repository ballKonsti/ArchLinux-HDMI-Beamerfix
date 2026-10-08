// beamer display popup — `beamer ui` toggles it over Quickshell IPC.

import QtQuick
import Quickshell
import Quickshell.Io

ShellRoot {
    id: root

    // `beamer ui` starts the shell on the first press, so open right away
    property bool shown: true

    Beamer {
        id: svc
    }

    // an unconfirmed change: reopen (now on the remaining screen) to ask
    Connections {
        target: svc
        function onRevertAtChanged(): void {
            if (svc.revertAt > 0) root.shown = true;
        }
    }

    IpcHandler {
        target: "beamer"

        function toggle(): void {
            root.shown = !root.shown;
        }
        function open(): void {
            root.shown = true;
        }
        function close(): void {
            root.shown = false;
        }
    }

    // nothing exists while closed: no window, no input region
    LazyLoader {
        active: root.shown

        Popup {
            beamer: svc
            onCloseRequested: root.shown = false
        }
    }
}
