import QtQuick
import QtQuick.Layouts
import "Format.js" as Format

// What Tab brings in: the file across the top and the transport across the
// bottom, over gradients so they read on any picture. A picture's transport
// steps, zooms and turns rather than plays.
Item {
    id: overlay
    signal toggleFullscreen()
    property bool fullscreen: false
    property var keys
    property Item picture
    // Narrow windows keep what matters most: play, the time, fullscreen.
    readonly property bool roomy: width >= 760
    readonly property bool medium: width >= 560

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 120
        gradient: Gradient {
            GradientStop { position: 0; color: Qt.rgba(0, 0, 0, 0.75) }
            GradientStop { position: 1; color: "transparent" }
        }
    }
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 200
        gradient: Gradient {
            GradientStop { position: 0; color: "transparent" }
            GradientStop { position: 1; color: Qt.rgba(0, 0, 0, 0.85) }
        }
    }

    RowLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 22
        spacing: 12
        Text {
            text: backend.title
            color: "white"
            font.pixelSize: 17
            font.weight: Font.Medium
            elide: Text.ElideRight
            Layout.maximumWidth: overlay.width * 0.5
        }
        Text {
            text: backend.summary
            color: "#9a9aa0"
            font.pixelSize: 13
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
        ControlButton {
            visible: (backend.isPicture || backend.hasVideo) && overlay.medium
            icon: "copy"
            text: backend.isPicture ? qsTr("Copy picture") : qsTr("Copy frame")
            keys: overlay.keys.copy
            onClicked: backend.copy()
        }
        ControlButton {
            visible: backend.canEdit && (backend.isPicture || backend.hasVideo) && overlay.roomy
            icon: backend.isPicture ? "edit" : "scissors"
            text: backend.isPicture ? qsTr("Edit in Omasnap") : qsTr("Trim in Omacut")
            keys: overlay.keys.edit
            onClicked: backend.edit()
        }
    }

    ColumnLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 22
        spacing: 12

        Timeline {
            visible: !backend.isPicture
            expanded: true
            Layout.fillWidth: true
        }

        RowLayout {
            visible: backend.isPicture
            Layout.fillWidth: true
            spacing: 6
            ControlButton {
                visible: backend.playlistCount > 1
                icon: "prev"
                onClicked: backend.previous()
            }
            Text {
                visible: backend.playlistCount > 1
                Layout.leftMargin: 4
                Layout.rightMargin: 4
                text: qsTr("%1 of %2").arg((backend.playlistIndex + 1).toLocaleString(Qt.locale(), 'f', 0))
                                      .arg(backend.playlistCount.toLocaleString(Qt.locale(), 'f', 0))
                color: "white"
                font.pixelSize: 13
            }
            ControlButton {
                visible: backend.playlistCount > 1
                icon: "next"
                onClicked: backend.next()
            }
            Item { Layout.fillWidth: true }
            ControlButton {
                icon: "zoomOut"
                onClicked: overlay.picture.zoomBy(0.8)
            }
            // The scale, which goes back to fitting the window.
            ControlButton {
                text: overlay.picture && !overlay.picture.fitted
                      ? Math.round(overlay.picture.ratio * 100).toLocaleString(Qt.locale(), 'f', 0) + "%" : qsTr("Fit")
                keys: overlay.keys.fit
                onClicked: overlay.picture.fitted ? overlay.picture.actualSize() : overlay.picture.reset()
            }
            ControlButton {
                icon: "zoomIn"
                onClicked: overlay.picture.zoomBy(1.25)
            }
            ControlButton {
                visible: overlay.medium
                icon: "turn"
                keys: overlay.keys.turn
                onClicked: backend.rotate(90)
            }
            ControlButton {
                visible: overlay.medium
                icon: "trash"
                keys: overlay.keys.trash
                onClicked: backend.trash()
            }
            ControlButton {
                icon: overlay.fullscreen ? "unfull" : "full"
                onClicked: overlay.toggleFullscreen()
            }
        }

        RowLayout {
            visible: !backend.isPicture
            Layout.fillWidth: true
            spacing: 6
            ControlButton {
                visible: backend.playlistCount > 1
                icon: "prev"
                onClicked: backend.previous()
            }
            ControlButton {
                primary: true
                icon: backend.playing ? "pause" : "play"
                onClicked: backend.togglePlay()
            }
            ControlButton {
                visible: backend.playlistCount > 1
                icon: "next"
                onClicked: backend.next()
            }
            ControlButton {
                visible: backend.seekable && overlay.medium
                icon: "back"
                onClicked: backend.seekBy(-5000)
            }
            ControlButton {
                visible: backend.seekable && overlay.medium
                icon: "fwd"
                onClicked: backend.seekBy(5000)
            }
            ControlButton {
                icon: backend.muted || backend.volume === 0 ? "mute" : "volume"
                onClicked: backend.toggleMute()
            }
            // The volume, draggable.
            Item {
                visible: overlay.roomy
                Layout.preferredWidth: 90
                Layout.preferredHeight: 20
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width
                    height: 4
                    color: Qt.rgba(1, 1, 1, 0.2)
                    Rectangle {
                        height: parent.height
                        width: parent.width * (backend.muted ? 0 : backend.volume)
                        color: "white"
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    preventStealing: true
                    function set(x) { backend.setVolume(Math.max(0, Math.min(1, x / width))) }
                    onPressed: mouse => set(mouse.x)
                    onPositionChanged: mouse => { if (pressed) set(mouse.x) }
                }
            }
            Text {
                Layout.leftMargin: 10
                text: backend.duration > 0 ? Format.clock(backend.position) + "  /  " + Format.clock(backend.duration)
                                           : (backend.seekable ? Format.clock(backend.position) : qsTr("Live"))
                color: "white"
                font.family: "monospace"
                font.pixelSize: 13
            }
            Item { Layout.fillWidth: true }
            ControlButton {
                visible: overlay.medium || backend.rate !== 1
                text: backend.rate.toLocaleString(Qt.locale(), 'f', backend.rate * 4 % 4 === 0 ? 0 : 2) + "×"
                keys: overlay.keys.speed
                onClicked: backend.changeRate(backend.rate >= 2 ? -(backend.rate - 1) : 0.25)
            }
            ControlButton {
                visible: backend.subtitleChoices > 0 && overlay.medium
                icon: "subs"
                text: backend.subtitleLabel !== "" ? backend.subtitleLabel : qsTr("Off")
                keys: overlay.keys.subtitles
                onClicked: backend.cycleSubtitles()
            }
            ControlButton {
                visible: backend.audioTracks > 1 && overlay.roomy
                icon: "audio"
                text: backend.audioLabel
                keys: overlay.keys.audio
                onClicked: backend.cycleAudio()
            }
            ControlButton {
                icon: overlay.fullscreen ? "unfull" : "full"
                onClicked: overlay.toggleFullscreen()
            }
        }
    }
}
