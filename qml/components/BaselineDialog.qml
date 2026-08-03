import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import ".."

// Aviso al arrancar cuando falta la baseline vanilla (o quedó vieja).
// Sale directo a construirla si el juego ya está configurado; si no, manda a Ajustes.
Dialog {
    id: root
    modal: true
    closePolicy: Popup.NoAutoClose
    width: Math.min(560, parent ? parent.width - 80 : 560)
    padding: 0
    background: Rectangle { color: Theme.panel; border.color: Theme.border; radius: Theme.radius }

    signal openSettings()

    readonly property bool canBuild: App.hasGamePath && App.toolsAvailable

    contentItem: ColumnLayout {
        spacing: 0

        Label {
            Layout.fillWidth: true
            Layout.margins: 18
            Layout.bottomMargin: 4
            text: I18n.s.baseline_prompt_title
            color: Theme.text
            font.pixelSize: 20
            font.bold: true
        }

        Label {
            Layout.fillWidth: true
            Layout.leftMargin: 18
            Layout.rightMargin: 18
            text: App.baselineStale ? I18n.s.baseline_prompt_body_stale
                                    : I18n.s.baseline_prompt_body
            color: Theme.textDim
            wrapMode: Text.Wrap
        }

        Label {
            Layout.fillWidth: true
            Layout.leftMargin: 18
            Layout.rightMargin: 18
            Layout.topMargin: 10
            visible: !root.canBuild
            text: I18n.s.baseline_prompt_no_game
            color: Theme.warn
            wrapMode: Text.Wrap
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 18
            spacing: 8

            Item { Layout.fillWidth: true }
            Button {
                text: I18n.s.baseline_prompt_later
                onClicked: root.close()
            }
            Button {
                text: root.canBuild ? I18n.s.baseline_prompt_build
                                    : I18n.s.baseline_prompt_settings
                highlighted: true
                onClicked: {
                    if (root.canBuild) App.buildBaselineFromGame()
                    else root.openSettings()
                    root.close()
                }
            }
        }
    }
}
