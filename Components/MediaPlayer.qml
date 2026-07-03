import QtQuick
import QtQuick.Layouts
import ADAS_HMI

// Compact now-playing card for the home view. Binds the same MediaController
// as the RightScreen drawer, so playback is continuous across views.
Rectangle {
    id: mediaCard
    radius: 14
    color: Theme.surface
    border.color: Theme.border
    border.width: 1

    RowLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 12

        Image {
            source: MediaController.albumArt
            sourceSize.width: 52; sourceSize.height: 52
            Layout.preferredWidth: 52; Layout.preferredHeight: 52
        }

        ColumnLayout {
            Layout.fillWidth: true; spacing: 2
            Text {
                text: MediaController.trackTitle === "" ? "Not playing" : MediaController.trackTitle
                color: Theme.textPrimary; font.pixelSize: 15; font.bold: true
                elide: Text.ElideRight; Layout.fillWidth: true
            }
            Text {
                text: MediaController.statusText !== "" ? MediaController.statusText : MediaController.artist
                color: Theme.textSecondary; font.pixelSize: 12
                elide: Text.ElideRight; Layout.fillWidth: true
            }
            Text {
                visible: MediaController.isLive
                text: "● LIVE"; color: Theme.danger; font.pixelSize: 10; font.bold: true
            }
        }

        component XportButton : Rectangle {
            property string glyph: ""
            property int size: 36
            signal activated()
            Layout.preferredWidth: size; Layout.preferredHeight: size
            radius: size / 2
            color: tap.pressed ? Theme.surfaceAlt : "transparent"
            Text { anchors.centerIn: parent; text: glyph; color: Theme.textPrimary; font.pixelSize: 18 }
            MouseArea { id: tap; anchors.fill: parent; onClicked: parent.activated() }
        }

        XportButton { glyph: "⏮"; onActivated: MediaController.previous() }
        XportButton {
            glyph: MediaController.playing ? "⏸" : "▶"; size: 42
            onActivated: MediaController.togglePlay()
        }
        XportButton { glyph: "⏭"; onActivated: MediaController.next() }
    }
}
