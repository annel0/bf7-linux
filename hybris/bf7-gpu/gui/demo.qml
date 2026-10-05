import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: root
    width: 720; height: 1612
    gradient: Gradient {
        GradientStop { position: 0.0; color: "#0d1b2a" }
        GradientStop { position: 1.0; color: "#1b263b" }
    }

    property int tick: 0
    Timer { interval: 1000; running: true; repeat: true; onTriggered: root.tick++ }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 28
        spacing: 22

        Text {
            text: "TECNO BF7 · Debian 12"
            color: "#e0e1dd"
            font.pixelSize: 40
            font.bold: true
            Layout.alignment: Qt.AlignHCenter
        }
        Text {
            text: "GUI на PowerVR Rogue GE8300"
            color: "#778da9"
            font.pixelSize: 24
            Layout.alignment: Qt.AlignHCenter
        }

        Text {
            id: clock
            text: Qt.formatDateTime(new Date(), "HH:mm:ss")
            color: "#ffd166"
            font.pixelSize: 84
            font.family: "monospace"
            Layout.alignment: Qt.AlignHCenter
            Connections {
                target: root
                function onTickChanged() {
                    clock.text = Qt.formatDateTime(new Date(), "HH:mm:ss")
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 320

            Repeater {
                model: 8
                Rectangle {
                    width: 54; height: 54; radius: 27
                    color: Qt.hsla(index / 8.0, 0.65, 0.6, 1.0)
                    x: parent.width / 2 - 27
                    y: parent.height / 2 - 27
                    transform: Rotation {
                        origin.x: 27; origin.y: 27
                        axis { x: 0; y: 0; z: 1 }
                        NumberAnimation on angle {
                            from: index * 45; to: index * 45 + 360
                            duration: 4000; loops: Animation.Infinite
                        }
                    }
                    Behavior on color { ColorAnimation { duration: 400 } }
                    Component.onCompleted: {
                        // разносим шарики по окружности
                        var a = index * Math.PI / 4
                        x = parent.width / 2 - 27 + Math.cos(a) * 120
                        y = parent.height / 2 - 27 + Math.sin(a) * 120
                    }
                    SequentialAnimation on scale {
                        loops: Animation.Infinite
                        NumberAnimation { from: 0.7; to: 1.3; duration: 900 + index * 60; easing.type: Easing.InOutSine }
                        NumberAnimation { from: 1.3; to: 0.7; duration: 900 + index * 60; easing.type: Easing.InOutSine }
                    }
                }
            }
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 18
            Button {
                text: "Нажми"
                font.pixelSize: 26
                onClicked: status.text = "нажато " + (++root.clicks) + " раз"
            }
            Button {
                text: "Сброс"
                font.pixelSize: 26
                onClicked: { root.clicks = 0; status.text = "касание работает?" }
            }
        }
        property int clicks: 0

        Text {
            id: status
            text: "касание работает?"
            color: "#a8dadc"
            font.pixelSize: 22
            Layout.alignment: Qt.AlignHCenter
        }

        Item { Layout.fillHeight: true }

        Text {
            text: "кадр " + root.tick + " · Qt " + qtVersion
            property string qtVersion: "5.15"
            color: "#415a77"
            font.pixelSize: 18
            Layout.alignment: Qt.AlignHCenter
        }
    }
}
