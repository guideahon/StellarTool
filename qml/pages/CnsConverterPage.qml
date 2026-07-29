import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import ".."

Item {
    id: page
    property string inputPath: ""
    property string outputPath: App.defaultBuildOutDir()
    property bool toReplacer: mode.currentIndex === 1

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth

        ColumnLayout {
            width: parent.width
            spacing: 14
            anchors.margins: 18

            Label {
                text: "CNS Converter"
                color: Theme.text
                font.pixelSize: 22
                font.bold: true
            }
            Label {
                Layout.fillWidth: true
                text: "Convierte outfits replacer a Custom Nanosuit System y outfits CNS a un reemplazo vanilla. Los mods de origen nunca se modifican."
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

                    Label { text: "Dirección"; color: Theme.text; font.bold: true }
                    ComboBox {
                        id: mode
                        Layout.fillWidth: true
                        model: ["Replacer → CNS", "CNS → replacer"]
                    }

                    Label { text: "Mod de entrada (.zip, .utoc, .pak o carpeta)"; color: Theme.text; font.bold: true }
                    RowLayout {
                        Layout.fillWidth: true
                        TextField {
                            Layout.fillWidth: true
                            text: page.inputPath
                            placeholderText: "Elegí el mod..."
                            color: Theme.text
                            onTextEdited: page.inputPath = text
                        }
                        Button { text: "Archivo…"; onClicked: inputFile.open() }
                        Button { text: "Carpeta…"; onClicked: inputFolder.open() }
                    }

                    Label { text: "Nombre visible"; color: Theme.text; font.bold: true }
                    TextField {
                        id: modName
                        Layout.fillWidth: true
                        placeholderText: "Se deriva del nombre del archivo si queda vacío"
                        color: Theme.text
                    }

                    Label {
                        visible: page.toReplacer
                        text: "Outfit vanilla que reemplaza"
                        color: Theme.text
                        font.bold: true
                    }
                    ComboBox {
                        id: replacement
                        visible: page.toReplacer
                        Layout.fillWidth: true
                        model: App.cnsReplacementNames()
                        editable: true
                    }

                    Label {
                        visible: page.toReplacer
                        text: "Variante CNS (opcional: nombre o número)"
                        color: Theme.text
                        font.bold: true
                    }
                    TextField {
                        id: selection
                        visible: page.toReplacer
                        Layout.fillWidth: true
                        placeholderText: "Vacío = primera variante"
                        color: Theme.text
                    }

                    Label { text: "Carpeta de salida"; color: Theme.text; font.bold: true }
                    RowLayout {
                        Layout.fillWidth: true
                        TextField {
                            Layout.fillWidth: true
                            text: page.outputPath
                            color: Theme.text
                            onTextEdited: page.outputPath = text
                        }
                        Button { text: "Elegir…"; onClicked: outputFolder.open() }
                    }

                    Button {
                        Layout.alignment: Qt.AlignRight
                        text: page.toReplacer ? "Convertir a replacer" : "Convertir a CNS"
                        highlighted: true
                        enabled: !App.busy && page.inputPath.length > 0
                                 && page.outputPath.length > 0
                                 && (!page.toReplacer || replacement.currentText.length > 0)
                        onClicked: App.convertCns(
                            Qt.resolvedUrl("file:///" + page.inputPath.replace(/\\/g, "/")),
                            Qt.resolvedUrl("file:///" + page.outputPath.replace(/\\/g, "/")),
                            modName.text,
                            page.toReplacer ? "replacer" : "cns",
                            replacement.currentText,
                            selection.text)
                    }
                }
            }

            Label {
                visible: App.lastCnsResult.length > 0
                Layout.fillWidth: true
                text: App.lastCnsResult
                color: Theme.text
                wrapMode: Text.Wrap
            }
            Item { Layout.fillHeight: true }
        }
    }

    FileDialog {
        id: inputFile
        title: "Elegir mod"
        nameFilters: ["Mods (*.zip *.pak *.utoc)", "Todos los archivos (*)"]
        onAccepted: page.inputPath = decodeURIComponent(
                        selectedFile.toString().replace("file:///", ""))
    }
    FolderDialog {
        id: inputFolder
        title: "Elegir carpeta del mod"
        onAccepted: page.inputPath = decodeURIComponent(
                        selectedFolder.toString().replace("file:///", ""))
    }
    FolderDialog {
        id: outputFolder
        title: "Elegir carpeta de salida"
        onAccepted: page.outputPath = decodeURIComponent(
                        selectedFolder.toString().replace("file:///", ""))
    }
}
