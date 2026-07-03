import QtQuick
import QtQuick.Shapes
import QtQuick.Effects
import QtLocation
import QtPositioning
import ADAS_HMI

// Small non-interactive route preview for the home view. Auto-centres on the
// live position, draws the shared route + destination pin + heading arrow, and
// fades out at the edges (radial alpha mask) instead of a hard border.
Item {
    id: miniMap

    Plugin {
        id: miniPlugin
        name: "osm"
        PluginParameter { name: "osm.mapping.providersrepository.disabled"; value: "true" }
        PluginParameter { name: "osm.mapping.host"; value: "https://tile.openstreetmap.org/" }
    }

    PositionSource {
        id: pos
        updateInterval: 2000
        active: miniMap.visible
    }

    Map {
        id: map
        anchors.fill: parent
        plugin: miniPlugin
        zoomLevel: 16
        center: pos.position.coordinate.isValid
                ? pos.position.coordinate
                : QtPositioning.coordinate(29.8402, 31.3005)
        // No gesture handlers on purpose — this map is display-only.

        MapPolyline {
            line.color: Theme.accent
            line.width: 5
            path: NavController.routePath
        }

        // Destination pin.
        MapQuickItem {
            visible: NavController.hasPin && NavController.destCoordinate.isValid
            coordinate: NavController.destCoordinate
            anchorPoint.x: 11; anchorPoint.y: 28
            sourceItem: Image {
                source: "qrc:/icons/resources/pin.svg"
                width: 22; height: 28
                sourceSize.width: 22; sourceSize.height: 28
            }
        }

        // Current position, heading-rotated arrow.
        MapQuickItem {
            coordinate: map.center
            anchorPoint.x: 14; anchorPoint.y: 14
            sourceItem: Item {
                width: 28; height: 28
                Rectangle { anchors.fill: parent; radius: 14; color: Theme.accent; border.color: "white"; border.width: 3 }
                Canvas {
                    anchors.fill: parent
                    rotation: SystemHandler.heading
                    onPaint: {
                        var ctx = getContext("2d"); ctx.reset();
                        ctx.fillStyle = "white"; ctx.beginPath();
                        ctx.moveTo(width / 2, height * 0.22);
                        ctx.lineTo(width * 0.72, height * 0.66);
                        ctx.lineTo(width / 2, height * 0.56);
                        ctx.lineTo(width * 0.28, height * 0.66);
                        ctx.closePath(); ctx.fill();
                    }
                }
            }
        }

        layer.enabled: true
        layer.effect: MultiEffect {
            maskEnabled: true
            maskSource: fadeMask
        }
    }

    // Radial alpha mask: opaque centre, gentle fade to transparent at the rim.
    Item {
        id: fadeMask
        anchors.fill: parent
        layer.enabled: true
        visible: false
        Shape {
            id: maskShape
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                strokeWidth: 0
                fillGradient: RadialGradient {
                    centerX: maskShape.width / 2; centerY: maskShape.height / 2
                    centerRadius: maskShape.width / 2
                    focalX: maskShape.width / 2; focalY: maskShape.height / 2
                    GradientStop { position: 0.0;  color: "white" }
                    GradientStop { position: 0.55; color: "white" }
                    GradientStop { position: 1.0;  color: "transparent" }
                }
                startX: 0; startY: 0
                PathLine { x: maskShape.width; y: 0 }
                PathLine { x: maskShape.width; y: maskShape.height }
                PathLine { x: 0; y: maskShape.height }
                PathLine { x: 0; y: 0 }
            }
        }
    }
}
