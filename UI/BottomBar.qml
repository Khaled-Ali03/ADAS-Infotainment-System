import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ADAS_HMI

// Persistent strip along the bottom edge: dual-zone HVAC + settings access.
Rectangle {
    id: bottomBar
    color: Theme.dark ? "#111111" : "#1A1A1A"   // always a dark control strip

    // +/- climate control for one zone. Signal names avoid "up"/"down"
    // because AbstractButton already has a `down` property that shadows them.
    component ClimateZone : RowLayout {
        property string label: "Driver"
        property real temp: 21.0
        signal tempRaised()
        signal tempLowered()
        spacing: 8

        Text {
            text: label
            color: "#888888"
            font.pixelSize: 12
            Layout.alignment: Qt.AlignVCenter
        }
        RoundButton {
            text: "−" // minus
            implicitWidth: 34; implicitHeight: 34
            onClicked: tempLowered()
            background: Rectangle { radius: 17; color: "#2A2A2A" }
            contentItem: Text { text: parent.text; color: "white"; font.pixelSize: 20; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
        }
        Text {
            text: temp.toFixed(1) + "°"
            color: "white"
            font.pixelSize: 18
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
            Layout.preferredWidth: 54
            Layout.alignment: Qt.AlignVCenter
        }
        RoundButton {
            text: "+"
            implicitWidth: 34; implicitHeight: 34
            onClicked: tempRaised()
            background: Rectangle { radius: 17; color: "#2A2A2A" }
            contentItem: Text { text: parent.text; color: "white"; font.pixelSize: 20; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        spacing: 0

        ClimateZone {
            label: "Driver"
            temp: HVACHandler.driverTempC
            onTempRaised: HVACHandler.driverTempUp()
            onTempLowered: HVACHandler.driverTempDown()
        }

        Item { Layout.fillWidth: true }

        // Fan speed indicator (read-only for now).
        RowLayout {
            spacing: 6
            Text { text: "❄"; color: "#22AFFB"; font.pixelSize: 16; Layout.alignment: Qt.AlignVCenter }
            Text { text: "Fan " + HVACHandler.fanSpeed; color: "#AAAAAA"; font.pixelSize: 14; Layout.alignment: Qt.AlignVCenter }
        }

        Item { Layout.fillWidth: true }

        ClimateZone {
            label: "Passenger"
            temp: HVACHandler.passengerTempC
            onTempRaised: HVACHandler.passengerTempUp()
            onTempLowered: HVACHandler.passengerTempDown()
        }

        Item { Layout.preferredWidth: 16 }

        // Settings popup.
        RoundButton {
            text: "⚙"
            implicitWidth: 36; implicitHeight: 36
            onClicked: settingsPopup.open()
            background: Rectangle { radius: 18; color: "#2A2A2A" }
            contentItem: Text { text: parent.text; color: "white"; font.pixelSize: 20; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
            Layout.alignment: Qt.AlignVCenter
        }
    }

    Popup {
        id: settingsPopup
        parent: Overlay.overlay
        anchors.centerIn: Overlay.overlay
        width: 360
        height: 220
        modal: true
        background: Rectangle { color: "#1E1E1E"; radius: 16; border.color: "#333333" }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 12

            Text { text: "Settings"; color: "white"; font.pixelSize: 22; font.bold: true }

            // Speed unit toggle (drives the cluster + cruise display).
            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                Text { text: "Speed unit"; color: "#CCCCCC"; font.pixelSize: 15; Layout.fillWidth: true }
                Repeater {
                    model: [ { label: "km/h", mph: false }, { label: "mph", mph: true } ]
                    delegate: Button {
                        required property var modelData
                        text: modelData.label
                        implicitHeight: 32
                        background: Rectangle {
                            radius: 8
                            color: SystemHandler.useMph === modelData.mph ? "#22AFFB" : "#2A2A2A"
                        }
                        contentItem: Text {
                            text: parent.text
                            color: SystemHandler.useMph === modelData.mph ? "black" : "#CCCCCC"
                            font.pixelSize: 14; font.bold: true
                            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                            leftPadding: 12; rightPadding: 12
                        }
                        onClicked: SystemHandler.useMph = modelData.mph
                    }
                }
            }

            Text {
                text: "CARLA controls (weather, traffic) and vehicle settings\nwill live here in a later pass."
                color: "#999999"; font.pixelSize: 14; wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Item { Layout.fillHeight: true }
            Button {
                text: "Close"
                Layout.alignment: Qt.AlignRight
                onClicked: settingsPopup.close()
            }
        }
    }
}
