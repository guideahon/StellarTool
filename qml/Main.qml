import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import "."
import "pages"
import "components"

// Ventana principal: barra lateral de navegación + StackLayout de páginas.
// La lógica vive en AppController (C++); QML solo presenta y enlaza señales.
// El sidebar cambia según advancedMode: modo fácil (EasyMerge+Builder+CNS)
// vs modo avanzado (Mods → Changes → Conflicts → Merge).
ApplicationWindow {
    id: win
    visible: true
    width: 1180
    height: 760
    minimumWidth: 900
    minimumHeight: 560
    title: "Stellar Tool — " + I18n.s.app_subtitle
    color: Theme.bg
    palette.window: Theme.bg
    palette.windowText: Theme.text
    palette.base: Theme.panel
    palette.alternateBase: Theme.panelAlt
    palette.button: Theme.panelAlt
    palette.buttonText: Theme.text
    palette.text: Theme.text
    palette.brightText: Theme.text
    palette.highlight: Theme.accent
    palette.highlightedText: Theme.accentText
    palette.placeholderText: Theme.textDim
    palette.toolTipBase: Theme.panelAlt
    palette.toolTipText: Theme.text

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
                               { key: "nav_easy", icon: "⚡", page: 0 },
                               { key: "nav_mods", icon: "📦", page: 1 },
                               { key: "nav_changes", icon: "📝", page: 2 },
                               { key: "nav_conflicts", icon: "⚔️", page: 3 },
                               { key: "nav_merge", icon: "🔀", page: 4 },
                               { key: "nav_builder", icon: "🛠️", page: 6 },
                               { key: "nav_cns", icon: "👗", page: 7 },
                               { label: "CNS ID Fixer", icon: "🆔", page: 8 },
                               { label: "Live", icon: "🎮", page: 9 },
                               { key: "nav_settings", icon: "⚙️", page: 5 },
                             ]
                           : [
                               { key: "nav_easy", icon: "⚡", page: 0 },
                               { key: "nav_builder", icon: "🛠️", page: 6 },
                               { key: "nav_cns", icon: "👗", page: 7 },
                               { label: "CNS ID Fixer", icon: "🆔", page: 8 },
                               { label: "Live", icon: "🎮", page: 9 },
                               { key: "nav_settings", icon: "⚙️", page: 5 },
                             ]
                    delegate: Rectangle {
                        required property var modelData
                        Layout.fillWidth: true
                        height: 40
                        radius: Theme.radius
                        color: win.currentPage === modelData.page ? Theme.panelAlt : "transparent"
                        border.color: win.currentPage === modelData.page ? Theme.border : "transparent"
                        RowLayout {
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.leftMargin: 12
                            anchors.right: parent.right
                            anchors.rightMargin: 8
                            spacing: 10
                            Label {
                                text: modelData.icon
                                font.pixelSize: 15
                                Layout.preferredWidth: 20
                                horizontalAlignment: Text.AlignHCenter
                            }
                            Label {
                                text: modelData.label || I18n.s[modelData.key]
                                color: win.currentPage === modelData.page ? Theme.text : Theme.textDim
                                font.pixelSize: 15
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
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
                                color: Theme.warnText
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
                    property bool blOk: App.hasBaseline && !App.baselineStale
                    Layout.fillWidth: true
                    height: 26
                    radius: 13
                    color: "transparent"
                    border.color: blOk ? Theme.border : Theme.warn
                    Label {
                        anchors.centerIn: parent
                        text: parent.blOk ? I18n.s.status_baseline_ok
                              : (App.baselineStale ? I18n.s.status_baseline_stale
                                                   : I18n.s.status_baseline_missing)
                        color: parent.blOk ? Theme.ok : Theme.warn
                        font.pixelSize: 12
                        elide: Text.ElideRight
                        width: Math.min(implicitWidth, parent.width - 8)
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
                // Solo el build largo se puede cortar (App.cancellable): el resto
                // de las tareas terminan solas en segundos.
                FlatButton {
                    id: cancelBuildButton
                    visible: App.cancellable
                    Layout.fillWidth: true
                    text: I18n.s.cancel
                    onClicked: App.cancelBuild()
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
            HomePage { onOpenSettings: win.currentPage = 5 }
            ChangesPage {}
            ConflictsPage {}
            MergePage {}
            SettingsPage {}
            BuilderPage {}
            CnsConverterPage {}
            CnsIdFixerPage {}
            LivePage { onOpenSettings: win.currentPage = 5 }
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
    Component.onCompleted: {
        if (!I18n.chosen) langDialog.open()
        else win.maybeWarnBaseline()
    }

    // Aviso de baseline faltante/vieja: una sola vez por sesión y después de los
    // popups de idioma/actualización, para no apilar diálogos al arrancar.
    property bool baselineWarned: false
    function maybeWarnBaseline() {
        if (win.baselineWarned) return
        if (langDialog.opened || updateDialog.opened || win.updatePending) return
        if (App.hasBaseline && !App.baselineStale) return
        win.baselineWarned = true
        baselineDialog.open()
    }
    BaselineDialog {
        id: baselineDialog
        anchors.centerIn: parent
        onOpenSettings: win.currentPage = 5
    }

    // Aviso de actualización (chequeo silencioso al arrancar + botón en Settings).
    UpdateDialog {
        id: updateDialog
        anchors.centerIn: parent
    }
    // En el primer arranque manda el popup de idioma: el aviso espera a que cierre.
    property bool updatePending: false
    Connections {
        target: Updater
        function onUpdateAvailable(version) {
            if (langDialog.opened) win.updatePending = true
            else updateDialog.open()
        }
    }
    Connections {
        target: langDialog
        function onClosed() {
            if (win.updatePending) {
                win.updatePending = false
                updateDialog.open()
            } else {
                win.maybeWarnBaseline()
            }
        }
    }
    Connections {
        target: updateDialog
        function onClosed() { win.maybeWarnBaseline() }
    }
}
