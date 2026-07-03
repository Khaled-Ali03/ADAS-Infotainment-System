import QtQuick
import QtQuick.Controls
import QtQuick.VirtualKeyboard
import ADAS_HMI
import "UI"
import "Components"

// Hardware-locked 1024x600 layout engine. One persistent scene with two states:
//   home -> the vehicle stage fills the screen; nav card + media are visible.
//   map  -> the stage shrinks into the left lane and the RightScreen map
//           slides in; home chrome fades out.
// Top/Bottom bars are fixed in both. The 3D car (inside LeftScreen) is
// gear-driven: Park = free-rotating hero, Drive = forward + lanes.
ApplicationWindow {
    id: mainWindow
    visible: true
    title: "ADAS HMI"
    color: Theme.window

    width: 1024
    height: 600
    minimumWidth: 1024
    maximumWidth: 1024
    minimumHeight: 600
    maximumHeight: 600

    Item {
        id: rootContainer
        anchors.fill: parent
        state: "home"

        TopBar {
            id: topBar
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            z: 10
            atHome: rootContainer.state === "home"
            onGoHome: rootContainer.state = "home"
        }

        BottomBar {
            id: bottomBar
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 50
            z: 10
        }

        // Persistent vehicle / ADAS stage.
        LeftScreen {
            id: leftScreen
            anchors.top: topBar.bottom
            anchors.bottom: bottomBar.top
            anchors.left: parent.left
            width: rootContainer.width   // overridden by state
            z: 1
        }

        // Map view (slides in for navigation).
        RightScreen {
            id: rightScreen
            anchors.top: topBar.bottom
            anchors.bottom: bottomBar.top
            anchors.left: leftScreen.right
            anchors.right: parent.right
            opacity: 0
            visible: opacity > 0
            z: 1
        }

        // Home chrome overlay (fades out for the map).
        Item {
            id: homeOverlay
            anchors.top: topBar.bottom
            anchors.bottom: bottomBar.top
            anchors.left: parent.left
            anchors.right: parent.right
            opacity: 1
            visible: opacity > 0
            z: 5

            NavCard {
                id: navCard
                width: 330
                anchors.right: parent.right
                anchors.rightMargin: 40
                anchors.top: parent.top
                anchors.topMargin: 12
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 12
                onLaunchNavigation: rootContainer.state = "map"
            }

            MediaPlayer {
                id: mediaPlayer
                height: 76
                anchors.left: parent.left
                anchors.leftMargin: 60
                anchors.right: navCard.left
                anchors.rightMargin: 40
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 24
            }
        }

        // Home leaves a ~380 px right column for the nav card so the car and
        // lanes never overlap it; map collapses the stage to the 341 px lane.
        states: [
            State {
                name: "home"
                PropertyChanges { target: leftScreen; width: rootContainer.width - 380 }
                PropertyChanges { target: homeOverlay; opacity: 1 }
                PropertyChanges { target: rightScreen; opacity: 0 }
            },
            State {
                name: "map"
                PropertyChanges { target: leftScreen; width: 341 }
                PropertyChanges { target: homeOverlay; opacity: 0 }
                PropertyChanges { target: rightScreen; opacity: 1 }
            }
        ]

        transitions: [
            Transition {
                NumberAnimation {
                    properties: "width,opacity"
                    duration: 650
                    easing.type: Easing.InOutCubic
                }
            }
        ]
    }

    InputPanel {
        id: inputPanel
        z: 99
        y: mainWindow.height
        anchors.left: parent.left
        anchors.right: parent.right
        states: State {
            name: "visible"
            when: inputPanel.active
            PropertyChanges { target: inputPanel; y: mainWindow.height - inputPanel.height }
        }
        transitions: Transition {
            from: ""; to: "visible"
            NumberAnimation { properties: "y"; duration: 200; easing.type: Easing.OutCubic }
        }
    }
}
