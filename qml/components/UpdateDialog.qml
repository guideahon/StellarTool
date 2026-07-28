import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import ".."

// Aviso de nueva versión con las tres salidas: actualizar, posponer al próximo
// arranque (no persiste nada) y saltear esta versión (persiste el tag).
Dialog {
    id: root
    modal: true
    closePolicy: Popup.NoAutoClose
    width: Math.min(560, parent ? parent.width - 80 : 560)
    padding: 0
    background: Rectangle { color: Theme.panel; border.color: Theme.border; radius: Theme.radius }

    readonly property bool working: Updater.state === "downloading" || Updater.state === "extracting"
    readonly property bool failed: Updater.state === "error"

    contentItem: ColumnLayout {
        spacing: 0

        Label {
            Layout.fillWidth: true
            Layout.margins: 18
            Layout.bottomMargin: 4
            text: I18n.s.update_title
            color: Theme.text
            font.pixelSize: 20
            font.bold: true
        }

        Label {
            Layout.fillWidth: true
            Layout.leftMargin: 18
            Layout.rightMargin: 18
            text: I18n.s.update_available.arg(Updater.latestVersion).arg(Updater.currentVersion)
            color: Theme.textDim
            wrapMode: Text.Wrap
        }

        // Notas de la release (texto plano, tal cual las publica GitHub).
        Rectangle {
            Layout.fillWidth: true
            Layout.margins: 18
            Layout.bottomMargin: 0
            visible: Updater.releaseNotes.length > 0
            implicitHeight: Math.min(200, notesArea.implicitHeight + 16)
            radius: Theme.radius
            color: Theme.panelAlt
            border.color: Theme.border

            ScrollView {
                anchors.fill: parent
                anchors.margins: 8
                clip: true
                TextArea {
                    id: notesArea
                    readOnly: true
                    wrapMode: Text.Wrap
                    background: null
                    color: Theme.text
                    font.pixelSize: 13
                    text: Updater.releaseNotes
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: 18
            Layout.bottomMargin: 0
            spacing: 6
            visible: root.working

            Label {
                text: Updater.state === "extracting"
                      ? I18n.s.update_extracting
                      : I18n.s.update_downloading
                color: Theme.textDim
            }
            ProgressBar {
                Layout.fillWidth: true
                from: 0
                to: 1
                value: Updater.progress
                indeterminate: Updater.state === "extracting"
            }
        }

        Label {
            Layout.fillWidth: true
            Layout.margins: 18
            Layout.bottomMargin: 0
            visible: root.failed
            text: I18n.s.update_error.arg(Updater.errorText)
            color: Theme.danger
            wrapMode: Text.Wrap
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 18
            spacing: 8

            Button {
                text: I18n.s.update_open_page
                onClicked: Updater.openReleasePage()
            }
            Item { Layout.fillWidth: true }
            Button {
                text: I18n.s.update_skip
                enabled: !root.working
                onClicked: { Updater.skipThisVersion(); root.close() }
            }
            Button {
                text: I18n.s.update_later
                enabled: !root.working
                onClicked: { Updater.remindNextBoot(); root.close() }
            }
            Button {
                text: root.failed ? I18n.s.update_retry : I18n.s.update_now
                highlighted: true
                enabled: !root.working
                onClicked: Updater.install()
            }
        }
    }

    Connections {
        target: Updater
        // El exe nuevo ya se está copiando: el diálogo se va con la app.
        function onQuitRequested() { root.close() }
    }
}
