import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import ".."

Item {
    id: page
    property string inputPath: ""
    property string outputPath: ""
    property int operation: 0 // 0 tojson, 1 fromjson, 2 fix

    function localPath(url) {
        return decodeURIComponent(url.toString().replace(/^file:\/\//, "").replace(/^\//, ""))
    }
    function inputTitle() { return operation === 1 ? "Elegir JSON" : "Elegir partida (.sav)" }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        ColumnLayout {
            width: parent.width
            spacing: 14
            anchors.margins: 18

            Label { text: "Convertidor de partidas"; color: Theme.text; font.pixelSize: 22; font.bold: true }
            Label {
                Layout.fillWidth: true
                text: "Convierte partidas de Stellar Blade y mods a JSON para inspeccionarlas o editarlas, y vuelve a generarlas. Incluye una reparación para partidas CNS dañadas."
                color: Theme.textDim; wrapMode: Text.Wrap
            }

            Rectangle {
                Layout.fillWidth: true; implicitHeight: form.implicitHeight + 28
                radius: Theme.radius; color: Theme.panel; border.color: Theme.border
                ColumnLayout {
                    id: form; anchors.fill: parent; anchors.margins: 14; spacing: 10
                    Label { text: "Operación"; color: Theme.text; font.bold: true }
                    ComboBox {
                        id: operationBox; Layout.fillWidth: true
                        model: ["Partida → JSON", "JSON → partida", "Reparar partida CNS"]
                        onCurrentIndexChanged: page.operation = currentIndex
                    }
                    Label { text: "Entrada"; color: Theme.text; font.bold: true }
                    RowLayout {
                        Layout.fillWidth: true
                        TextField { Layout.fillWidth: true; text: page.inputPath; readOnly: true; color: Theme.text; placeholderText: page.inputTitle() }
                        Button { text: "Elegir…"; onClicked: inputDialog.open() }
                    }
                    RowLayout {
                        visible: page.operation !== 2; Layout.fillWidth: true
                        Label { text: "Salida"; color: Theme.text; font.bold: true }
                        TextField { Layout.fillWidth: true; text: page.outputPath; readOnly: true; color: Theme.text; placeholderText: page.operation === 0 ? "archivo.json" : "archivo.sav" }
                        Button { text: "Elegir…"; onClicked: outputDialog.open() }
                    }
                    RowLayout {
                        visible: page.operation === 0; Layout.fillWidth: true
                        Label { text: "Indentación JSON"; color: Theme.text }
                        SpinBox { id: indentBox; from: 0; to: 8; value: 2; editable: true }
                    }
                    Label {
                        Layout.fillWidth: true
                        text: page.operation === 2
                            ? "La reparación elimina AutoLoadCNS y CamPosition. Se crea un backup automático en la carpeta Backup junto a la partida antes de sobrescribirla."
                            : "Al generar una partida sobre un archivo existente, se crea un backup automático en la carpeta Backup. Verificá siempre el resultado dentro del juego."
                        color: Theme.textDim; wrapMode: Text.Wrap
                    }
                    Button {
                        Layout.alignment: Qt.AlignRight; highlighted: true
                        text: page.operation === 0 ? "Convertir a JSON" : page.operation === 1 ? "Generar partida" : "Reparar CNS"
                        enabled: !App.busy && page.inputPath.length > 0 && (page.operation === 2 || page.outputPath.length > 0)
                        onClicked: {
                            if (page.operation === 0) App.convertSaveToJson(page.fileUrl(page.inputPath), page.fileUrl(page.outputPath), indentBox.value)
                            else if (page.operation === 1) App.convertJsonToSave(page.fileUrl(page.inputPath), page.fileUrl(page.outputPath))
                            else App.fixSave(page.fileUrl(page.inputPath))
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true; implicitHeight: credit.implicitHeight + 28
                radius: Theme.radius; color: Theme.panel; border.color: Theme.border
                Label { id: credit; anchors.fill: parent; anchors.margins: 14; color: Theme.textDim; wrapMode: Text.Wrap
                    text: "Crédito: conversor original creado por lotress.\nNexus Mods: https://www.nexusmods.com/stellarblade/users/12188623\nRepositorio: https://github.com/lotress/CNSSaveConverter" }
            }
            Label { visible: App.saveConverterResult.length > 0; Layout.fillWidth: true; text: App.saveConverterResult; color: Theme.textDim; wrapMode: Text.Wrap }
            Item { Layout.fillHeight: true }
        }
    }

    function fileUrl(path) { return "file:///" + path.replace(/\\/g, "/") }
    FileDialog { id: inputDialog; title: page.inputTitle(); fileMode: FileDialog.OpenFile; onAccepted: page.inputPath = page.localPath(selectedFile) }
    FileDialog { id: outputDialog; title: "Elegir archivo de salida"; fileMode: FileDialog.SaveFile; onAccepted: page.outputPath = page.localPath(selectedFile) }
}
