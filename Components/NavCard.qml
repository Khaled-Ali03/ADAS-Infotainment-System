import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ADAS_HMI

// Home-view navigation column (right side). The search bar is pinned at the
// vertical centre and opens the full map; with an active route the mini-map
// appears above it and the scrollable turn list below it, with the summary +
// Cancel Trip pinned at the end.
Item {
    id: navCard

    signal launchNavigation()

    readonly property bool hasRoute: NavController.hasRoute
    readonly property bool hasDest: NavController.hasPin || NavController.hasRoute

    // ---- Mini route preview (above the bar) ----
    MiniMap {
        id: miniMap
        width: 190; height: 190
        visible: navCard.hasRoute
        anchors.horizontalCenter: searchBar.horizontalCenter
        anchors.bottom: searchBar.top
        anchors.bottomMargin: 14
    }

    // ---- Fixed search bar (vertical centre) ----
    Rectangle {
        id: searchBar
        height: 50
        radius: 25
        anchors.right: parent.right
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        color: Theme.surface
        border.color: Theme.border
        border.width: 1

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16; anchors.rightMargin: 16
            spacing: 10
            Text { text: "🔍"; color: Theme.textSecondary; font.pixelSize: 16 }
            Text {
                Layout.fillWidth: true
                text: navCard.hasDest && NavController.destName !== ""
                      ? NavController.destName : "Where to?"
                color: navCard.hasDest ? Theme.textPrimary : Theme.textFaint
                font.pixelSize: 15
                font.bold: navCard.hasDest
                elide: Text.ElideRight
            }
        }
        MouseArea { anchors.fill: parent; onClicked: navCard.launchNavigation() }
    }

    // ---- Directions panel (below the bar, down to the bottom) ----
    Rectangle {
        id: directionsPanel
        visible: navCard.hasRoute
        anchors.left: searchBar.left
        anchors.right: searchBar.right
        anchors.top: searchBar.bottom
        anchors.topMargin: 12
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 8
        radius: 16
        color: Theme.surface
        border.color: Theme.border
        border.width: 1

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 14
            spacing: 8

            Text {
                text: NavController.destAddress
                visible: text !== ""
                color: Theme.textSecondary; font.pixelSize: 12
                elide: Text.ElideRight; Layout.fillWidth: true
            }

            // Scrollable turn-by-turn list (shared NavController data).
            ListView {
                id: turnList
                Layout.fillWidth: true; Layout.fillHeight: true
                clip: true; spacing: 2
                model: NavController.directions
                ScrollBar.vertical: ScrollBar { policy: turnList.contentHeight > turnList.height ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded }
                delegate: RowLayout {
                    required property var modelData
                    width: turnList.width; height: 48; spacing: 10
                    Text { text: modelData.dirGlyph; color: Theme.accent; font.pixelSize: 22; Layout.preferredWidth: 26; horizontalAlignment: Text.AlignHCenter }
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 1
                        Text { text: modelData.distanceText; color: Theme.textPrimary; font.pixelSize: 13; font.bold: true }
                        Text { text: modelData.instruction; color: Theme.textSecondary; font.pixelSize: 11; elide: Text.ElideRight; Layout.fillWidth: true; maximumLineCount: 1 }
                    }
                }
            }

            // Pinned footer: separator + Cancel Trip + summary (always visible).
            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

            Button {
                Layout.fillWidth: true; Layout.preferredHeight: 38
                text: "CANCEL TRIP"
                background: Rectangle { color: Theme.surfaceAlt; radius: 8 }
                contentItem: Text { text: parent.text; color: Theme.danger; font.pixelSize: 13; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                onClicked: { NavController.clearRoute(); NavController.clearPin(); }
            }

            RowLayout {
                Layout.fillWidth: true
                Text { text: NavController.routeDistanceText; color: Theme.textPrimary; font.pixelSize: 12; font.bold: true; Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter }
                Text { text: NavController.routeDurationText; color: Theme.accent; font.pixelSize: 12; font.bold: true; Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter }
                Text { text: NavController.routeEtaText; color: Theme.textPrimary; font.pixelSize: 12; font.bold: true; Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter }
            }
        }
    }
}
