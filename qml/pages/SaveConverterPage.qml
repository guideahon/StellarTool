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
    function inputTitle() { return operation === 1 ? I18n.s.save_choose_json : I18n.s.save_choose_sav }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        ColumnLayout {
            width: parent.width
            spacing: 14
            anchors.margins: 18

            Label { text: I18n.s.save_title; color: Theme.text; font.pixelSize: 22; font.bold: true }
            Label {
                Layout.fillWidth: true
                text: I18n.s.save_desc
                color: Theme.textDim; wrapMode: Text.Wrap
            }

            Rectangle {
                Layout.fillWidth: true; implicitHeight: form.implicitHeight + 28
                radius: Theme.radius; color: Theme.panel; border.color: Theme.border
                ColumnLayout {
                    id: form; anchors.fill: parent; anchors.margins: 14; spacing: 10
                    Label { text: I18n.s.operation; color: Theme.text; font.bold: true }
                    ComboBox {
                        id: operationBox; Layout.fillWidth: true
                        model: [I18n.s.save_to_json, I18n.s.save_from_json, I18n.s.save_fix]
                        onCurrentIndexChanged: page.operation = currentIndex
                    }
                    Label { text: I18n.s.input; color: Theme.text; font.bold: true }
                    RowLayout {
                        Layout.fillWidth: true
                        TextField { Layout.fillWidth: true; text: page.inputPath; readOnly: true; color: Theme.text; placeholderText: page.inputTitle() }
                        Button { text: I18n.s.choose; onClicked: inputDialog.open() }
                    }
                    RowLayout {
                        visible: page.operation !== 2; Layout.fillWidth: true
                        Label { text: I18n.s.output; color: Theme.text; font.bold: true }
                        TextField { Layout.fillWidth: true; text: page.outputPath; readOnly: true; color: Theme.text; placeholderText: page.operation === 0 ? "archivo.json" : "archivo.sav" }
                        Button { text: I18n.s.choose; onClicked: outputDialog.open() }
                    }
                    RowLayout {
                        visible: page.operation === 0; Layout.fillWidth: true
                        Label { text: I18n.s.save_indent; color: Theme.text }
                        SpinBox { id: indentBox; from: 0; to: 8; value: 2; editable: true }
                    }
                    Label {
                        Layout.fillWidth: true
                        text: page.operation === 2
                            ? I18n.s.save_fix_hint
                            : I18n.s.save_write_hint
                        color: Theme.textDim; wrapMode: Text.Wrap
                    }
                    Button {
                        Layout.alignment: Qt.AlignRight; highlighted: true
                        text: page.operation === 0 ? I18n.s.save_convert : page.operation === 1 ? I18n.s.save_generate : I18n.s.save_repair
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
                    text: I18n.s.save_credit }
            }
            Label { visible: App.saveConverterResult.length > 0; Layout.fillWidth: true; text: App.saveConverterResult; color: Theme.textDim; wrapMode: Text.Wrap }
            Item { Layout.fillHeight: true }
        }
    }

    function fileUrl(path) { return "file:///" + path.replace(/\\/g, "/") }
    FileDialog { id: inputDialog; title: page.inputTitle(); fileMode: FileDialog.OpenFile; onAccepted: page.inputPath = page.localPath(selectedFile) }
    FileDialog { id: outputDialog; title: I18n.s.choose_output_file; fileMode: FileDialog.SaveFile; onAccepted: page.outputPath = page.localPath(selectedFile) }
}
