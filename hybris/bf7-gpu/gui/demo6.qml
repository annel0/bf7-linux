import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.VirtualKeyboard

Window {
    id: win
    visible: true
    visibility: Window.FullScreen
    width: 720; height: 1612
    color: "#0d1b2a"
    title: "BF7"

    property int clicks: 0
    property int tick: 0
    Timer { interval: 1000; running: true; repeat: true; onTriggered: win.tick++ }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#0d1b2a" }
            GradientStop { position: 1.0; color: "#1b263b" }
        }
    }

    ColumnLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 26
        spacing: 16

        Text {
            text: "TECNO BF7 · Debian 12"
            color: "#e0e1dd"; font.pixelSize: 38; font.bold: true
            Layout.alignment: Qt.AlignHCenter
        }
        Text {
            text: "Qt Quick на PowerVR Rogue GE8300"
            color: "#778da9"; font.pixelSize: 21
            Layout.alignment: Qt.AlignHCenter
        }
        Text {
            text: Qt.formatDateTime(new Date(), "HH:mm:ss") + (win.tick, "")
            color: "#ffd166"; font.pixelSize: 72; font.family: "monospace"
            Layout.alignment: Qt.AlignHCenter
        }

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 300
            Repeater {
                model: 8
                Rectangle {
                    required property int index
                    width: 52; height: 52; radius: 26
                    color: Qt.hsla(index / 8.0, 0.65, 0.62, 1.0)
                    x: parent.width / 2 - 26 + Math.cos(index * Math.PI / 4) * 110
                    y: parent.height / 2 - 26 + Math.sin(index * Math.PI / 4) * 110
                    SequentialAnimation on scale {
                        loops: Animation.Infinite
                        NumberAnimation { from: 0.6; to: 1.35; duration: 800 + index * 70; easing.type: Easing.InOutSine }
                        NumberAnimation { from: 1.35; to: 0.6; duration: 800 + index * 70; easing.type: Easing.InOutSine }
                    }
                }
            }
            Rectangle {
                anchors.centerIn: parent
                width: 140; height: 140; radius: 20
                color: "#e63946"
                RotationAnimation on rotation { from: 0; to: 360; duration: 3000; loops: Animation.Infinite }
            }
        }

        TextField {
            id: field
            Layout.fillWidth: true
            placeholderText: "коснитесь — появится клавиатура"
            font.pixelSize: 28
            color: "#f1faee"
            background: Rectangle {
                color: "#1d3557"; radius: 12
                border.color: field.activeFocus ? "#ffd166" : "#457b9d"; border.width: 2
            }
        }
        Text {
            text: field.text.length ? "введено: " + field.text : "поле пустое"
            color: "#a8dadc"; font.pixelSize: 20
            Layout.alignment: Qt.AlignHCenter
            elide: Text.ElideRight
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 20
            Rectangle {
                width: 190; height: 72; radius: 14
                color: a1.pressed ? "#457b9d" : "#1d3557"
                border.color: "#a8dadc"; border.width: 2
                Text { anchors.centerIn: parent; text: "Нажми"; color: "#f1faee"; font.pixelSize: 26 }
                MouseArea { id: a1; anchors.fill: parent; onClicked: win.clicks++ }
            }
            Rectangle {
                width: 190; height: 72; radius: 14
                color: a2.pressed ? "#457b9d" : "#1d3557"
                border.color: "#a8dadc"; border.width: 2
                Text { anchors.centerIn: parent; text: "Сброс"; color: "#f1faee"; font.pixelSize: 26 }
                MouseArea { id: a2; anchors.fill: parent; onClicked: { win.clicks = 0; field.text = "" } }
            }
        }
        Text {
            text: "касаний: " + win.clicks
            color: "#a8dadc"; font.pixelSize: 22
            Layout.alignment: Qt.AlignHCenter
        }
    }

    // В eglfs окно ровно одно, поэтому клавиатура живёт внутри сцены,
    // а не отдельным окном (иначе Qt падает: "OpenGL windows cannot be
    // mixed with others").
    InputPanel {
        id: inputPanel
        z: 99
        anchors.left: parent.left
        anchors.right: parent.right
        y: win.height
        states: State {
            name: "visible"
            when: inputPanel.active
            PropertyChanges { target: inputPanel; y: win.height - inputPanel.height }
        }
        transitions: Transition {
            NumberAnimation { properties: "y"; duration: 180; easing.type: Easing.InOutQuad }
        }
    }

    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 14
        text: "секунд с запуска: " + win.tick
        color: "#415a77"; font.pixelSize: 17
    }
}
