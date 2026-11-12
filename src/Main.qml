import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Window
import QtMultimedia
import "Format.js" as Format

ApplicationWindow {
    id: win
    width: 960
    height: 540
    minimumWidth: 320
    minimumHeight: 200
    visible: true
    title: backend.hasMedia ? backend.title + " — Showtime" : "Showtime"
    // The picture's surround is black whatever the theme, as every player's
    // is; ApplicationWindow would otherwise paint Material's background.
    color: "black"
    background: Rectangle { color: "black" }

    Material.theme: theme.light ? Material.Light : Material.Dark
    Material.accent: theme.accent
    Material.background: theme.background
    Material.foreground: theme.foreground

    readonly property bool rightToLeft: Qt.application.layoutDirection === Qt.RightToLeft
    readonly property bool fullscreen: visibility === Window.FullScreen
    property bool overlayShown: false
    property bool sized: false

    // The one table of keys: the shortcuts, the overlay's labels and the ?
    // overlay all read it, so they can't drift apart.
    readonly property var keys: ({
        play: "Space", seek: "← →", copyFrame: "Ctrl+Shift+C", trim: "E", speed: "[ ]",
        subtitles: "C", audio: "A"
    })
    readonly property var shortcutList: [
        { keys: "Space  K", action: qsTr("Play or pause") },
        { keys: "← →", action: qsTr("Back or forward 5 seconds") },
        { keys: "Shift+← →", action: qsTr("Back or forward 1 second") },
        { keys: "J  L", action: qsTr("Back or forward 10 seconds") },
        { keys: "0 – 9", action: qsTr("Jump to that tenth of the way") },
        { keys: "Home", action: qsTr("Back to the start") },
        { keys: ",  .", action: qsTr("Step one frame back or forward") },
        { keys: "PgUp  PgDn", action: qsTr("Previous or next chapter") },
        { keys: "[  ]", action: qsTr("Slower or faster") },
        { keys: "Backspace", action: qsTr("Normal speed") },
        { keys: "↑ ↓", action: qsTr("Louder or quieter") },
        { keys: "M", action: qsTr("Mute") },
        { keys: "C", action: qsTr("Next subtitles, or off") },
        { keys: "A", action: qsTr("Next audio track") },
        { keys: "Tab", action: qsTr("Show or hide the controls") },
        { keys: "F", action: qsTr("Fullscreen") },
        { keys: "N  P", action: qsTr("Next or previous file") },
        { keys: "I", action: qsTr("What's in the file") },
        { keys: "Ctrl+Shift+C", action: qsTr("Copy the frame") },
        { keys: "Ctrl+S", action: qsTr("Save the frame") },
        { keys: "E", action: qsTr("Trim in Omacut") },
        { keys: "Ctrl+O", action: qsTr("Open files") },
        { keys: "?", action: qsTr("Show or hide the shortcuts") },
        { keys: "Q", action: qsTr("Quit") }
    ]

    function toggleFullscreen() {
        if (fullscreen)
            showNormal()
        else
            showFullScreen()
    }

    // The display that answers a key: the state and time in the corner, and
    // what changed in the middle.
    function poke() {
        cornerTimer.restart()
        cornerOsd.shown = true
    }
    function say(value, hint) {
        centerOsd.value = value
        centerOsd.hint = hint || ""
        centerOsd.shown = true
        centerTimer.restart()
        poke()
    }
    function showSeek() {
        seekTimer.restart()
        poke()
    }
    function percent(value) {
        return Math.round(value * 100).toLocaleString(Qt.locale(), 'f', 0) + "%"
    }
    function speedText() {
        return backend.rate.toLocaleString(Qt.locale(), 'f', backend.rate * 4 % 4 === 0 ? 0 : 2) + "×"
    }

    // The window opens at the video's shape, as large as fits in 80% of the
    // screen, and never larger than the video itself.
    function sizeToVideo() {
        if (sized || fullscreen || !backend.hasVideo || backend.videoSize.width <= 0)
            return
        sized = true
        const maxWidth = Screen.desktopAvailableWidth * 0.8, maxHeight = Screen.desktopAvailableHeight * 0.8
        const scale = Math.min(1, maxWidth / backend.videoSize.width, maxHeight / backend.videoSize.height)
        width = Math.max(minimumWidth, Math.round(backend.videoSize.width * scale))
        height = Math.max(minimumHeight, Math.round(backend.videoSize.height * scale))
    }

    // The screen stays on while a video plays, and only then.
    Binding {
        target: idle
        property: "active"
        value: backend.playing && backend.hasVideo
    }

    Connections {
        target: backend
        function onVideoSizeChanged() { win.sizeToVideo() }
        function onPictureCleared() {
            if (videoLoader.item)
                videoLoader.item.clearOutput()
        }
        function onNotice(text) { win.say(text, "") }
        function onResumedAtChanged() {
            if (backend.resumedAt >= 0)
                win.say(qsTr("Resumed at %1").arg(Format.clock(backend.resumedAt)), qsTr("Home starts over"))
        }
        function onMediaChanged() {
            win.poke()
            if (backend.takeFirstRunHint())
                win.say(qsTr("Press Tab for controls"), qsTr("? shows every key"))
        }
    }

    onClosing: backend.close()

    Shortcut {
        sequences: ["Space", "K"]
        enabled: backend.hasMedia
        onActivated: {
            const wasPlaying = backend.playing
            backend.togglePlay()
            win.say(wasPlaying ? qsTr("Paused") : qsTr("Playing"), "")
        }
    }
    Shortcut { sequence: "Left"; onActivated: { backend.seekBy(-5000); win.showSeek() } }
    Shortcut { sequence: "Right"; onActivated: { backend.seekBy(5000); win.showSeek() } }
    Shortcut { sequence: "Shift+Left"; onActivated: { backend.seekBy(-1000); win.showSeek() } }
    Shortcut { sequence: "Shift+Right"; onActivated: { backend.seekBy(1000); win.showSeek() } }
    Shortcut { sequence: "J"; onActivated: { backend.seekBy(-10000); win.showSeek() } }
    Shortcut { sequence: "L"; onActivated: { backend.seekBy(10000); win.showSeek() } }
    Shortcut { sequence: "Home"; onActivated: { backend.startOver(); win.showSeek() } }
    function jumpTenth(n) { backend.seekFraction(n / 10); showSeek() }
    Shortcut { sequence: "0"; onActivated: win.jumpTenth(0) }
    Shortcut { sequence: "1"; onActivated: win.jumpTenth(1) }
    Shortcut { sequence: "2"; onActivated: win.jumpTenth(2) }
    Shortcut { sequence: "3"; onActivated: win.jumpTenth(3) }
    Shortcut { sequence: "4"; onActivated: win.jumpTenth(4) }
    Shortcut { sequence: "5"; onActivated: win.jumpTenth(5) }
    Shortcut { sequence: "6"; onActivated: win.jumpTenth(6) }
    Shortcut { sequence: "7"; onActivated: win.jumpTenth(7) }
    Shortcut { sequence: "8"; onActivated: win.jumpTenth(8) }
    Shortcut { sequence: "9"; onActivated: win.jumpTenth(9) }
    Shortcut { sequence: ","; onActivated: { backend.stepFrame(-1); win.showSeek() } }
    Shortcut { sequence: "."; onActivated: { backend.stepFrame(1); win.showSeek() } }
    Shortcut {
        sequence: "PgUp"
        onActivated: { backend.jumpChapter(-1); win.showSeek(); if (backend.chapterTitle !== "") win.say(backend.chapterTitle, qsTr("Chapter")) }
    }
    Shortcut {
        sequence: "PgDown"
        onActivated: { backend.jumpChapter(1); win.showSeek(); if (backend.chapterTitle !== "") win.say(backend.chapterTitle, qsTr("Chapter")) }
    }
    Shortcut { sequence: "["; onActivated: { backend.changeRate(-0.25); win.say(win.speedText(), qsTr("Speed · [ slower · ] faster · Backspace resets")) } }
    Shortcut { sequence: "]"; onActivated: { backend.changeRate(0.25); win.say(win.speedText(), qsTr("Speed · [ slower · ] faster · Backspace resets")) } }
    Shortcut { sequence: "Backspace"; onActivated: { backend.resetRate(); win.say(win.speedText(), qsTr("Speed")) } }
    Shortcut { sequence: "Up"; onActivated: { backend.changeVolume(0.05); win.say(win.percent(backend.volume), qsTr("Volume")) } }
    Shortcut { sequence: "Down"; onActivated: { backend.changeVolume(-0.05); win.say(win.percent(backend.volume), qsTr("Volume")) } }
    Shortcut { sequence: "M"; onActivated: { backend.toggleMute(); win.say(backend.muted ? qsTr("Muted") : win.percent(backend.volume), qsTr("Volume")) } }
    Shortcut {
        sequence: "C"
        onActivated: {
            backend.cycleSubtitles()
            win.say(backend.subtitleChoices === 0 ? qsTr("No subtitles") : backend.subtitleLabel !== "" ? backend.subtitleLabel : qsTr("Off"), qsTr("Subtitles"))
        }
    }
    Shortcut {
        sequence: "A"
        onActivated: {
            backend.cycleAudio()
            win.say(backend.audioTracks < 2 ? qsTr("Only one audio track") : backend.audioLabel, qsTr("Audio"))
        }
    }
    Shortcut { sequence: "Tab"; onActivated: win.overlayShown = !win.overlayShown }
    Shortcut { sequence: "F"; onActivated: win.toggleFullscreen() }
    Shortcut {
        sequence: "Esc"
        onActivated: {
            if (win.overlayShown)
                win.overlayShown = false
            else if (win.fullscreen)
                win.showNormal()
        }
    }
    Shortcut { sequence: "N"; onActivated: { if (!backend.next()) win.say(qsTr("This is the last file"), "") } }
    Shortcut { sequence: "P"; onActivated: { if (!backend.previous()) win.say(qsTr("This is the first file"), "") } }
    Shortcut { sequences: ["Return", "Enter"]; enabled: backend.ended && backend.nextName !== ""; onActivated: backend.next() }
    Shortcut { sequence: "I"; enabled: backend.hasMedia; onActivated: infoCard.opened ? infoCard.close() : infoCard.open() }
    Shortcut { sequence: "Ctrl+Shift+C"; onActivated: backend.copyFrame() }
    Shortcut { sequence: "Ctrl+S"; onActivated: backend.saveFrame() }
    Shortcut { sequence: "E"; enabled: backend.canTrim; onActivated: backend.trimInOmacut() }
    Shortcut { sequence: "Ctrl+O"; onActivated: backend.openDialog() }
    Shortcut { sequences: ["?", "F1"]; onActivated: help.opened ? help.close() : help.open() }
    Shortcut { sequences: ["Q", "Ctrl+Q"]; onActivated: win.close() }

    // Right-to-left languages mirror the window; the Translator sets the
    // application's layout direction.
    Item {
        id: root
        anchors.fill: parent
        LayoutMirroring.enabled: win.rightToLeft
        LayoutMirroring.childrenInherit: true

        // Kept until the first file, so a window with nothing to play never
        // starts Qt Multimedia's FFmpeg backend.
        Loader {
            id: videoLoader
            anchors.fill: parent
            active: backend.playerReady
            sourceComponent: VideoOutput {
                objectName: "video"
                fillMode: VideoOutput.PreserveAspectFit
                endOfStreamPolicy: VideoOutput.KeepLastFrame
                Component.onCompleted: backend.setVideoSink(videoSink)
                // Detached as destruction begins, before the sink is torn
                // down, so the decoder's thread never hands it a frame after.
                Component.onDestruction: backend.setVideoSink(null)
            }
        }
        readonly property rect picture: videoLoader.item && backend.hasVideo
                                        ? videoLoader.item.contentRect : Qt.rect(0, 0, width, height)

        // A song: its cover and its name.
        ColumnLayout {
            anchors.centerIn: parent
            visible: backend.hasMedia && !backend.hasVideo && !backend.loading && backend.error === ""
            spacing: 14
            width: Math.min(parent.width - 48, 420)
            Image {
                Layout.alignment: Qt.AlignHCenter
                visible: backend.coverArt !== ""
                source: backend.coverArt
                fillMode: Image.PreserveAspectFit
                Layout.preferredWidth: Math.min(root.width * 0.6, root.height * 0.55, 360)
                Layout.preferredHeight: Layout.preferredWidth
            }
            Icon {
                Layout.alignment: Qt.AlignHCenter
                visible: backend.coverArt === ""
                name: "music"
                size: 64
                color: "#6a6a70"
            }
            Text {
                Layout.fillWidth: true
                text: backend.title
                color: "white"
                font.pixelSize: 20
                font.weight: Font.Medium
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                visible: backend.artist !== ""
                text: backend.artist
                color: "#9a9aa0"
                font.pixelSize: 15
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }
        }

        // Subtitles, drawn in one style whether they came from the file or
        // from beside it, sized to the picture.
        Text {
            id: subtitle
            objectName: "subtitle"
            visible: text !== ""
            text: backend.subtitleText
            textFormat: Text.PlainText
            color: "white"
            width: root.picture.width * 0.9
            x: root.picture.x + (root.picture.width - width) / 2
            y: root.picture.y + root.picture.height - height - root.picture.height * 0.06
               - (win.overlayShown ? Math.max(0, 140 - (root.height - root.picture.y - root.picture.height)) : 0)
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            font.pixelSize: Math.max(14, Math.round(root.picture.height * 0.045))
            font.weight: Font.Medium
            style: Text.Outline
            styleColor: Qt.rgba(0, 0, 0, 0.6)
            Rectangle {
                z: -1
                anchors.centerIn: parent
                width: subtitle.contentWidth + 24
                height: subtitle.contentHeight + 8
                color: Qt.rgba(0, 0, 0, 0.55)
            }
        }

        // Nothing open yet: say how to start.
        ColumnLayout {
            anchors.centerIn: parent
            visible: !backend.hasMedia
            spacing: 10
            Icon {
                Layout.alignment: Qt.AlignHCenter
                name: "play"
                size: 40
                color: theme.accent
            }
            Text {
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("Drop a video or a song here")
                color: "white"
                font.pixelSize: 18
            }
            Text {
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("or press Ctrl+O to open one")
                color: "#9a9aa0"
                font.pixelSize: 14
            }
        }

        // What went wrong, in plain words, rather than a black picture.
        ColumnLayout {
            anchors.centerIn: parent
            visible: backend.hasMedia && backend.error !== ""
            width: Math.min(parent.width - 48, 520)
            spacing: 8
            Text {
                Layout.fillWidth: true
                text: qsTr("Showtime can't play %1").arg(backend.fileName)
                color: "white"
                font.pixelSize: 17
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
            }
            Text {
                objectName: "error"
                Layout.fillWidth: true
                text: backend.error
                color: "#9a9aa0"
                font.pixelSize: 14
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
            }
        }

        // The end of a file, with the next one offered, never started.
        Rectangle {
            visible: backend.ended && backend.nextName !== "" && !win.overlayShown
            anchors.centerIn: parent
            width: endColumn.implicitWidth + 40
            height: endColumn.implicitHeight + 28
            color: Qt.rgba(theme.background.r, theme.background.g, theme.background.b, 0.88)
            radius: theme.radius
            Column {
                id: endColumn
                anchors.centerIn: parent
                spacing: 6
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("Next: %1").arg(backend.nextName)
                    color: theme.foreground
                    font.pixelSize: 17
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("Enter plays it · N skips to it")
                    color: theme.colors.muted
                    font.pixelSize: 13
                }
            }
        }

        // Clicking plays and pauses; a double click is fullscreen, and its
        // second click undoes the first one's pause.
        TapHandler {
            id: tap
            enabled: backend.hasMedia
            onTapped: {
                backend.togglePlay()
                if (tap.tapCount === 2)
                    win.toggleFullscreen()
            }
        }
        WheelHandler {
            enabled: backend.hasMedia
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            onWheel: event => {
                const steps = event.angleDelta.y / 120
                if (steps === 0)
                    return
                backend.changeVolume(0.05 * steps)
                win.say(win.percent(backend.volume), qsTr("Volume"))
            }
        }
        // The pointer hides after a second of stillness.
        HoverHandler {
            id: pointer
            property bool still: false
            cursorShape: still && !win.overlayShown ? Qt.BlankCursor : Qt.ArrowCursor
            onPointChanged: { still = false; stillTimer.restart() }
        }
        Timer { id: stillTimer; interval: 1000; onTriggered: pointer.still = true }

        DropArea {
            anchors.fill: parent
            onDropped: drop => {
                if (drop.hasUrls) {
                    backend.openUrls(drop.urls)
                    drop.accept()
                }
            }
        }

        // The thin line, which grows into a timeline when the pointer comes
        // to the bottom edge or a key seeks.
        Item {
            id: bottomEdge
            visible: backend.hasMedia && !win.overlayShown
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 40
            HoverHandler { id: edgeHover }
            Rectangle {
                anchors.fill: parent
                visible: bareTimeline.expanded
                gradient: Gradient {
                    GradientStop { position: 0; color: "transparent" }
                    GradientStop { position: 1; color: Qt.rgba(0, 0, 0, 0.6) }
                }
            }
            Timeline {
                id: bareTimeline
                objectName: "progressLine"
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.leftMargin: expanded ? 16 : 0
                anchors.rightMargin: expanded ? 16 : 0
                anchors.bottomMargin: expanded ? 6 : 0
                expanded: edgeHover.hovered || seekTimer.running
            }
        }
        Timer { id: seekTimer; interval: 2000 }

        // The corner: playing or paused, the time, the file.
        Rectangle {
            id: cornerOsd
            objectName: "cornerOsd"
            property bool shown: false
            visible: opacity > 0
            opacity: shown && backend.hasMedia && !win.overlayShown ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 180 } }
            x: win.rightToLeft ? parent.width - width - 20 : 20
            y: 20
            width: cornerRow.implicitWidth + 28
            height: 40
            radius: Math.min(4, theme.radius)
            color: Qt.rgba(theme.background.r, theme.background.g, theme.background.b, 0.85)
            Row {
                id: cornerRow
                anchors.centerIn: parent
                spacing: 12
                Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: backend.playing ? "play" : "pause"
                    size: 14
                    color: theme.accent
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    // Formatted only while it shows, not on every position update.
                    text: !cornerOsd.visible ? ""
                          : backend.duration > 0 ? Format.clock(backend.position) + " / " + Format.clock(backend.duration)
                          : (backend.seekable ? Format.clock(backend.position) : qsTr("Live"))
                    color: theme.foreground
                    font.family: "monospace"
                    font.pixelSize: 13
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: backend.buffering ? qsTr("Buffering…") : backend.fileName
                    color: theme.colors.muted
                    font.pixelSize: 13
                    elide: Text.ElideMiddle
                    width: Math.min(implicitWidth, win.width * 0.5)
                }
            }
        }
        Timer { id: cornerTimer; interval: 2000; onTriggered: cornerOsd.shown = backend.hasMedia && !backend.playing }

        // The middle: what just changed.
        Rectangle {
            id: centerOsd
            objectName: "centerOsd"
            property bool shown: false
            property string value
            property string hint
            visible: opacity > 0
            opacity: shown ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 180 } }
            anchors.centerIn: parent
            width: Math.min(parent.width - 40, centerColumn.implicitWidth + 52)
            height: centerColumn.implicitHeight + 32
            radius: theme.radius
            color: Qt.rgba(theme.background.r, theme.background.g, theme.background.b, 0.85)
            Column {
                id: centerColumn
                anchors.centerIn: parent
                width: Math.min(implicitWidth, centerOsd.width - 52)
                spacing: 8
                Text {
                    objectName: "centerValue"
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: Math.min(implicitWidth, win.width - 92)
                    text: centerOsd.value
                    color: theme.accent
                    font.pixelSize: 28
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignHCenter
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: text !== ""
                    width: Math.min(implicitWidth, win.width - 92)
                    text: centerOsd.hint
                    color: theme.colors.muted
                    font.pixelSize: 13
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }
        Timer { id: centerTimer; interval: 2000; onTriggered: centerOsd.shown = false }

        // Made only while it shows, so its clock and timeline cost nothing
        // during bare playback.
        Loader {
            id: controls
            objectName: "overlay"
            anchors.fill: parent
            active: win.overlayShown && backend.hasMedia
            sourceComponent: ControlsOverlay {
                fullscreen: win.fullscreen
                keys: win.keys
                onToggleFullscreen: win.toggleFullscreen()
                opacity: 0
                Component.onCompleted: opacity = 1
                Behavior on opacity { NumberAnimation { duration: 150 } }
            }
        }
    }

    // I: what's in the file.
    Popup {
        id: infoCard
        objectName: "info"
        anchors.centerIn: Overlay.overlay
        width: Math.min(win.width - 40, 560)
        height: Math.min(win.height - 40, infoColumn.implicitHeight + 40)
        modal: true
        focus: true
        padding: 20
        background: Rectangle {
            color: Qt.rgba(theme.background.r, theme.background.g, theme.background.b, 0.95)
            border.color: theme.colors.selection
            radius: theme.radius
        }
        Flickable {
            anchors.fill: parent
            contentHeight: infoColumn.implicitHeight
            clip: true
            ColumnLayout {
                id: infoColumn
                width: parent.width
                spacing: 6
                LayoutMirroring.enabled: win.rightToLeft
                LayoutMirroring.childrenInherit: true
                Repeater {
                    model: backend.info
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 16
                        Label {
                            Layout.preferredWidth: 110
                            text: modelData.label
                            color: theme.colors.muted
                        }
                        Label {
                            Layout.fillWidth: true
                            text: modelData.value
                            wrapMode: Text.Wrap
                        }
                    }
                }
                Label {
                    visible: backend.chapters.length > 0
                    Layout.topMargin: 8
                    text: qsTr("Chapters")
                    color: theme.colors.muted
                    font.capitalization: Font.AllUppercase
                    font.pixelSize: 11
                }
                Repeater {
                    model: backend.chapters
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 16
                        Label {
                            Layout.preferredWidth: 110
                            text: Format.clock(modelData.start)
                            font.family: "monospace"
                            color: modelData.title === backend.chapterTitle ? theme.accent : theme.colors.muted
                        }
                        Label {
                            Layout.fillWidth: true
                            text: modelData.title
                            color: modelData.title === backend.chapterTitle ? theme.accent : theme.foreground
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }
    }

    // ?: every key.
    Popup {
        id: help
        objectName: "help"
        anchors.centerIn: Overlay.overlay
        width: Math.min(win.width - 40, helpGrid.implicitWidth + 48)
        height: Math.min(win.height - 40, helpGrid.implicitHeight + 48)
        modal: true
        focus: true
        padding: 24
        background: Rectangle {
            color: Qt.rgba(theme.background.r, theme.background.g, theme.background.b, 0.95)
            border.color: theme.colors.selection
            radius: theme.radius
        }
        Flickable {
            anchors.fill: parent
            contentHeight: helpGrid.implicitHeight
            contentWidth: width
            clip: true
            GridLayout {
                id: helpGrid
                columns: 2
                columnSpacing: 24
                rowSpacing: 6
                // A popup is not an Item and doesn't inherit the window's
                // mirroring, so its content opts in.
                LayoutMirroring.enabled: win.rightToLeft
                LayoutMirroring.childrenInherit: true
                Repeater {
                    model: win.shortcutList
                    delegate: Label {
                        required property var modelData
                        required property int index
                        Layout.row: index
                        Layout.column: 0
                        text: modelData.keys
                        color: theme.accent
                        font.family: "monospace"
                        font.bold: true
                    }
                }
                Repeater {
                    model: win.shortcutList
                    delegate: Label {
                        required property var modelData
                        required property int index
                        Layout.row: index
                        Layout.column: 1
                        text: modelData.action
                    }
                }
            }
        }
    }
}
