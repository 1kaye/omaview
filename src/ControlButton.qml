import QtQuick

// A control on the overlay: an icon, an optional label, and its key, which
// the shortcuts overlay shows too.
Rectangle {
    id: control
    property string icon
    property string text
    property string keys
    property bool primary: false
    property bool outlined: text !== ""
    signal clicked()
    implicitHeight: primary ? 46 : (outlined ? 30 : 36)
    implicitWidth: outlined ? row.implicitWidth + 20 : implicitHeight
    radius: Math.min(4, theme.radius)
    color: primary ? theme.accent : mouse.containsMouse ? Qt.rgba(1, 1, 1, 0.12) : "transparent"
    border.width: outlined && !primary ? 1 : 0
    border.color: Qt.rgba(1, 1, 1, 0.25)
    Accessible.role: Accessible.Button
    Accessible.name: text

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 7
        Icon {
            visible: control.icon !== ""
            name: control.icon
            size: control.primary ? 20 : (control.outlined ? 14 : 18)
            color: control.primary ? theme.accentForeground : "#e6e6ea"
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            visible: control.text !== ""
            text: control.text
            color: "#e6e6ea"
            font.pixelSize: 13
            width: Math.min(implicitWidth, 160)
            elide: Text.ElideRight
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            visible: control.outlined && control.keys !== ""
            text: control.keys
            color: "#8a8a90"
            font.family: "monospace"
            font.pixelSize: 11
            anchors.verticalCenter: parent.verticalCenter
        }
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: control.clicked()
    }
}
