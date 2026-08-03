import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import ".."

// Control en vivo (fase 1): FOV, velocidad y salto mientras el juego corre.
// Todo pasa por Live (LiveService): esta página no toca el pipeline de merge.
Item {
    id: page

    signal openSettings()

    readonly property bool usable: Live.installed && Live.bridgeAlive

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth

        ColumnLayout {
            width: parent.width
            spacing: 14
            anchors.margins: 18

            Label {
                text: "Live"
                color: Theme.text
                font.pixelSize: 22
                font.bold: true
            }
            Label {
                Layout.fillWidth: true
                text: "Modificá el juego mientras corre: campo de visión, velocidad de movimiento y fuerza de salto. "
                    + "Requiere UE4SS instalado. El bridge no toca partidas guardadas, inventario ni progresión."
                color: Theme.textDim
                wrapMode: Text.Wrap
            }

            // --- Estado / instalación ---
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: setup.implicitHeight + 28
                radius: Theme.radius
                color: Theme.panel
                border.color: Theme.border

                ColumnLayout {
                    id: setup
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 10

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        Label {
                            text: "Estado del bridge"
                            color: Theme.text
                            font.bold: true
                            Layout.fillWidth: true
                        }
                        Rectangle {
                            width: stateLabel.width + 18
                            height: 24
                            radius: 12
                            color: "transparent"
                            border.color: page.usable ? Theme.ok
                                        : (Live.installed ? Theme.warn : Theme.border)
                            Label {
                                id: stateLabel
                                anchors.centerIn: parent
                                text: !App.hasGamePath ? "sin juego"
                                    : !Live.ue4ssPresent ? "sin UE4SS"
                                    : !Live.installed ? "no instalado"
                                    : Live.bridgeAlive ? (Live.ready ? "conectado" : "esperando partida")
                                    : "juego cerrado"
                                color: page.usable ? Theme.ok
                                     : (Live.installed ? Theme.warn : Theme.textDim)
                                font.pixelSize: 12
                            }
                        }
                    }

                    Label {
                        visible: !App.hasGamePath
                        Layout.fillWidth: true
                        text: "Configurá la ruta de Stellar Blade en Ajustes para poder instalar el bridge."
                        color: Theme.warn
                        wrapMode: Text.Wrap
                    }
                    Label {
                        visible: App.hasGamePath && !Live.ue4ssPresent
                        Layout.fillWidth: true
                        text: "No se encontró UE4SS en SB\\Binaries\\Win64\\ue4ss. Instalalo por separado: "
                            + "Stellar Tool no lo distribuye."
                        color: Theme.warn
                        wrapMode: Text.Wrap
                    }
                    Label {
                        visible: Live.installed && !Live.bridgeAlive
                        Layout.fillWidth: true
                        text: "Bridge instalado. Abrí el juego para conectarte; si ya estaba abierto, reinicialo "
                            + "para que UE4SS cargue el mod."
                        color: Theme.textDim
                        wrapMode: Text.Wrap
                    }
                    Label {
                        visible: Live.bridgeAlive && !Live.ready
                        Layout.fillWidth: true
                        text: "Conectado, esperando que cargue una partida."
                        color: Theme.textDim
                        wrapMode: Text.Wrap
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            visible: Live.installed
                            Layout.fillWidth: true
                            text: Live.bridgeDir
                            color: Theme.textDim
                            font.pixelSize: 11
                            elide: Text.ElideMiddle
                        }
                        Item { Layout.fillWidth: !Live.installed }
                        Button {
                            text: "Ajustes"
                            visible: !App.hasGamePath
                            onClicked: page.openSettings()
                        }
                        Button {
                            text: "Abrir carpeta"
                            visible: Live.installed
                            onClicked: Live.openBridgeDir()
                        }
                        Button {
                            text: Live.installed ? "Reinstalar bridge" : "Instalar bridge"
                            highlighted: !Live.installed
                            enabled: App.hasGamePath && Live.ue4ssPresent
                            onClicked: Live.install()
                        }
                        Button {
                            text: "Desinstalar"
                            visible: Live.installed
                            onClicked: uninstallDialog.open()
                        }
                    }
                }
            }

            // --- Controles ---
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: controls.implicitHeight + 28
                radius: Theme.radius
                color: Theme.panel
                border.color: Theme.border
                opacity: page.usable ? 1.0 : 0.5

                ColumnLayout {
                    id: controls
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 16
                    enabled: page.usable

                    // FOV
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        RowLayout {
                            Layout.fillWidth: true
                            CheckBox {
                                id: fovToggle
                                text: "Campo de visión (FOV)"
                                checked: Live.fovEnabled
                                onToggled: Live.fovEnabled = checked
                            }
                            Item { Layout.fillWidth: true }
                            Label {
                                text: Live.fov.toFixed(0) + "°"
                                color: Theme.text
                                font.bold: true
                            }
                        }
                        Slider {
                            Layout.fillWidth: true
                            enabled: fovToggle.checked
                            from: 40; to: 170; stepSize: 1
                            value: Live.fov
                            onMoved: Live.fov = value
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: fovToggle.checked && Live.fov > 100
                            text: "Arriba de 100° el juego puede mostrar escenario sin terminar o culleado: "
                                + "es un rango experimental."
                            color: Theme.warn
                            wrapMode: Text.Wrap
                            font.pixelSize: 12
                        }
                        Label {
                            visible: Live.fovProperty.length > 0
                            text: "Property: " + Live.fovProperty
                                + (Live.fovBase > 0 ? "  ·  base " + Live.fovBase.toFixed(0) + "°" : "")
                            color: Theme.textDim
                            font.pixelSize: 11
                        }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

                    // Velocidad
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: "Velocidad de movimiento"; color: Theme.text; Layout.fillWidth: true }
                            Label { text: "×" + Live.speed.toFixed(2); color: Theme.text; font.bold: true }
                        }
                        Slider {
                            Layout.fillWidth: true
                            from: 0.1; to: 5.0; stepSize: 0.05
                            value: Live.speed
                            onMoved: Live.speed = value
                        }
                        Label {
                            visible: Live.speedBase > 0
                            text: "Base del juego: " + Live.speedBase.toFixed(0)
                                + "  →  " + (Live.speedBase * Live.speed).toFixed(0)
                            color: Theme.textDim
                            font.pixelSize: 11
                        }
                    }

                    // Salto
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: "Fuerza de salto"; color: Theme.text; Layout.fillWidth: true }
                            Label { text: "×" + Live.jump.toFixed(2); color: Theme.text; font.bold: true }
                        }
                        Slider {
                            Layout.fillWidth: true
                            from: 0.1; to: 5.0; stepSize: 0.05
                            value: Live.jump
                            onMoved: Live.jump = value
                        }
                        Label {
                            visible: Live.jumpBase > 0
                            text: "Base del juego: " + Live.jumpBase.toFixed(0)
                                + "  →  " + (Live.jumpBase * Live.jump).toFixed(0)
                            color: Theme.textDim
                            font.pixelSize: 11
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: "Multiplicadores altos pueden mostrar clipping de animación o pop-in de streaming: "
                            + "el juego no está pensado para esos valores."
                        color: Theme.textDim
                        wrapMode: Text.Wrap
                        font.pixelSize: 12
                    }

                    RowLayout {
                        Layout.alignment: Qt.AlignRight
                        Button {
                            text: "Restaurar valores del juego"
                            onClicked: Live.resetAll()
                        }
                    }
                }
            }

            Label {
                visible: Live.statusMessage.length > 0
                Layout.fillWidth: true
                text: "Bridge: " + Live.statusMessage
                color: Theme.textDim
                font.family: "Consolas"
                font.pixelSize: 11
                wrapMode: Text.WrapAnywhere
            }

            Item { Layout.fillHeight: true }
        }
    }

    Dialog {
        id: uninstallDialog
        anchors.centerIn: parent
        modal: true
        title: "Desinstalar bridge"
        standardButtons: Dialog.Yes | Dialog.Cancel
        background: Rectangle {
            color: Theme.panel
            border.color: Theme.border
            radius: Theme.radius
        }
        contentItem: Label {
            text: "Se borra la carpeta StellarToolLive de los mods de UE4SS. No se toca ningún otro mod. "
                + "Los valores vuelven a los del juego al reiniciarlo. ¿Continuar?"
            color: Theme.text
            wrapMode: Text.Wrap
        }
        onAccepted: Live.uninstall()
    }

    Connections {
        target: Live
        function onErrorOccurred(message) {
            liveError.errorText = message
            liveError.open()
        }
    }
    Dialog {
        id: liveError
        property string errorText: ""
        anchors.centerIn: parent
        modal: true
        title: "Live"
        standardButtons: Dialog.Ok
        background: Rectangle {
            color: Theme.panel
            border.color: Theme.border
            radius: Theme.radius
        }
        contentItem: Label { text: liveError.errorText; color: Theme.text; wrapMode: Text.Wrap }
    }

    // La ruta del juego se puede cambiar en Ajustes mientras esta pagina existe.
    Connections {
        target: App
        function onGamePathChanged() { Live.refresh() }
    }
}
