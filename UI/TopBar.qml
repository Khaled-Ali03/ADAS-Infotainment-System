import QtQuick
import QtQuick.Layouts
import ADAS_HMI
import "../Components"

// Fixed, chromeless status bar across the top of both views. Laid out around a
// symmetric reference band (bandLeft = the map-state left-screen border,
// bandRight = its mirror), giving three zones:
//   LEFT   : home | gear selector | battery
//   CENTRE : lock | profile (screen centre)
//   RIGHT  : clock | temperature | theme toggle
Item {
    id: topBar
    implicitHeight: 46

    property bool atHome: true
    signal goHome()

    readonly property int tempF: Math.round(SystemHandler.ambientTempC * 9 / 5 + 32)

    readonly property real bandLeft: 341   // map-state left-screen width
    readonly property real bandRight: width - bandLeft
    // Centre of the theme toggle; clock + temperature cluster to its left.
    readonly property real themeCenter: width - 31

    // ---- Home button ----
    Item {
        id: homeBtn
        anchors.left: parent.left
        anchors.leftMargin: 14
        anchors.verticalCenter: parent.verticalCenter
        width: 34; height: 34
        Rectangle { anchors.fill: parent; radius: 17; color: homeTap.pressed ? Theme.surfaceAlt : "transparent"; opacity: 0.5 }
        Image {
            anchors.centerIn: parent
            source: "qrc:/icons/resources/home.svg"
            sourceSize.width: 26; sourceSize.height: 26
            width: 24; height: 24
            fillMode: Image.PreserveAspectFit
            smooth: true
        }
        MouseArea { id: homeTap; anchors.fill: parent; onClicked: topBar.goHome() }
    }

    // ---- Gear selector (P always grey; active R/N/D in accent) ----
    Row {
        id: gears
        anchors.verticalCenter: parent.verticalCenter
        x: topBar.bandLeft / 2 - width / 2
        spacing: 12
        Repeater {
            model: ["P", "R", "N", "D"]
            delegate: Text {
                required property string modelData
                readonly property bool current: SystemHandler.gear === modelData
                anchors.verticalCenter: parent.verticalCenter
                text: modelData
                font.pixelSize: current ? 20 : 17
                font.bold: current
                color: modelData === "P" ? Theme.textSecondary
                     : current ? Theme.accent : Theme.textFaint
                MouseArea {
                    anchors.fill: parent; anchors.margins: -5
                    onClicked: SystemHandler.gear = parent.modelData
                }
            }
        }
    }

    // ---- Battery: shell + adaptive fill + nub ----
    RowLayout {
        id: battery
        anchors.verticalCenter: parent.verticalCenter
        x: topBar.bandLeft - width - 6
        spacing: 5
        Text { visible: SystemHandler.charging; text: "⚡"; color: Theme.accent; font.pixelSize: 14 }
        Rectangle {
            width: 28; height: 14; radius: 3
            color: "transparent"
            border.color: Theme.textSecondary; border.width: 1.5
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left; anchors.leftMargin: 2
                width: Math.max(1, (parent.width - 4) * SystemHandler.batteryPercent / 100)
                height: parent.height - 4
                radius: 1
                color: SystemHandler.batteryPercent <= 15 ? Theme.danger : "#2DCE89"
            }
        }
        Rectangle { width: 2.5; height: 6; radius: 1; color: Theme.textSecondary } // nub
        Text { text: SystemHandler.batteryPercent + "%"; color: Theme.textPrimary; font.pixelSize: 14; font.bold: true }
    }

    // ---- Lock (mirrors the car lock) ----
    LockIcon {
        id: lock
        anchors.verticalCenter: parent.verticalCenter
        x: topBar.bandLeft + 70
        locked: SystemHandler.locked
        color: Theme.gold
        pixelSize: 22
        MouseArea { anchors.fill: parent; anchors.margins: -6; onClicked: SystemHandler.toggleLock() }
    }

    // ---- Profile, centred on the screen ----
    RowLayout {
        id: profile
        anchors.verticalCenter: parent.verticalCenter
        x: (topBar.width - width) / 2
        spacing: 6
        Text { text: "👤"; color: Theme.textSecondary; font.pixelSize: 15 }
        Text {
            text: ProfileManager.currentProfile !== "" ? ProfileManager.currentProfile : "Easy Entry"
            color: Theme.textPrimary; font.pixelSize: 15; font.bold: true
        }
    }

    // ---- Clock + temperature ----
    Text {
        id: clock
        anchors.verticalCenter: parent.verticalCenter
        x: topBar.themeCenter - 180 - width / 2
        text: SystemHandler.clockText
        color: Theme.textPrimary; font.pixelSize: 15; font.bold: true
    }

    Text {
        id: temp
        anchors.verticalCenter: parent.verticalCenter
        x: topBar.themeCenter - 90 - width / 2
        text: topBar.tempF + "°F"
        color: Theme.textPrimary; font.pixelSize: 15
    }

    // ---- Theme toggle ----
    Item {
        id: themeBtn
        anchors.right: parent.right
        anchors.rightMargin: 14
        anchors.verticalCenter: parent.verticalCenter
        width: 34; height: 34
        Rectangle { anchors.fill: parent; radius: 17; color: themeTap.pressed ? Theme.surfaceAlt : "transparent" }
        Text { anchors.centerIn: parent; text: Theme.dark ? "☀" : "☾"; color: Theme.textPrimary; font.pixelSize: 18 }
        MouseArea { id: themeTap; anchors.fill: parent; onClicked: Theme.toggle() }
    }
}
