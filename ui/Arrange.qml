// Drag-to-arrange canvas: the laptop sits in the middle, external screens
// snap flush to one of its edges. Dropping a screen switches to Extend.
// All math is in logical pixels (resolution / scale), like the compositor's.

import QtQuick

Item {
    id: arr

    required property var beamer
    required property Theme theme

    readonly property var lap: arr.beamer.internal
    readonly property var exts: arr.beamer.externals
    readonly property bool extending: arr.beamer.mode === "extend"

    readonly property real lw: lap ? lap.logicalWidth : 1536
    readonly property real lh: lap ? lap.logicalHeight : 864
    readonly property real maxEw: exts.reduce((m, e) => Math.max(m, e.logicalWidth), lw * 0.6)
    readonly property real maxEh: exts.reduce((m, e) => Math.max(m, e.logicalHeight), lh * 0.6)
    readonly property real pad: 10
    // pixels on the canvas per logical pixel: room for a screen on every side
    readonly property real k: Math.min((width - 2 * pad) / (lw + 2 * maxEw), (height - 2 * pad) / (lh + 2 * maxEh))
    readonly property real lx: (width - lw * k) / 2
    readonly property real ly: (height - lh * k) / 2

    // top-left of a screen relative to the laptop, in logical pixels
    function origin(side: string, offset: real, ew: real, eh: real): point {
        switch (side) {
        case "left": return Qt.point(-ew, offset);
        case "above": return Qt.point(offset, -eh);
        case "below": return Qt.point(offset, lh);
        default: return Qt.point(lw, offset);
        }
    }

    // Snap to start / center / end of the shared edge, and keep some overlap.
    function snapOffset(off: real, along: real, size: real): int {
        const threshold = Math.max(along, size) * 0.07;
        for (const c of [0, (along - size) / 2, along - size])
            if (Math.abs(off - c) < threshold) return Math.round(c);
        const keep = Math.min(along, size) * 0.15;
        return Math.round(Math.max(-(size - keep), Math.min(along - keep, off)));
    }

    function overlaps(name: string, side: string, offset: real, ew: real, eh: real): bool {
        const a = origin(side, offset, ew, eh);
        return arr.exts.some(e => {
            if (e.name === name) return false;
            const b = origin(e.side, e.offset, e.logicalWidth, e.logicalHeight);
            return a.x < b.x + e.logicalWidth && b.x < a.x + ew && a.y < b.y + e.logicalHeight && b.y < a.y + eh;
        });
    }

    // Where a dragged rect (canvas coordinates) lands, or null if it would overlap.
    function landing(e: var, cx: real, cy: real): var {
        const ex = (cx - lx) / k, ey = (cy - ly) / k;
        const ew = e.logicalWidth, eh = e.logicalHeight;
        // which edge: compare the center offset normalised by how far each edge is
        const nx = (ex + ew / 2 - lw / 2) / ((lw + ew) / 2);
        const ny = (ey + eh / 2 - lh / 2) / ((lh + eh) / 2);
        const horizontal = Math.abs(nx) >= Math.abs(ny);
        const side = horizontal ? (nx > 0 ? "right" : "left") : (ny > 0 ? "below" : "above");
        const offset = horizontal ? snapOffset(ey, lh, eh) : snapOffset(ex, lw, ew);
        return overlaps(e.name, side, offset, ew, eh) ? null : { side: side, offset: offset };
    }

    // keyboard: put the first external screen centered on that side
    function moveFirst(side: string): void {
        const e = arr.exts[0];
        if (!e) return;
        const vertical = side === "left" || side === "right";
        const offset = Math.round(vertical ? (lh - e.logicalHeight) / 2 : (lw - e.logicalWidth) / 2);
        if (!overlaps(e.name, side, offset, e.logicalWidth, e.logicalHeight))
            arr.beamer.place(e.name, side, offset);
    }

    // laptop
    Rectangle {
        x: arr.lx
        y: arr.ly
        width: arr.lw * arr.k
        height: arr.lh * arr.k
        radius: 8
        color: arr.theme.laptop
        border.color: arr.theme.laptopBorder
        border.width: 1

        Column {
            anchors.centerIn: parent
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Laptop"
                color: arr.theme.text
                font.pixelSize: 13
                font.weight: Font.Medium
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: arr.lap ? `${arr.lap.width}×${arr.lap.height}` : ""
                color: arr.theme.textDim
                font.pixelSize: 11
            }
        }
    }

    // external screens
    Repeater {
        model: arr.exts

        Rectangle {
            id: scr
            required property var modelData

            property string side: modelData.side
            property int offset: modelData.offset
            readonly property point home: arr.origin(side, offset, modelData.logicalWidth, modelData.logicalHeight)
            readonly property var preview: drag.active ? arr.landing(modelData, x, y) : null

            width: modelData.logicalWidth * arr.k
            height: modelData.logicalHeight * arr.k
            x: arr.lx + home.x * arr.k
            y: arr.ly + home.y * arr.k
            z: drag.active ? 2 : 1
            radius: 8
            color: Qt.alpha(arr.theme.accent, drag.active ? 0.35 : arr.extending ? 0.25 : 0.12)
            border.color: arr.theme.accent
            border.width: drag.active ? 2 : 1
            opacity: arr.extending || drag.active ? 1 : 0.6

            // outline of where the screen will snap while dragging
            Rectangle {
                parent: arr
                readonly property point at: scr.preview
                    ? arr.origin(scr.preview.side, scr.preview.offset, scr.modelData.logicalWidth, scr.modelData.logicalHeight)
                    : Qt.point(0, 0)
                visible: scr.preview !== null
                x: arr.lx + at.x * arr.k
                y: arr.ly + at.y * arr.k
                width: scr.width
                height: scr.height
                radius: 8
                color: "transparent"
                border.color: arr.theme.accent
                border.width: 1
                opacity: 0.7
            }

            Behavior on x { enabled: !drag.active; NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
            Behavior on y { enabled: !drag.active; NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }

            HoverHandler {
                cursorShape: drag.active ? Qt.ClosedHandCursor : Qt.OpenHandCursor
            }

            DragHandler {
                id: drag
                onActiveChanged: {
                    if (active) return;
                    const land = arr.landing(scr.modelData, scr.x, scr.y);
                    if (land) {
                        scr.side = land.side;
                        scr.offset = land.offset;
                        arr.beamer.place(scr.modelData.name, land.side, land.offset);
                    }
                    // snap back onto the (new) home position
                    scr.x = Qt.binding(() => arr.lx + scr.home.x * arr.k);
                    scr.y = Qt.binding(() => arr.ly + scr.home.y * arr.k);
                }
            }

            Column {
                anchors.centerIn: parent
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: scr.modelData.name
                    color: arr.theme.text
                    font.pixelSize: 12
                    font.weight: Font.Medium
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: arr.extending || drag.active ? `${scr.modelData.width}×${scr.modelData.height}` : "drag to extend"
                    color: arr.theme.textDim
                    font.pixelSize: 10
                }
            }
        }
    }
}
