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
                                color: I18n.language === modelData.code ? "#14161c" : Theme.text
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

        Item { Layout.fillHeight: true }
    }

    FolderDialog {
        id: gameDialog
        onAccepted: App.setGamePath(selectedFolder)
    }
}
