import QtQuick
import QtQuick3D
import QtQuick3D.AssetUtils
import ADAS_HMI

// The centerpiece car, rendered from Car.glb with Quick3D.
//   - Park : static hero 3/4 pose, user-draggable to orbit.
//   - Drive: camera locks behind + above, car faces forward, sits low,
//            surrounding traffic + pedestrians render in the same scene.
// Orbit rig: the camera is parented to a pivot at the model centre, so
// rotating the pivot orbits the view. Distance auto-fits from the bounds.
// Animations drive plain properties, never a Node.eulerRotation sub-component
// (Quick3D rejects two Behaviors there: "another interceptor").
Item {
    id: root

    property bool drivingMode: false
    property real steer: 0.0           // -1..1, raw telemetry
    property real steerSmoothed: 0.0   // -1..1, de-noised steering for visuals

    // Park orbit angles (user-draggable). Default = front-left 3/4 hero view.
    property real azimuth: 38
    property real elevation: -12

    // Rig angles actually applied (park = user orbit, drive = chase view).
    // Only animated on a mode change — never during a drag (feels laggy) and
    // never during the initial load.
    property real pivotYaw: drivingMode ? 0 : azimuth
    property real pivotPitch: drivingMode ? -24 : elevation
    Behavior on pivotYaw { enabled: root.transitionsReady && !drag.active; NumberAnimation { duration: 650; easing.type: Easing.InOutCubic } }
    Behavior on pivotPitch { enabled: root.transitionsReady && !drag.active; NumberAnimation { duration: 650; easing.type: Easing.InOutCubic } }

    // Body yaw from the smoothed steering so the car leans into turns like the
    // wheel does. steer*540° wheel / ~15:1 ratio ≈ 36° road-wheel, ~2.2x visual
    // gain -> factor 79, capped at ±30°.
    readonly property real bodyYaw: Math.max(-30, Math.min(30, steerSmoothed * 79))
    // Subtracting makes the car nose the same way as the wheel in the rear view.
    property real carYaw: drivingMode ? 180 - bodyYaw : 0
    Behavior on carYaw { NumberAnimation { duration: 350; easing.type: Easing.OutCubic } }

    // Push the look-target up so the car sits low in the view in drive.
    property real verticalBias: drivingMode ? fitDistance * 0.17 : 0.0
    Behavior on verticalBias { enabled: root.transitionsReady; NumberAnimation { duration: 650; easing.type: Easing.InOutCubic } }

    // Auto-fit results (filled once the model loads).
    property real fitDistance: 1000
    property vector3d modelCenter: Qt.vector3d(0, 0, 0)

    // Traffic placement (metres -> scene units). The ego model's real length
    // calibrates the scale; the compression factors pull far traffic into a
    // readable spread instead of its true sparse layout.
    property real egoLenZ: 1.0                       // ego length in scene units (from fit)
    property real egoHalfX: 0.9                      // ego half-width (from fit)
    property real groundY: 0.0                       // ground plane (ego bounds min-y)
    readonly property real egoRealLenM: 4.6
    readonly property real metresScale: egoLenZ / egoRealLenM
    property real depthCompress: 0.6                 // <1 pulls far traffic nearer
    property real lateralSpread: 1.0                 // scales side-to-side offset

    // Narrow (map) layout: push the camera back so the car shrinks.
    property bool compact: false

    // The ego car's on-screen width in pixels (a world width W at distance d
    // subtends W * focalPx / d). LeftScreen sizes the lane corridor from this.
    readonly property real carScreenWidth: {
        if (!fitted || camDistance <= 0 || view.height <= 0)
            return 0;
        var focalPx = (view.height / 2) / Math.tan(cam.fieldOfView * Math.PI / 180 / 2);
        return (2 * egoHalfX) * focalPx / camDistance;
    }

    // Park callout anchors, model-local. Car.glb: front=+Z, rear=-Z, up=+Y.
    property vector3d frunkLocal: Qt.vector3d(0, 0, 0)
    property vector3d trunkLocal: Qt.vector3d(0, 0, 0)
    property vector3d roofLocal: Qt.vector3d(0, 0, 0)

    // Project a model-local anchor to View3D pixel coords, tracking the live
    // camera/car pose (the dep sum forces re-evaluation when the view moves).
    // Returns an off-screen point when the anchor is behind the camera.
    function projectPark(local) {
        if (!fitted) return Qt.point(-9999, -9999);
        var _dep = pivotYaw + pivotPitch + camDistance + verticalBias + carYaw
                 + view.width + view.height + modelCenter.x + fitDistance;
        var s = carNode.mapPositionToScene(local);
        var v = view.mapFrom3DScene(s);
        if (v.z <= 0) return Qt.point(-9999, -9999);
        return Qt.point(v.x, v.y);
    }
    readonly property point frunkPoint: projectPark(frunkLocal)
    readonly property point trunkPoint: projectPark(trunkLocal)
    readonly property point roofPoint: projectPark(roofLocal)

    // Park callouts fade as the user tilts far from the default hero pitch.
    readonly property real labelFade: {
        var dev = Math.abs(elevation + 12);   // deviation from the default tilt
        return Math.max(0, Math.min(1, 1 - (dev - 25) / 35));
    }
    property real camDistance: fitDistance * (drivingMode ? (compact ? 2.05 : 1.55) : 1.0)
    Behavior on camDistance { enabled: root.transitionsReady; NumberAnimation { duration: 650; easing.type: Easing.InOutCubic } }

    // fitted -> a valid fit happened, safe to reveal the car.
    // transitionsReady -> set shortly AFTER the first valid fit, so bounds
    // updates during load snap and only later mode changes animate (otherwise
    // the car visibly zooms on startup).
    property bool fitted: false
    property bool transitionsReady: false
    Timer { id: settleTimer; interval: 300; onTriggered: root.transitionsReady = true }

    function fitCamera() {
        if (car.status !== RuntimeLoader.Success)
            return;
        var mn = car.bounds.minimum, mx = car.bounds.maximum;
        var sx = mx.x - mn.x, sy = mx.y - mn.y, sz = mx.z - mn.z;
        if (sx <= 0 || sy <= 0 || sz <= 0)
            return;   // bounds not populated yet — don't fit to an empty box
        root.modelCenter = Qt.vector3d((mn.x + mx.x) / 2, (mn.y + mx.y) / 2, (mn.z + mx.z) / 2);
        root.egoLenZ = sz;
        root.egoHalfX = sx / 2;
        root.groundY = mn.y;
        var cx = (mn.x + mx.x) / 2, midY = (mn.y + mx.y) / 2, cz = (mn.z + mx.z) / 2;
        root.frunkLocal = Qt.vector3d(cx, midY, mx.z);   // front (+Z)
        root.trunkLocal = Qt.vector3d(cx, midY, mn.z);   // rear (-Z)
        root.roofLocal  = Qt.vector3d(cx, mx.y, cz);     // roof top centre
        var radius = 0.5 * Math.max(sx, Math.max(sy, sz));
        var fov = cam.fieldOfView * Math.PI / 180;
        root.fitDistance = radius / Math.tan(fov / 2) * 1.7;
        root.fitted = true;
        if (!root.transitionsReady && !settleTimer.running)
            settleTimer.start();
    }

    // One piece of surrounding traffic. SUV.glb (flat grey, no colour identity)
    // covers all four-wheelers; two-wheelers get their own model. Auto-scaled
    // to its real-world length so a truck reads bigger than a car.
    component TrafficVehicle : Node {
        id: v
        property string className: "car"
        property real metresScale: 1.0   // scene units per metre (from the ego fit)
        property bool active: true       // load the model only for a vehicle slot

        eulerRotation.y: 180             // face away (same-direction traffic)

        readonly property real realLenM: {
            switch (className) {
            case "truck":      return 7.0;
            case "bus":        return 10.0;
            case "motorcycle": return 2.1;
            case "bicycle":    return 1.8;
            default:           return 4.6; // car
            }
        }
        readonly property url modelSrc: {
            switch (className) {
            case "motorcycle": return VehicleModelBase + "Motorcycle.glb";
            case "bicycle":    return VehicleModelBase + "Bicycle.glb";
            default:           return VehicleModelBase + "SUV.glb"; // car / truck / bus
            }
        }

        Node {
            id: vFit
            RuntimeLoader {
                id: vLoader
                source: v.active ? v.modelSrc : ""
                onStatusChanged: v.refit()
                onBoundsChanged: v.refit()
            }
        }

        // Normalise the model to its real length using the longer horizontal
        // extent, and drop it onto the ground plane by its own min-y.
        function refit() {
            if (vLoader.status !== RuntimeLoader.Success)
                return;
            var mn = vLoader.bounds.minimum, mx = vLoader.bounds.maximum;
            var lenH = Math.max(mx.x - mn.x, mx.z - mn.z);
            if (lenH <= 0)
                return;
            var s = (v.realLenM * v.metresScale) / lenH;
            vFit.scale = Qt.vector3d(s, s, s);
            vFit.position = Qt.vector3d(0, -mn.y * s, 0);
        }
    }

    // One crossing pedestrian from Man.glb, scaled to ~1.75 m and turned to
    // face the way it is walking.
    component Pedestrian3D : Node {
        id: p
        property real metresScale: 1.0
        property bool facingRight: true  // walking screen-right (+x) vs screen-left
        property bool active: true

        // Man.glb faces +Z; ±90 turns it to walk across the scene.
        eulerRotation.y: facingRight ? 90 : -90
        Behavior on eulerRotation.y { NumberAnimation { duration: 200 } }

        readonly property real realHeightM: 1.75

        Node {
            id: pFit
            RuntimeLoader {
                id: pLoader
                source: p.active ? PedestrianModelUrl : ""
                onStatusChanged: p.refit()
                onBoundsChanged: p.refit()
            }
        }

        function refit() {
            if (pLoader.status !== RuntimeLoader.Success)
                return;
            var mn = pLoader.bounds.minimum, mx = pLoader.bounds.maximum;
            var hgt = mx.y - mn.y;
            if (hgt <= 0)
                return;
            var s = (p.realHeightM * p.metresScale) / hgt;
            pFit.scale = Qt.vector3d(s, s, s);
            pFit.position = Qt.vector3d(0, -mn.y * s, 0);
        }
    }

    View3D {
        id: view
        anchors.fill: parent
        camera: cam
        opacity: root.fitted ? 1.0 : 0.0          // stay blank until framed
        Behavior on opacity { NumberAnimation { duration: 250 } }

        environment: SceneEnvironment {
            backgroundMode: SceneEnvironment.Transparent
            antialiasingMode: SceneEnvironment.MSAA
            antialiasingQuality: SceneEnvironment.High
        }

        Node {
            id: pivot
            position: Qt.vector3d(root.modelCenter.x,
                                  root.modelCenter.y + root.verticalBias,
                                  root.modelCenter.z)
            eulerRotation.x: root.pivotPitch
            eulerRotation.y: root.pivotYaw

            PerspectiveCamera {
                id: cam
                z: root.camDistance
                fieldOfView: 35
                clipNear: 1
                clipFar: root.fitDistance * 12
            }
        }

        DirectionalLight { eulerRotation.x: -35; eulerRotation.y: -120; brightness: 1.1 }
        DirectionalLight { eulerRotation.x: -15; eulerRotation.y: 100; brightness: 0.6 }

        Node {
            id: carNode
            eulerRotation.y: root.carYaw
            RuntimeLoader {
                id: car
                source: CarModelUrl
                onStatusChanged: root.fitCamera()
                onBoundsChanged: root.fitCamera()
            }
        }

        // Surrounding traffic + pedestrians (Drive only): one model per tracked
        // object, placed ahead of the ego from its metric depth + lateral
        // offset (pinhole, FOV 90 -> fx = width/2).
        Repeater3D {
            model: root.fitted && root.drivingMode ? DetectionModel : null

            delegate: Node {
                id: slot
                required property string className
                required property real centerX
                required property real depth
                required property real heading
                required property int trackId

                readonly property bool isVehicle:
                    className === "vehicle" || className === "car" || className === "truck"
                    || className === "bus" || className === "motorcycle" || className === "bicycle"
                readonly property bool isPedestrian:
                    className === "pedestrian" || className === "person"
                readonly property real depthM: depth > 0 ? depth : 0
                readonly property real lateralM:
                    (2 * centerX / DetectionModel.sourceWidth - 1) * depthM

                visible: (isVehicle || isPedestrian) && depthM > 0
                position: Qt.vector3d(
                    root.modelCenter.x + lateralM * root.metresScale * root.lateralSpread,
                    root.groundY,
                    root.modelCenter.z - depthM * root.metresScale * root.depthCompress)
                // Eased slide so a noisy frame-to-frame jump glides.
                Behavior on position { Vector3dAnimation { duration: 320; easing.type: Easing.InOutQuad } }

                // Grow in on spawn so a (re)appearing track doesn't pop.
                property bool spawned: false
                scale: spawned ? Qt.vector3d(1, 1, 1) : Qt.vector3d(0.01, 0.01, 0.01)
                Behavior on scale { Vector3dAnimation { duration: 240; easing.type: Easing.OutCubic } }
                Component.onCompleted: spawned = true

                TrafficVehicle {
                    className: slot.className
                    metresScale: root.metresScale
                    active: slot.isVehicle
                    visible: slot.visible && slot.isVehicle
                }
                Pedestrian3D {
                    metresScale: root.metresScale
                    // heading = atan2(vy, vx) in image space; vx > 0 means
                    // crossing toward screen-right.
                    facingRight: Math.cos(slot.heading) >= 0
                    active: slot.isPedestrian
                    visible: slot.visible && slot.isPedestrian
                }
            }
        }
    }

    // Drag to orbit the car (park only).
    DragHandler {
        id: drag
        target: null
        enabled: !root.drivingMode
        property real startAz: 0
        property real startEl: 0
        onActiveChanged: {
            if (active) {
                startAz = root.azimuth;
                startEl = root.elevation;
            } else {
                // Wrap into (-180,180] so the next park<->drive transition
                // takes the short way round (no extra full spins).
                root.azimuth = ((root.azimuth % 360) + 540) % 360 - 180;
            }
        }
        onActiveTranslationChanged: {
            root.azimuth = startAz - activeTranslation.x * 0.4;
            root.elevation = Math.max(-80, Math.min(8, startEl - activeTranslation.y * 0.4));
        }
    }
}
