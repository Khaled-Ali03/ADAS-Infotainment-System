import QtQuick

// Gold padlock, locked vs unlocked, used in the TopBar and above the car in
// park. The SVGs carry their own colours; `color` is only kept for callers.
Item {
    id: lock
    property bool locked: true
    property color color: "#C59B27"
    property int pixelSize: 22
    width: pixelSize
    height: pixelSize

    Image {
        anchors.fill: parent
        source: lock.locked ? "qrc:/icons/resources/Closed_Lock.svg"
                            : "qrc:/icons/resources/Open_Lock.svg"
        sourceSize.width: pixelSize * 2
        sourceSize.height: pixelSize * 2
        fillMode: Image.PreserveAspectFit
        smooth: true
    }
}
