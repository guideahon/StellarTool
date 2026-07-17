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
            text: "Mods cargados"
            color: Theme.text
            font.pixelSize: 22
            font.bold: true
        }
        Label {
            text: "Agregá mods (.pak, .zip o carpeta). El orden define la prioridad por defecto: el primero gana."
            color: Theme.textDim
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        // Drop zone
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 90
            radius: Theme.radius
            color: dropArea.containsDrag ? Theme.panelAlt : Theme.panel
            border.color: dropArea.containsDrag ? Theme.accent : Theme.border
            border.width: 2

            Label {
                anchors.centerIn: parent
                text: "Arrastrá acá un .pak / .zip / carpeta, o usá los botones"
                color: Theme.textDim
            }
            DropArea {
                id: dropArea
                anchors.fill: parent
                onDropped: (drop) => {
                    for (let i = 0; i < drop.urls.length; ++i)
                        App.addMod(drop.urls[i])
                }
            }
        }

        RowLayout {
            spacing: 8
            Button {
                text: "Agregar archivo..."
                enabled: !App.busy
                onClicked: fileDialog.open()
            }
            Button {
                text: "Agregar carpeta..."
                enabled: !App.busy
                onClicked: folderDialog.open()
            }
            Item { Layout.fillWidth: true }
            Button {
                text: "Importar baseline..."
                enabled: !App.busy
                onClicked: baselineDialog.open()
                ToolTip.visible: hovered
                ToolTip.text: "Carpeta con JSONs de tablas vanilla (dump de UAssetGUI/FModel)"
            }
            Button {
                text: "Analizar cambios"
                enabled: !App.busy && App.modModel.rowCount() > 0 && App.toolsAvailable
                highlighted: true
                onClicked: App.analyze()
            }
        }

        // Lista de mods
        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: App.modModel
            spacing: 6
            clip: true
            delegate: Rectangle {
                required property string name
                required property string source
                required property int tableCount
                required property int otherCount
                required property int unreadableCount
                required property int index
                width: ListView.view.width
                height: 64
                radius: Theme.radius
                color: Theme.panel
                border.color: Theme.border

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 12
                    Label {
                        text: (index + 1) + "."
                        color: Theme.accent
                        font.bold: true
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Label { text: name; color: Theme.text; font.pixelSize: 16; font.bold: true }
                        Label {
                            text: tableCount + " tablas · " + otherCount + " otros assets"
                                  + (unreadableCount > 0 ? " · " + unreadableCount + " no analizables" : "")
                            color: unreadableCount > 0 ? Theme.warn : Theme.textDim
                            font.pixelSize: 12
                        }
                    }
                    Button {
                        text: "Quitar"
                        enabled: !App.busy
                        onClicked: App.removeMod(index)
                    }
                }
            }
            Label {
                anchors.centerIn: parent
                visible: parent.count === 0
                text: "Todavía no hay mods cargados"
                color: Theme.textDim
            }
        }
    }

    FileDialog {
        id: fileDialog
        nameFilters: ["Mods (*.pak *.zip)"]
        onAccepted: App.addMod(selectedFile)
    }
    FolderDialog {
        id: folderDialog
        onAccepted: App.addMod(selectedFolder)
    }
    FolderDialog {
        id: baselineDialog
        onAccepted: App.importBaseline(selectedFolder)
    }
}
