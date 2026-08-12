import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import ".."

Item {
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 12

        Label {
            text: I18n.s.settings_title
            color: Theme.text
            font.pixelSize: 22
            font.bold: true
        }

        // ---- Carpeta del juego + baseline ----
        Label {
            text: I18n.s.settings_game
            color: Theme.text
            font.pixelSize: 16
            font.bold: true
        }
        Label {
            text: I18n.s.settings_game_desc
            color: Theme.textDim
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Rectangle {
                Layout.fillWidth: true
                height: 34
                radius: Theme.radius
                color: Theme.panel
                border.color: Theme.border
                Label {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    verticalAlignment: Text.AlignVCenter
                    text: App.hasGamePath ? App.gamePath : I18n.s.settings_game_none
                    color: App.hasGamePath ? Theme.text : Theme.warn
                    elide: Text.ElideMiddle
                }
            }
            Button {
                text: I18n.s.settings_game_choose
                enabled: !App.busy
                onClicked: gameDialog.open()
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Button {
                text: I18n.s.settings_build_baseline
                enabled: !App.busy && App.hasGamePath && App.toolsAvailable
                highlighted: true
                onClicked: App.buildBaselineFromGame()
            }
            Label {
                text: App.hasBaseline ? I18n.s.settings_baseline_ok : I18n.s.settings_baseline_none
                color: App.hasBaseline ? Theme.ok : Theme.textDim
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

        // ---- Mappings (.usmap) override ----
        Label {
            text: I18n.s.settings_mappings
            color: Theme.text
            font.pixelSize: 16
            font.bold: true
        }
        Label {
            text: I18n.s.settings_mappings_desc
            color: Theme.textDim
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Rectangle {
                Layout.fillWidth: true
                height: 34
                radius: Theme.radius
                color: Theme.panel
                border.color: Theme.border
                Label {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    verticalAlignment: Text.AlignVCenter
                    text: App.usmapIsCustom ? App.usmapPath : I18n.s.settings_mappings_bundled
                    color: App.usmapIsCustom ? Theme.ok : Theme.textDim
                    elide: Text.ElideMiddle
                }
            }
            Button {
                text: I18n.s.settings_mappings_choose
                enabled: !App.busy
                onClicked: usmapDialog.open()
            }
            Button {
                text: I18n.s.settings_mappings_reset
                enabled: !App.busy && App.usmapIsCustom
                onClicked: App.clearUsmapPath()
            }
        }
        // Auto-descarga del usmap por versión de juego (archivo de la comunidad).
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Rectangle {
                Layout.preferredWidth: 130
                height: 34
                radius: Theme.radius
                color: Theme.panelAlt
                border.color: Theme.border
                TextField {
                    id: usmapVersionField
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    verticalAlignment: Text.AlignVCenter
                    color: Theme.text
                    background: null
                    text: App.detectedGameVersion
                    placeholderText: "1.4.1"
                }
            }
            Button {
                text: App.downloadingUsmap ? I18n.s.settings_mappings_downloading
                                           : I18n.s.settings_mappings_download
                enabled: !App.busy && !App.downloadingUsmap
                onClicked: App.downloadUsmap(usmapVersionField.text)
            }
            Label {
                id: usmapDlStatus
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.textDim
                text: ""
            }
        }
        Connections {
            target: App
            function onUsmapDownloadDone(ok, message) {
                usmapDlStatus.text = message
                usmapDlStatus.color = ok ? Theme.ok : Theme.danger
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

        // ---- Oodle (oo2core_9_win64.dll) ----
        // Nunca se distribuye (licencia): sale del juego del usuario. Cuando la
        // autodetección falla no había forma de arreglarlo desde la UI.
        Label {
            text: I18n.s.settings_oodle
            color: Theme.text
            font.pixelSize: 16
            font.bold: true
        }
        Label {
            text: I18n.s.settings_oodle_desc
            color: Theme.textDim
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        Rectangle {
            Layout.fillWidth: true
            visible: !App.oodlePath
            implicitHeight: oodleMissingText.implicitHeight + 20
            radius: Theme.radius
            color: Theme.panel
            border.color: Theme.warn
            Label {
                id: oodleMissingText
                anchors.fill: parent
                anchors.margins: 10
                text: I18n.s.settings_oodle_none + "\n" + I18n.s.settings_oodle_desc
                color: Theme.warn
                wrapMode: Text.Wrap
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Rectangle {
                Layout.fillWidth: true
                height: 34
                radius: Theme.radius
                color: Theme.panel
                border.color: Theme.border
                Label {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    verticalAlignment: Text.AlignVCenter
                    text: App.oodlePath ? (App.oodleIsCustom
                                           ? App.oodlePath
                                           : I18n.s.settings_oodle_auto.arg(App.oodlePath))
                                        : I18n.s.settings_oodle_none
                    color: App.oodlePath ? Theme.ok : Theme.warn
                    elide: Text.ElideMiddle
                }
            }
            Button {
                text: App.oodleIsCustom ? I18n.s.settings_oodle_reset : I18n.s.settings_oodle_choose
                enabled: !App.busy
                onClicked: App.oodleIsCustom ? App.clearOodlePath() : App.refreshOodle()
            }
            Button {
                text: I18n.s.settings_oodle_choose
                enabled: !App.busy
                onClicked: oodleDialog.open()
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

        Label {
            text: I18n.s.settings_language
            color: Theme.text
            font.pixelSize: 16
            font.bold: true
        }
        Label {
            text: I18n.s.settings_language_desc
            color: Theme.textDim
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 3
            columnSpacing: 8
            rowSpacing: 8
            Repeater {
                model: I18n.languages
                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    height: 44
                    radius: Theme.radius
                    color: I18n.language === modelData.code ? Theme.panelAlt : Theme.panel
                    border.color: I18n.language === modelData.code ? Theme.accent : Theme.border
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        spacing: 10
                        Rectangle {
                            width: 34; height: 22; radius: 5
                            color: I18n.language === modelData.code ? Theme.accent : Theme.border
                            Label {
                                anchors.centerIn: parent
                                text: modelData.code.split("_")[0].toUpperCase()
                                color: I18n.language === modelData.code ? Theme.accentText : Theme.text
                                font.pixelSize: 11
                                font.bold: true
                            }
                        }
                        Label {
                            text: modelData.name
                            color: Theme.text
                            font.pixelSize: 15
                            Layout.fillWidth: true
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: I18n.language = modelData.code
                    }
                }
            }
        }

        // ---- Apariencia (claro / oscuro / OLED) ----
        Label {
            text: I18n.s.settings_appearance || "Appearance"
            color: Theme.text; font.pixelSize: 16; font.bold: true
        }
        RowLayout {
            spacing: 8
            Repeater {
                model: [
                    { key: "settings_dark", mode: "dark", icon: "🌙" },
                    { key: "settings_oled", mode: "oled", icon: "◉" },
                    { key: "settings_light", mode: "light", icon: "☀" }
                ]
                delegate: Rectangle {
                    Layout.preferredWidth: 160; height: 40; radius: Theme.radius
                    color: Theme.mode === modelData.mode ? Theme.accent : Theme.panel
                    border.color: Theme.mode === modelData.mode ? Theme.accent : Theme.border
                    Label {
                        anchors.centerIn: parent
                        text: modelData.icon + " " + (I18n.s[modelData.key] || modelData.mode)
                        color: Theme.mode === modelData.mode ? Theme.accentText : Theme.text
                        font.pixelSize: 14; font.bold: true
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: App.themeMode = modelData.mode
                    }
                }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

        // ---- Actualizaciones ----
        Label {
            text: I18n.s.update_section
            color: Theme.text; font.pixelSize: 16; font.bold: true
        }
        Label {
            text: I18n.s.update_current_version.arg(Updater.currentVersion)
            color: Theme.textDim
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Switch {
                id: autoUpdateSwitch
                checked: Updater.checkOnStartup
                onToggled: Updater.checkOnStartup = checked
            }
            Label {
                text: I18n.s.update_auto
                color: Theme.text
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: { autoUpdateSwitch.toggle(); autoUpdateSwitch.toggled() }
                }
            }
            Button {
                text: Updater.state === "checking" ? I18n.s.update_checking : I18n.s.update_check_now
                enabled: !Updater.busy
                onClicked: { updateStatus.text = ""; Updater.checkForUpdates(false) }
            }
        }
        Label {
            id: updateStatus
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.textDim
            text: ""
        }
        Connections {
            target: Updater
            function onUpToDate() {
                updateStatus.text = I18n.s.update_uptodate.arg(Updater.currentVersion)
                updateStatus.color = Theme.ok
            }
            function onStateChanged() {
                if (Updater.state === "error") {
                    updateStatus.text = I18n.s.update_error.arg(Updater.errorText)
                    updateStatus.color = Theme.danger
                }
            }
        }

        // ---- Shoutouts / creditos ----
        Label {
            text: I18n.s.settings_shoutouts || "Shoutouts"
            color: Theme.text; font.pixelSize: 16; font.bold: true
        }
        Rectangle {
            Layout.fillWidth: true
            radius: Theme.radius; color: Theme.panelAlt; border.color: Theme.border
            implicitHeight: soCol.implicitHeight + 20
            ColumnLayout {
                id: soCol
                anchors.fill: parent; anchors.margins: 10; spacing: 4
                Label { text: "• repak, retoc (trumank), UAssetGUI, CUE4Parse — toolchain"; color: Theme.textDim; wrapMode: Text.Wrap; Layout.fillWidth: true }
                Label { text: "• UE4SS + CNS — outfit helper runtime"; color: Theme.textDim; wrapMode: Text.Wrap; Layout.fillWidth: true }
                Label { text: "• \"Keygen Vibes\" chiptune — generado proceduralmente para este tool, dominio publico (CC0)"; color: Theme.textDim; wrapMode: Text.Wrap; Layout.fillWidth: true }
                Label { text: "• automod (jpabscale) — inspiracion: patches declarativos TOML de propiedades .uasset y auto-merge (nexusmods.com/stellarblade/mods/987)"; color: Theme.textDim; wrapMode: Text.Wrap; Layout.fillWidth: true }
                Label { text: "• Comunidad Nexus Mods de Stellar Blade — feedback y pruebas (FengYeLy, yadilloH y otros)"; color: Theme.textDim; wrapMode: Text.Wrap; Layout.fillWidth: true }
            }
        }

        Item { Layout.fillHeight: true }
    }

    FolderDialog {
        id: gameDialog
        onAccepted: App.setGamePath(selectedFolder)
    }

    FileDialog {
        id: usmapDialog
        nameFilters: ["Unreal mappings (*.usmap)"]
        onAccepted: App.setUsmapPath(selectedFile)
    }

    FileDialog {
        id: oodleDialog
        nameFilters: ["Oodle (oo2core_9_win64.dll)"]
        onAccepted: App.setOodlePath(selectedFile)
    }
}
