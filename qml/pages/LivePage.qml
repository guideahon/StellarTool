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

    function bridgeState() {
        if (!App.hasGamePath) return I18n.s.live_state_no_game
        if (!Live.ue4ssPresent) return I18n.s.live_state_no_ue4ss
        if (!Live.installed) return I18n.s.live_state_not_installed
        return Live.bridgeAlive ? (Live.ready ? I18n.s.live_state_connected : I18n.s.live_state_waiting_save)
                                : I18n.s.live_state_game_closed
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth

        ColumnLayout {
            width: parent.width
            spacing: 14
            anchors.margins: 18

            Label {
                text: I18n.s.live_title
                color: Theme.text
                font.pixelSize: 22
                font.bold: true
            }
            Label {
                Layout.fillWidth: true
                text: I18n.s.live_desc
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
                            text: I18n.s.live_bridge_status
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
                                text: page.bridgeState()
                                color: page.usable ? Theme.ok
                                     : (Live.installed ? Theme.warn : Theme.textDim)
                                font.pixelSize: 12
                            }
                        }
                    }

                    Label {
                        visible: !App.hasGamePath
                        Layout.fillWidth: true
                        text: I18n.s.live_no_game_hint
                        color: Theme.warn
                        wrapMode: Text.Wrap
                    }
                    Label {
                        visible: App.hasGamePath && !Live.ue4ssPresent
                        Layout.fillWidth: true
                        text: I18n.s.live_no_ue4ss_hint
                        color: Theme.warn
                        wrapMode: Text.Wrap
                    }
                    Label {
                        visible: Live.installed && !Live.bridgeAlive
                        Layout.fillWidth: true
                        text: I18n.s.live_installed_hint
                        color: Theme.textDim
                        wrapMode: Text.Wrap
                    }
                    Label {
                        visible: Live.bridgeAlive && !Live.ready
                        Layout.fillWidth: true
                        text: I18n.s.live_waiting_hint
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
                            text: I18n.s.settings
                            visible: !App.hasGamePath
                            onClicked: page.openSettings()
                        }
                        Button {
                            text: I18n.s.open_folder
                            visible: Live.installed
                            onClicked: Live.openBridgeDir()
                        }
                        Button {
                            text: Live.installed ? I18n.s.live_reinstall : I18n.s.live_install
                            highlighted: !Live.installed
                            enabled: App.hasGamePath && Live.ue4ssPresent
                            onClicked: Live.install()
                        }
                        Button {
                            text: I18n.s.uninstall
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
                                text: I18n.s.live_fov
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
                            text: I18n.s.live_fov_warning
                            color: Theme.warn
                            wrapMode: Text.Wrap
                            font.pixelSize: 12
                        }
                        Label {
                            visible: Live.fovProperty.length > 0
                            text: I18n.s.live_property + ": " + Live.fovProperty
                                + (Live.fovBase > 0 ? "  ·  " + I18n.s.live_base + " " + Live.fovBase.toFixed(0) + "°" : "")
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
                            Label { text: I18n.s.live_speed; color: Theme.text; Layout.fillWidth: true }
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
                            text: I18n.s.live_game_base + ": " + Live.speedBase.toFixed(0)
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
                            Label { text: I18n.s.live_jump; color: Theme.text; Layout.fillWidth: true }
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
                            text: I18n.s.live_game_base + ": " + Live.jumpBase.toFixed(0)
                                + "  →  " + (Live.jumpBase * Live.jump).toFixed(0)
                            color: Theme.textDim
                            font.pixelSize: 11
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: I18n.s.live_multiplier_warning
                        color: Theme.textDim
                        wrapMode: Text.Wrap
                        font.pixelSize: 12
                    }

                    RowLayout {
                        Layout.alignment: Qt.AlignRight
                        Button {
                            text: I18n.s.live_reset
                            onClicked: Live.resetAll()
                        }
                    }
                }
            }

            Label {
                visible: Live.statusMessage.length > 0
                Layout.fillWidth: true
                text: I18n.s.live_bridge + ": " + Live.statusMessage
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
        title: I18n.s.live_uninstall_title
        standardButtons: Dialog.Yes | Dialog.Cancel
        background: Rectangle {
            color: Theme.panel
            border.color: Theme.border
            radius: Theme.radius
        }
        contentItem: Label {
            text: I18n.s.live_uninstall_confirm
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
        title: I18n.s.live_title
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
