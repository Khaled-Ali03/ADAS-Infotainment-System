import QtQuick
import QtQuick.Layouts
import QtQuick.Effects
import ADAS_HMI

// Drive-mode instrument cluster (top strip of the ADAS view):
//   LEFT   - autopilot steering wheel (tilts with the de-noised steering)
//   CENTRE - current speed + cruise-control stepper
//   RIGHT  - posted speed-limit sign
// Speed / limit / steering all come from the real telemetry.
Item {
    id: cluster
    implicitHeight: 100

    readonly property int limitDisplay: SystemHandler.useMph
        ? Math.round(SystemHandler.speedLimitKmh * 0.621371)
        : SystemHandler.speedLimitKmh

    // CARLA steer is -1..1 (full lock); a real wheel turns ~1.5 rotations each
    // way, so full lock -> 540°. Uses steerSmoothed so it holds turns instead
    // of wiggling with the noisy capture.
    readonly property real wheelAngle: SystemHandler.steerSmoothed * 540

    // Renders a single-colour SVG recoloured to `color`.
    component MonoIcon : Item {
        id: mono
        property alias source: monoImg.source
        property color color: "black"
        property int pixelSize: 24
        implicitWidth: pixelSize
        implicitHeight: pixelSize

        Image {
            id: monoImg
            anchors.fill: parent
            fillMode: Image.PreserveAspectFit
            visible: false
            sourceSize.width: mono.pixelSize * 2
            sourceSize.height: mono.pixelSize * 2
        }
        MultiEffect {
            source: monoImg
            anchors.fill: monoImg
            colorization: 1.0
            colorizationColor: mono.color
        }
    }

    // ---- LEFT: steering wheel ----
    MonoIcon {
        id: wheel
        anchors.left: parent.left
        anchors.leftMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        source: "qrc:/icons/resources/steering_wheel.svg"
        color: Theme.accent
        pixelSize: 46
        rotation: cluster.wheelAngle
        Behavior on rotation { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
    }

    // ---- CENTRE: current speed ----
    Text {
        id: speedText
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: -16
        text: SystemHandler.speedDisplay
        color: Theme.textPrimary
        font.pixelSize: 60; font.bold: true
    }
    Text {
        anchors.left: speedText.right
        anchors.leftMargin: 6
        anchors.baseline: speedText.baseline
        text: SystemHandler.speedUnitText
        color: Theme.textSecondary
        font.pixelSize: 16
    }

    // ---- CENTRE: cruise stepper (tap the value to enable/disable) ----
    RowLayout {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: speedText.bottom
        anchors.topMargin: 4
        spacing: 8

        component StepButton : Rectangle {
            property string glyph: ""
            signal activated()
            width: 28; height: 28; radius: 14
            color: tap.pressed ? Theme.surfaceMuted : Theme.surfaceAlt
            Text { anchors.centerIn: parent; text: glyph; color: Theme.textPrimary; font.pixelSize: 18 }
            MouseArea { id: tap; anchors.fill: parent; onClicked: parent.activated() }
        }

        StepButton { glyph: "−"; onActivated: SystemHandler.cruiseDown() }

        Rectangle {
            Layout.preferredWidth: 50; Layout.preferredHeight: 28
            radius: 14
            color: SystemHandler.cruiseEnabled ? Theme.accent
                 : (setTap.pressed ? Theme.surfaceMuted : Theme.surfaceAlt)
            Text {
                anchors.centerIn: parent
                text: SystemHandler.cruiseDisplay
                color: SystemHandler.cruiseEnabled ? "white" : Theme.textPrimary
                font.pixelSize: 18; font.bold: true
            }
            MouseArea { id: setTap; anchors.fill: parent; onClicked: SystemHandler.toggleCruise() }
        }

        StepButton { glyph: "+"; onActivated: SystemHandler.cruiseUp() }
    }

    // ---- RIGHT: posted speed-limit sign ----
    Rectangle {
        visible: SystemHandler.speedLimitKmh > 0
        anchors.right: parent.right
        anchors.rightMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        width: 48; height: 48; radius: 24
        color: "white"; border.color: "#E0312E"; border.width: 4
        Text {
            anchors.centerIn: parent
            text: cluster.limitDisplay
            color: "black"; font.pixelSize: 18; font.bold: true
        }
    }
}
