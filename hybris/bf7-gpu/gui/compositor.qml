/*
 * Композитор Wayland для BF7.
 *
 * Работает поверх нашей платформы EGL (eglfs + mtkfb), то есть композиция и
 * вывод идут на PowerVR. Сам по себе это сервер отображения, а не оконный
 * менеджер: окнами управляет то, что запущено внутри — Xwayland с Xfce.
 *
 * Масштаб и поворот живут здесь, а не в настройках Xfce. Причина: Xwayland
 * запущен без -rootless, весь экран X — это одна поверхность Wayland, и её
 * размер диктует вывод композитора. Поэтому вывод объявляется в «логических»
 * пикселях (panel / scale), а на панель он растягивается и поворачивается
 * трансформацией сцены, то есть силами GPU. Действует на всё разом, включая
 * сам оконный менеджер и его декорации.
 *
 * BF7_SCALE    — 0.25..4.0, во сколько раз укрупнять картинку
 * BF7_ROTATION — 0, 90, 180, 270
 */
import QtQuick
import QtWayland.Compositor
import QtWayland.Compositor.XdgShell
import QtWayland.Compositor.WlShell

WaylandCompositor {
    id: comp
    socketName: "wayland-1"

    property Item surfaceArea: null
    property int clientCount: 0

    // Физический размер панели; заполняется окном после разворачивания.
    property int panelW: 720
    property int panelH: 1612

    readonly property real uiScale: (typeof bf7Scale !== "undefined") ? bf7Scale : 1.0
    readonly property int uiRotation: (typeof bf7Rotation !== "undefined") ? bf7Rotation : 0
    readonly property bool sideways: uiRotation === 90 || uiRotation === 270
    readonly property int logicalW: Math.max(160, Math.round((sideways ? panelH : panelW) / uiScale))
    readonly property int logicalH: Math.max(160, Math.round((sideways ? panelW : panelH) / uiScale))

    WaylandOutput {
        id: output
        objectName: "bf7Output"
        compositor: comp
        // Размер вывода выставляется из C++ (см. main.cpp): из QML
        // geometry доступна только на чтение, а Xwayland берёт размер
        // экрана именно из wl_output.
        sizeFollowsWindow: true

        window: Window {
            id: win
            visible: true
            visibility: Window.FullScreen
            width: 720
            height: 1612
            color: "#0b0f14"

            onWidthChanged: comp.panelW = width
            onHeightChanged: comp.panelH = height
            Component.onCompleted: {
                comp.panelW = width
                comp.panelH = height
            }

            /*
             * Пульс: постоянно движущийся квадрат.
             *
             * Нужен для разделения задачи. Если он движется плавно, значит
             * композитор и тракт вывода живы, и застывание рабочего стола
             * приходит со стороны Xwayland. Заодно он держит непрерывную
             * отрисовку — если при нём стол перестаёт застывать, значит
             * вендорскому тракту просто нужен следующий кадр, чтобы показать
             * предыдущий.
             *
             * Включается переменной BF7_HEARTBEAT, по умолчанию выключен.
             */
            Rectangle {
                id: heartbeat
                visible: (typeof bf7Heartbeat !== "undefined") && bf7Heartbeat
                width: 36
                height: 36
                radius: 6
                color: "#ff5555"
                x: 6
                z: 1000
                NumberAnimation on y {
                    running: heartbeat.visible
                    from: 6
                    to: 260
                    duration: 1200
                    loops: Animation.Infinite
                    easing.type: Easing.InOutQuad
                }
            }

            Item {
                id: stack
                width: comp.logicalW
                height: comp.logicalH
                anchors.centerIn: parent
                transform: [
                    Rotation {
                        origin.x: stack.width / 2
                        origin.y: stack.height / 2
                        angle: comp.uiRotation
                    },
                    Scale {
                        origin.x: stack.width / 2
                        origin.y: stack.height / 2
                        xScale: comp.uiScale
                        yScale: comp.uiScale
                    }
                ]
                Component.onCompleted: comp.surfaceArea = stack
            }

            Column {
                anchors.centerIn: parent
                spacing: 10
                visible: comp.clientCount === 0
                Text {
                    text: "композитор Wayland запущен"
                    color: "#5a7d9a"
                    font.pixelSize: 26
                    anchors.horizontalCenter: parent.horizontalCenter
                }
                Text {
                    text: "вывод " + comp.logicalW + "x" + comp.logicalH
                          + " -> панель " + comp.panelW + "x" + comp.panelH
                          + ", масштаб " + comp.uiScale.toFixed(2)
                          + ", поворот " + comp.uiRotation + "°"
                    color: "#37505f"
                    font.pixelSize: 17
                    anchors.horizontalCenter: parent.horizontalCenter
                }
                Text {
                    text: "клиентов нет"
                    color: "#37505f"
                    font.pixelSize: 17
                    anchors.horizontalCenter: parent.horizontalCenter
                }
            }
        }
    }

    Component {
        id: chrome
        ShellSurfaceItem {
            x: 0
            y: 0
            autoCreatePopupItems: true
            focusOnClick: true
            // Растягиваем на GPU с линейной фильтрацией, иначе при дробном
            // масштабе картинка идёт ступеньками.
            smooth: true
            onSurfaceDestroyed: {
                comp.clientCount--
                destroy()
            }
            Component.onCompleted: {
                comp.clientCount++
                forceActiveFocus()
            }
        }
    }

    XdgShell {
        onToplevelCreated: function (toplevel, xdgSurface) {
            if (!comp.surfaceArea)
                return
            toplevel.sendFullscreen(Qt.size(comp.logicalW, comp.logicalH))
            chrome.createObject(comp.surfaceArea, { shellSurface: xdgSurface })
        }
    }

    WlShell {
        onWlShellSurfaceCreated: function (shellSurface) {
            if (!comp.surfaceArea)
                return
            chrome.createObject(comp.surfaceArea, { shellSurface: shellSurface })
        }
    }
}
