import QtQuick
import QtQuick.Shapes
import ADAS_HMI
import "../Components"

// The persistent vehicle / ADAS stage: the 3D car plus, in Drive, the lane and
// detection radar drawn over it. Home chrome (nav card, media) lives in
// MainWindow, so this component scales cleanly between the full-width home
// layout and the narrow left lane of the map layout.
// Mode is gear-driven: Park = orbitable hero car, Drive = lanes + detections.
Item {
    id: leftScreen
    clip: true   // keep the car + lanes inside this rectangle

    readonly property bool driving: SystemHandler.driving

    // Ego-lane width as a multiple of the car's on-screen width, so the lane
    // stays in proportion in both layouts.
    property real laneWidthFactor: 1.7

    // Start/stop playback as we enter/leave Drive.
    onDrivingChanged: {
        if (driving) {
            DetectionModel.play();
        } else {
            DetectionModel.pause();
            DetectionModel.seek(0);
        }
    }

    // ---- Lane layer, drawn under the car. Blue = ego lane, grey = adjacent
    //      (only when one is actually detected on that side). ----
    Item {
        id: lanesLayer
        anchors.fill: parent
        visible: leftScreen.driving
        opacity: leftScreen.driving ? 1.0 : 0.0
        Behavior on opacity { NumberAnimation { duration: 300 } }

        Repeater {
            model: LaneModel
            delegate: Shape {
                id: laneShape
                anchors.fill: parent
                required property var points
                required property int laneType
                antialiasing: true
                z: laneType === 2 ? 0 : 1

                ShapePath {
                    strokeColor: laneShape.laneType === 0 ? Theme.accent
                               : laneShape.laneType === 1 ? "#5A6573" : "transparent"
                    strokeWidth: laneShape.laneType === 0 ? 4
                               : laneShape.laneType === 1 ? 2 : 0
                    // Ego-corridor fill: brand blue at ~30% so the current lane
                    // reads clearly over the light background.
                    fillColor: laneShape.laneType === 2 ? Qt.rgba(0.133, 0.686, 0.984, 0.30) : "transparent"
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin

                    PathPolyline {
                        path: {
                            var arr = [];
                            var pts = laneShape.points;
                            var w = laneShape.width, h = laneShape.height;
                            for (var i = 0; i < pts.length; ++i)
                                arr.push(Qt.point(pts[i].x * w, pts[i].y * h));
                            return arr;
                        }
                    }
                }
            }
        }
    }

    // The 3D centerpiece car (park: orbit, drive: forward).
    Vehicle3D {
        id: vehicle
        anchors.fill: parent
        drivingMode: leftScreen.driving
        steer: SystemHandler.steer
        steerSmoothed: SystemHandler.steerSmoothed
        // Narrow map layout -> shrink the car to leave room for side traffic.
        compact: leftScreen.width < 420
    }

    // Size the lane corridor from the car's on-screen width so the lane is
    // always a touch wider than the car in either layout.
    Binding {
        target: LaneModel
        property: "laneHalfWidth"
        when: leftScreen.driving
        value: (vehicle.carScreenWidth > 0 && leftScreen.width > 0)
               ? vehicle.carScreenWidth * leftScreen.laneWidthFactor / 2 / leftScreen.width
               : 0.16
    }

    // ---- Driving radar overlay (pedestrian detections; vehicles render as 3D
    //      models inside Vehicle3D). ----
    Item {
        id: radar
        anchors.fill: parent
        visible: leftScreen.driving
        opacity: leftScreen.driving ? 1.0 : 0.0
        Behavior on opacity { NumberAnimation { duration: 300 } }
        clip: true

        function mapX(cx) { return (cx / DetectionModel.sourceWidth) * width; }
        function mapY(cy) { return (cy / DetectionModel.sourceHeight) * height; }

        // Closer objects render larger, from metric depth (~5..75 m); falls
        // back to the in-frame y heuristic when depth is unknown.
        readonly property real nearMetres: 5.0
        readonly property real farMetres: 75.0
        function depthScale(depthM, cy) {
            if (depthM <= 0.0) {
                let ny = cy / DetectionModel.sourceHeight;
                return 0.55 + ny * 0.9;
            }
            let t = (depthM - radar.nearMetres) / (radar.farMetres - radar.nearMetres);
            t = Math.max(0.0, Math.min(1.0, t));
            return 1.45 - t * 0.9;
        }

        function iconFor(cls) {
            switch (cls) {
            case "pedestrian":    return "qrc:/icons/resources/person.svg";
            case "person":        return "qrc:/icons/resources/person.svg";
            default:              return "";
            }
        }

        // A pedestrian in the ego lane within 20 m is flagged critical.
        readonly property real criticalMetres: 20.0
        function isCritical(cls, cx, cy, depthM) {
            if (cls !== "pedestrian" && cls !== "person") return false;
            let nx = cx / DetectionModel.sourceWidth;
            let inLane = nx > 0.33 && nx < 0.67;
            if (!inLane) return false;
            if (depthM > 0.0) return depthM <= radar.criticalMetres;
            return (cy / DetectionModel.sourceHeight) > 0.5;
        }

        Repeater {
            model: DetectionModel
            delegate: Item {
                id: det
                required property string className
                required property real centerX
                required property real centerY
                required property real confidence
                required property int trackId
                required property real depth

                readonly property bool isPedestrian: className === "person"
                readonly property real scaleF: radar.depthScale(depth, centerY)
                readonly property real baseSize: 34
                readonly property real iconSize: baseSize * scaleF
                readonly property bool critical: radar.isCritical(className, centerX, centerY, depth)
                readonly property string iconSrc: radar.iconFor(className)

                visible: isPedestrian
                enabled: isPedestrian
                width: iconSize
                height: iconSize
                x: radar.mapX(centerX) - width / 2
                y: radar.mapY(centerY) - height / 2
                opacity: isPedestrian ? Math.max(0.4, confidence) : 0
                z: 2

                Behavior on x { NumberAnimation { duration: 120; easing.type: Easing.OutQuad } }
                Behavior on y { NumberAnimation { duration: 120; easing.type: Easing.OutQuad } }
                Behavior on width { NumberAnimation { duration: 120; easing.type: Easing.OutQuad } }
                Behavior on height { NumberAnimation { duration: 120; easing.type: Easing.OutQuad } }
                Behavior on opacity { NumberAnimation { duration: 150 } }

                // Red proximity pulse behind a critical pedestrian.
                Rectangle {
                    anchors.centerIn: parent
                    width: parent.width * 1.6
                    height: parent.height * 1.6
                    radius: width / 2
                    color: "#FF3B30"
                    visible: det.critical
                    SequentialAnimation on opacity {
                        running: det.critical
                        loops: Animation.Infinite
                        NumberAnimation { from: 0.55; to: 0.1; duration: 500; easing.type: Easing.InOutQuad }
                        NumberAnimation { from: 0.1; to: 0.55; duration: 500; easing.type: Easing.InOutQuad }
                    }
                }

                Image {
                    anchors.fill: parent
                    source: det.iconSrc
                    visible: det.iconSrc !== ""
                    fillMode: Image.PreserveAspectFit
                    sourceSize.width: 64
                    sourceSize.height: 64
                    smooth: true
                }

                // Grey fallback dot for classes without an icon.
                Rectangle {
                    anchors.centerIn: parent
                    width: parent.width * 0.5
                    height: width
                    radius: width / 2
                    color: "#7FA8C9"
                    visible: det.iconSrc === ""
                }
            }
        }
    }

    // Park body-state callouts, anchored to points projected by Vehicle3D so
    // they track the car as it is rotated.
    ParkOverlay {
        anchors.fill: parent
        visible: !leftScreen.driving
        opacity: leftScreen.driving ? 0.0 : 1.0
        Behavior on opacity { NumberAnimation { duration: 300 } }
        frunkPoint: vehicle.frunkPoint
        trunkPoint: vehicle.trunkPoint
        roofPoint: vehicle.roofPoint
        fade: vehicle.labelFade
    }

    // Top fade so lanes + traffic dissolve before the speed cluster.
    Rectangle {
        id: topFade
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
        height: Math.min(parent.height * 0.30, 165)
        visible: leftScreen.driving
        property color bg: Theme.window
        gradient: Gradient {
            GradientStop { position: 0.0; color: topFade.bg }
            GradientStop { position: 0.55; color: topFade.bg }
            GradientStop { position: 1.0; color: Qt.rgba(topFade.bg.r, topFade.bg.g, topFade.bg.b, 0) }
        }
    }

    // Drive instrument cluster (speed / autopilot / cruise / speed limit).
    DriveCluster {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        anchors.topMargin: 12
        visible: leftScreen.driving
        opacity: leftScreen.driving ? 1.0 : 0.0
        Behavior on opacity { NumberAnimation { duration: 300 } }
    }
}
