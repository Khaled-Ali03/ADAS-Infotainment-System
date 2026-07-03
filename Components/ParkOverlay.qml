import QtQuick
import ADAS_HMI

// Park-mode body-state callouts. Frunk, Trunk and the lock are anchored to
// points projected from Vehicle3D, so the connector lines + labels track the
// car as it is rotated; labels fade out as the car is tilted (`fade`). The car
// model is a single mesh, so these are clickable status labels, not 3D motion.
Item {
    id: po

    // Anchor points in this item's pixel space (from Vehicle3D). Off-screen
    // sentinel is (-9999,-9999).
    property point frunkPoint: Qt.point(-9999, -9999)
    property point trunkPoint: Qt.point(-9999, -9999)
    property point roofPoint: Qt.point(-9999, -9999)
    property real fade: 1.0

    // Each label sits directly above its anchor, so the connector is a short
    // vertical line that can't bend or shoot off-screen while rotating.
    readonly property point frunkLabel: Qt.point(frunkPoint.x, frunkPoint.y - 60)
    readonly property point trunkLabel: Qt.point(trunkPoint.x, trunkPoint.y - 60)
    readonly property point lockLabel:  Qt.point(roofPoint.x,  roofPoint.y - 82)

    function onScreen(p) { return p.x > -9000 && p.y > -9000; }

    Item {
        anchors.fill: parent
        opacity: po.fade

        // Connector lines, part -> just below the label (never touching it).
        Canvas {
            id: link
            anchors.fill: parent
            onPaint: {
                var ctx = getContext("2d"); ctx.reset();
                ctx.strokeStyle = Qt.rgba(0.5, 0.5, 0.5, 0.6);
                ctx.lineWidth = 1.2;
                function seg(part, labelPt, gap) {
                    if (!po.onScreen(part)) return;
                    var endY = labelPt.y + gap;
                    if (endY >= part.y - 1) return;   // label not above the part
                    ctx.beginPath(); ctx.moveTo(part.x, part.y); ctx.lineTo(part.x, endY); ctx.stroke();
                }
                seg(po.frunkPoint, po.frunkLabel, 18);
                seg(po.trunkPoint, po.trunkLabel, 18);
                seg(po.roofPoint,  po.lockLabel,  24);
            }
            Connections {
                target: po
                function onFrunkPointChanged() { link.requestPaint() }
                function onTrunkPointChanged() { link.requestPaint() }
                function onRoofPointChanged() { link.requestPaint() }
            }
        }

        // Status callout (bold state over grey name). An Item wraps the Column
        // so the anchored MouseArea isn't laid out by it.
        component Callout : Item {
            id: callout
            property string title: ""
            property bool open: false
            property point at: Qt.point(-9999, -9999)
            signal toggled()
            visible: po.onScreen(at)
            implicitWidth: col.implicitWidth
            implicitHeight: col.implicitHeight
            x: at.x - implicitWidth / 2
            y: at.y - implicitHeight / 2
            Column {
                id: col
                spacing: 1
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: callout.open ? "Open" : "Closed"
                    color: callout.open ? Theme.accent : Theme.textPrimary
                    font.pixelSize: 16; font.bold: true
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: callout.title; color: Theme.textSecondary; font.pixelSize: 12
                }
            }
            MouseArea { anchors.fill: parent; anchors.margins: -8; onClicked: callout.toggled() }
        }

        Callout {
            title: "Frunk"; at: po.frunkLabel
            open: SystemHandler.frunkOpen; onToggled: SystemHandler.toggleFrunk()
        }
        Callout {
            title: "Trunk"; at: po.trunkLabel
            open: SystemHandler.trunkOpen; onToggled: SystemHandler.toggleTrunk()
        }

        // Lock at the roof anchor (stands in for the doors).
        Item {
            id: lockCallout
            visible: po.onScreen(po.roofPoint)
            implicitWidth: lockCol.implicitWidth
            implicitHeight: lockCol.implicitHeight
            x: po.lockLabel.x - implicitWidth / 2
            y: po.lockLabel.y - implicitHeight / 2
            Column {
                id: lockCol
                spacing: 4
                LockIcon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    locked: SystemHandler.locked
                    pixelSize: 30
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: SystemHandler.locked ? "Locked" : "Unlocked"
                    color: Theme.textSecondary; font.pixelSize: 11; font.bold: true
                }
            }
            MouseArea { anchors.fill: parent; anchors.margins: -6; onClicked: SystemHandler.toggleLock() }
        }
    }
}
