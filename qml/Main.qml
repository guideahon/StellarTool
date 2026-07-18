import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import "."
import "pages"
import "components"

ApplicationWindow {
    id: win
    visible: true
    width: 1180
    height: 760
    minimumWidth: 900
    minimumHeight: 560
    title: "Stellar Tool — " + I18n.s.app_subtitle
    color: Theme.bg

    property int currentPage: 0

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.preferredWidth: 190
            Layout.fillHeight: true
            color: Theme.panel

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 6

                Label {
                    text: "Stellar Tool"
                    color: Theme.text
                    font.pixelSize: 20
                    font.bold: true
                    Layout.margins: 6
                }

                Repeater {
                    model: App.advancedMode
                           ? [
                               { key: "nav_easy", page: 0 },
                               { key: "nav_mods", page: 1 },
                               { key: "nav_changes", page: 2 },
                               { key: "nav_conflicts", page: 3 },
                               { key: "nav_merge", page: 4 },
                               { key: "nav_settings", page: 5 },
                             ]
                           : [
                               { key: "nav_easy", page: 0 },
                               { key: "nav_settings", page: 5 },
                             ]
                    delegate: Rectangle {
                        required property var modelData
                        Layout.fillWidth: true
                        height: 40
                        radius: Theme.radius
                        color: win.currentPage === modelData.page ? Theme.panelAlt : "transparent"
                        border.color: win.currentPage === modelData.page ? Theme.border : "transparent"
                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.leftMargin: 14
                            text: I18n.s[modelData.key]
                            color: win.currentPage === modelData.page ? Theme.text : Theme.textDim
                            font.pixelSize: 15
                        }
                        Rectangle {
                            visible: modelData.page === 3 && App.conflictModel.pendingCount > 0
                            anchors.right: parent.right
                            anchors.rightMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                            width: pendLabel.width + 12; height: 20; radius: 10
                            color: Theme.warn
                            Label {
                                id: pendLabel
                                anchors.centerIn: parent
                                text: App.conflictModel.pendingCount
                                color: "#14161c"
                                font.pixelSize: 12
                                font.bold: true
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: win.currentPage = modelData.page
                        }
                    }
                }

                Item { Layout.fillHeight: true }

                Label {
                    visible: !App.toolsAvailable
                    Layout.fillWidth: true
                    text: App.toolsError
                    color: Theme.danger
                    wrapMode: Text.Wrap
                    font.pixelSize: 12
                }

                // Estado del entorno: chips clickeables (van a Ajustes).
                Rectangle {
                    Layout.fillWidth: true
                    height: 26
                    radius: 13
                    color: "transparent"
                    border.color: App.hasGamePath ? Theme.border : Theme.warn
                    Label {
                        anchors.centerIn: parent
                        text: App.hasGamePath ? I18n.s.status_game_ok : I18n.s.status_game_missing
                        color: App.hasGamePath ? Theme.ok : Theme.warn
                        font.pixelSize: 12
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: win.currentPage = 5
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    height: 26
                    radius: 13
                    color: "transparent"
                    border.color: App.hasBaseline ? Theme.border : Theme.warn
                    Label {
                        anchors.centerIn: parent
                        text: App.hasBaseline ? I18n.s.status_baseline_ok : I18n.s.status_baseline_missing
                        color: App.hasBaseline ? Theme.ok : Theme.warn
                        font.pixelSize: 12
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: win.currentPage = 5
                    }
                }

                Label {
                    Layout.fillWidth: true
                    text: App.statusText
                    color: Theme.textDim
                    wrapMode: Text.Wrap
                    font.pixelSize: 12
                }
                BusyIndicator {
                    running: App.busy
                    visible: App.busy
                    Layout.alignment: Qt.AlignHCenter
                    implicitWidth: 32; implicitHeight: 32
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

                // Simple <-> Avanzado
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Switch {
                        id: modeSwitch
                        checked: App.advancedMode
                        onToggled: {
                            App.advancedMode = checked
                            if (!checked && win.currentPage !== 5) win.currentPage = 0
                        }
                        ToolTip.visible: hovered
                        ToolTip.delay: 400
                        ToolTip.text: I18n.s.mode_advanced_tip
                    }
                    Label {
                        text: I18n.s.mode_advanced
                        color: Theme.text
                        font.pixelSize: 13
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: { modeSwitch.toggle(); modeSwitch.toggled() }
                        }
                    }
                }
            }
        }

        Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: Theme.border }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: win.currentPage

            EasyMergePage { onOpenSettings: win.currentPage = 5 }
            HomePage {}
            ChangesPage {}
            ConflictsPage {}
            MergePage {}
            SettingsPage {}
        }
    }

    Dialog {
        id: errorDialog
        modal: true
        title: I18n.s.error
        anchors.centerIn: parent
        width: Math.min(640, win.width - 80)
        standardButtons: Dialog.Ok
        background: Rectangle { color: Theme.panel; border.color: Theme.border; radius: Theme.radius }
        contentItem: Label {
            text: errorDialog.errorText
            color: Theme.text
            wrapMode: Text.Wrap
        }
        property string errorText: ""
    }
    Connections {
        target: App
        function onErrorOccurred(message) {
            errorDialog.errorText = message
            errorDialog.open()
        }
    }

    // Popup de primer arranque: elegir idioma.
    LanguageDialog {
        id: langDialog
        anchors.centerIn: parent
    }
    Component.onCompleted: if (!I18n.chosen) langDialog.open()
}
