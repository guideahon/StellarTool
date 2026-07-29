import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import ".."

Item {
    id: page
    property string modsPath: App.defaultOutDir()

    function folderUrl(path) {
        return Qt.resolvedUrl("file:///" + path.replace(/\\/g, "/"))
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth

        ColumnLayout {
            width: parent.width
            spacing: 14
            anchors.margins: 18

            Label {
                text: "CNS ID Fixer"
                color: Theme.text
                font.pixelSize: 22
                font.bold: true
            }
            Label {
                Layout.fillWidth: true
                text: "Escanea mods IoStore, corrige Container_Id duplicados y reporta recursos con el mismo Package_Id. Compatible con instalaciones directas y carpetas administradas por MO2."
                color: Theme.textDim
                wrapMode: Text.Wrap
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: form.implicitHeight + 28
                radius: Theme.radius
                color: Theme.panel
                border.color: Theme.border

                ColumnLayout {
                    id: form
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 10

                    Label {
                        text: "Carpeta de mods"
                        color: Theme.text
                        font.bold: true
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        TextField {
                            Layout.fillWidth: true
                            text: page.modsPath
                            placeholderText: "Ej.: ...\\SB\\Content\\Paks\\~mods"
                            color: Theme.text
                            onTextEdited: page.modsPath = text
                        }
                        Button {
                            text: "Elegir…"
                            onClicked: folderDialog.open()
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: "Analizar no modifica nada. Corregir conserva el primer Container_Id de cada grupo, crea un .cnsidfixer.bak junto a cada archivo modificado y verifica el resultado. Los Package_Id nunca se alteran."
                        color: Theme.textDim
                        wrapMode: Text.Wrap
                    }

                    RowLayout {
                        Layout.alignment: Qt.AlignRight
                        Button {
                            text: "Analizar"
                            enabled: !App.busy && page.modsPath.length > 0
                            onClicked: App.runCnsIdFixer(page.folderUrl(page.modsPath), false)
                        }
                        Button {
                            text: "Corregir duplicados"
                            highlighted: true
                            enabled: !App.busy && page.modsPath.length > 0
                            onClicked: confirmDialog.open()
                        }
                    }
                }
            }

            Rectangle {
                visible: App.cnsIdFixerReport.length > 0
                Layout.fillWidth: true
                implicitHeight: reportLabel.implicitHeight + 28
                radius: Theme.radius
                color: Theme.panel
                border.color: Theme.border
                Label {
                    id: reportLabel
                    anchors.fill: parent
                    anchors.margins: 14
                    text: App.cnsIdFixerReport
                    color: Theme.text
                    font.family: "Consolas"
                    wrapMode: Text.WrapAnywhere
                }
            }
            Item { Layout.fillHeight: true }
        }
    }

    FolderDialog {
        id: folderDialog
        title: "Elegir carpeta de mods"
        onAccepted: page.modsPath = decodeURIComponent(
            selectedFolder.toString().replace("file:///", ""))
    }

    Dialog {
        id: confirmDialog
        anchors.centerIn: parent
        modal: true
        title: "Corregir Container_Id duplicados"
        standardButtons: Dialog.Yes | Dialog.Cancel
        background: Rectangle {
            color: Theme.panel
            border.color: Theme.border
            radius: Theme.radius
        }
        contentItem: Label {
            text: "Se modificarán solamente los .utoc con Container_Id duplicado. Se creará un backup recuperable antes de cada cambio. ¿Continuar?"
            color: Theme.text
            wrapMode: Text.Wrap
        }
        onAccepted: App.runCnsIdFixer(page.folderUrl(page.modsPath), true)
    }
}
