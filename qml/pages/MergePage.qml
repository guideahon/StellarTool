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
            text: "Merge"
            color: Theme.text
            font.pixelSize: 22
            font.bold: true
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: summaryCol.height + 24
            radius: Theme.radius
            color: Theme.panel
            border.color: Theme.border

            ColumnLayout {
                id: summaryCol
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 12
                spacing: 4
                Label {
                    text: "Cambios detectados: " + App.changeModel.totalCount
                    color: Theme.text
                }
                Label {
                    text: "Conflictos sin resolver: " + App.conflictModel.pendingCount
                    color: App.conflictModel.pendingCount > 0 ? Theme.warn : Theme.ok
                }
                Label {
                    text: "Salida: zzz_StellarTool_Merged.pak (el prefijo zzz asegura prioridad de carga)"
                    color: Theme.textDim
                    font.pixelSize: 12
                }
                Label {
                    visible: !App.hasBaseline
                    text: "Sin baseline: las tablas parten del mod de mayor prioridad y se aplican las selecciones encima."
                    color: Theme.warn
                    font.pixelSize: 12
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
            }
        }

        RowLayout {
            spacing: 8
            TextField {
                id: outDir
                Layout.fillWidth: true
                placeholderText: "Carpeta destino (ej: ...\\StellarBlade\\SB\\Content\\Paks\\~mods)"
                color: Theme.text
                background: Rectangle { color: Theme.panel; border.color: Theme.border; radius: Theme.radius }
            }
            Button {
                text: "Elegir..."
                onClicked: outDirDialog.open()
            }
        }

        CheckBox {
            id: zipCheck
            checked: App.exportZip
            onToggled: App.exportZip = checked
            text: "Generar también .zip instalable (Paks\\ + readme, para Vortex u otro mod manager)"
            contentItem: Label {
                text: zipCheck.text
                color: Theme.text
                leftPadding: zipCheck.indicator.width + 6
                verticalAlignment: Text.AlignVCenter
                wrapMode: Text.Wrap
            }
        }

        RowLayout {
            spacing: 8
            Button {
                text: "Generar pak mergeado"
                highlighted: true
                enabled: !App.busy && App.analyzed && App.conflictModel.pendingCount === 0
                          && outDir.text.length > 0
                onClicked: App.merge("file:///" + outDir.text.replace(/\\/g, "/"))
            }
            Button {
                text: "Guardar proyecto..."
                enabled: App.analyzed
                onClicked: saveDialog.open()
            }
            Button {
                text: "Cargar proyecto..."
                enabled: !App.busy
                onClicked: loadDialog.open()
            }
            Item { Layout.fillWidth: true }
        }

        Label {
            text: App.lastMergeResult
            color: App.lastMergeResult.startsWith("OK") ? Theme.ok : Theme.danger
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        Item { Layout.fillHeight: true }
    }

    FolderDialog {
        id: outDirDialog
        onAccepted: {
            let p = selectedFolder.toString().replace("file:///", "")
            outDir.text = decodeURIComponent(p).replace(/\//g, "\\")
        }
    }
    FileDialog {
        id: saveDialog
        fileMode: FileDialog.SaveFile
        nameFilters: ["Proyecto Stellar Tool (*.stproj)"]
        defaultSuffix: "stproj"
        onAccepted: App.saveProject(selectedFile)
    }
    FileDialog {
        id: loadDialog
        nameFilters: ["Proyecto Stellar Tool (*.stproj)"]
        onAccepted: App.loadProject(selectedFile)
    }
}
