import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import ".."
import "../components"

Item {
    id: page
    // inputPaths: una entrada = comportamiento de siempre; varias = un mod
    // exportado por cada una. inputPath es la primera, para el campo de texto.
    property var inputPaths: []
    property string inputPath: inputPaths.length > 0 ? inputPaths[0] : ""
    property string outputPath: App.defaultBuildOutDir()
    property bool toReplacer: mode.currentIndex === 1
    property bool advancedReplacerOptions: false
    property var historyItems: []

    function refreshHistory() {
        try {
            historyItems = JSON.parse(App.cnsHistory())
        } catch (e) {
            historyItems = []
        }
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth

        ColumnLayout {
            width: parent.width
            spacing: 14
            anchors.margins: 18

            Label {
                text: I18n.s.cns_title
                color: Theme.text
                font.pixelSize: 22
                font.bold: true
            }
            Label {
                Layout.fillWidth: true
                text: I18n.s.cns_desc
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

                    Label { text: I18n.s.direction; color: Theme.text; font.bold: true }
                    FieldCombo {
                        id: mode
                        Layout.fillWidth: true
                        currentIndex: 1
                        model: [I18n.s.cns_to_cns, I18n.s.cns_to_replacer]
                        onActivated: function(index) {
                            if (index === 1) {
                                page.inputPaths = []
                                page.advancedReplacerOptions = false
                                selection.clear()
                            }
                        }
                    }

                    Label {
                        text: page.toReplacer
                              ? I18n.s.cns_replacer_input_label : I18n.s.cns_input_mods
                        color: Theme.text
                        font.bold: true
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        TextField {
                            Layout.fillWidth: true
                            text: page.inputPaths.length > 1
                                  ? page.inputPaths.length + " " + I18n.s.selected_mods
                                  : page.inputPath
                            readOnly: page.toReplacer || page.inputPaths.length > 1
                            placeholderText: page.toReplacer
                                              ? I18n.s.cns_zip_placeholder
                                              : I18n.s.cns_input_placeholder
                            color: Theme.text
                            onTextEdited: page.inputPaths = text.length > 0 ? [text] : []
                        }
                        Button {
                            text: page.toReplacer ? I18n.s.cns_choose_zip : I18n.s.files
                            onClicked: inputFile.open()
                        }
                        Button {
                            visible: !page.toReplacer
                            text: I18n.s.folder
                            onClicked: inputFolder.open()
                        }
                    }
                    Label {
                        visible: !page.toReplacer && page.inputPaths.length > 1
                        Layout.fillWidth: true
                        text: page.inputPaths.map(function (p) {
                            return "• " + p.replace(/^.*[\\\/]/, "")
                        }).join("\n")
                        color: Theme.textDim
                        wrapMode: Text.Wrap
                    }

                    Label {
                        visible: !page.toReplacer && page.inputPaths.length <= 1
                        text: I18n.s.visible_name
                        color: Theme.text
                        font.bold: true
                    }
                    TextField {
                        id: modName
                        visible: !page.toReplacer && page.inputPaths.length <= 1
                        Layout.fillWidth: true
                        placeholderText: I18n.s.visible_name_placeholder
                        color: Theme.text
                    }
                    Label {
                        visible: !page.toReplacer && page.inputPaths.length > 1
                        Layout.fillWidth: true
                        text: I18n.s.multiple_mods_hint
                        color: Theme.textDim
                        wrapMode: Text.Wrap
                    }

                    Label {
                        visible: page.toReplacer
                        text: page.advancedReplacerOptions
                              ? I18n.s.cns_replacer_target_label
                              : I18n.s.replacement_outfit
                        color: Theme.text
                        font.bold: true
                    }
                    FieldCombo {
                        id: replacement
                        visible: page.toReplacer
                        Layout.fillWidth: true
                        model: [page.advancedReplacerOptions
                                    ? I18n.s.cns_select_target_asset
                                    : I18n.s.cns_select_default_outfit].concat(
                                   App.cnsReplacementNames(!page.advancedReplacerOptions))
                    }

                    Button {
                        visible: page.toReplacer
                        text: page.advancedReplacerOptions
                              ? I18n.s.cns_replacer_advanced_hide
                              : I18n.s.cns_replacer_advanced
                        flat: true
                        onClicked: {
                            page.advancedReplacerOptions = !page.advancedReplacerOptions
                            replacement.currentIndex = 0
                        }
                    }

                    Label {
                        visible: !page.toReplacer
                                  || page.advancedReplacerOptions
                        text: page.toReplacer
                              ? I18n.s.cns_source_outfit_label : I18n.s.cns_variant
                        color: Theme.text
                        font.bold: true
                    }
                    TextField {
                        id: selection
                        visible: !page.toReplacer || page.advancedReplacerOptions
                        Layout.fillWidth: true
                        placeholderText: page.toReplacer
                                          ? I18n.s.cns_source_outfit_placeholder
                                          : I18n.s.cns_variant_placeholder
                        color: Theme.text
                    }

                    Label {
                        visible: !page.toReplacer || page.advancedReplacerOptions
                        text: I18n.s.output_folder
                        color: Theme.text
                        font.bold: true
                    }
                    RowLayout {
                        visible: !page.toReplacer || page.advancedReplacerOptions
                        Layout.fillWidth: true
                        TextField {
                            Layout.fillWidth: true
                            text: page.outputPath
                            color: Theme.text
                            onTextEdited: page.outputPath = text
                        }
                        Button { text: I18n.s.choose; onClicked: outputFolder.open() }
                        Button {
                            text: I18n.s.open_folder
                            onClicked: App.openCnsOutputDir()
                        }
                    }
                    Label {
                        visible: page.toReplacer && !page.advancedReplacerOptions
                        Layout.fillWidth: true
                        text: I18n.s.cns_replacer_output_note.arg(page.outputPath)
                        color: Theme.textDim
                        wrapMode: Text.Wrap
                    }

                    Button {
                        Layout.alignment: Qt.AlignRight
                        text: (page.toReplacer ? I18n.s.cns_create_replacer : I18n.s.convert_to_cns)
                              + (page.inputPaths.length > 1
                                 ? " (" + page.inputPaths.length + ")" : "")
                        highlighted: true
                        enabled: !App.busy
                                 && (page.toReplacer
                                     ? page.inputPaths.length === 1
                                       && page.inputPath.toLowerCase().endsWith(".zip")
                                     : page.inputPaths.length > 0)
                                 && page.outputPath.length > 0
                                 && (!page.toReplacer || replacement.currentIndex > 0)
                        onClicked: App.convertCnsBatch(
                            page.inputPaths.map(function (p) {
                                return Qt.resolvedUrl("file:///" + p.replace(/\\/g, "/"))
                            }),
                            Qt.resolvedUrl("file:///" + page.outputPath.replace(/\\/g, "/")),
                            page.toReplacer ? "" : modName.text,
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

            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: I18n.s.cns_history
                    color: Theme.text
                    font.pixelSize: 18
                    font.bold: true
                    Layout.fillWidth: true
                }
                Button {
                    text: I18n.s.open_folder
                    onClicked: App.openCnsOutputDir()
                }
            }

            Label {
                visible: page.historyItems.length === 0
                Layout.fillWidth: true
                text: I18n.s.cns_empty
                color: Theme.textDim
            }

            Repeater {
                model: page.historyItems
                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: historyRow.implicitHeight + 20
                    radius: Theme.radius
                    color: Theme.panel
                    border.color: Theme.border

                    RowLayout {
                        id: historyRow
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 12
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Label {
                                text: modelData.name || "CNS"
                                color: Theme.text
                                font.bold: true
                            }
                            Label {
                                Layout.fillWidth: true
                                text: (modelData.timestamp || "").replace("T", " ")
                                      + " · " + (modelData.direction || "")
                                      + " · " + (modelData.assets || 0) + " assets"
                                color: Theme.textDim
                                elide: Text.ElideRight
                            }
                            Label {
                                Layout.fillWidth: true
                                text: modelData.zip || ""
                                color: Theme.textDim
                                elide: Text.ElideMiddle
                            }
                        }
                        Button {
                            text: I18n.s.open_folder
                            onClicked: App.openDir(
                                (modelData.zip || "").replace(/[\\\\/][^\\\\/]+$/, ""))
                        }
                    }
                }
            }
            Item { Layout.fillHeight: true }
        }
    }

    FileDialog {
        id: inputFile
        title: page.toReplacer ? I18n.s.cns_choose_zip : I18n.s.choose_mods
        fileMode: page.toReplacer ? FileDialog.OpenFile : FileDialog.OpenFiles
        nameFilters: page.toReplacer
                     ? [I18n.s.cns_zip_filter, I18n.s.cns_all_files_filter]
                     : [I18n.s.cns_mods_filter, I18n.s.cns_all_files_filter]
        onAccepted: {
            if (page.toReplacer) {
                page.inputPaths = [decodeURIComponent(selectedFile.toString().replace("file:///", ""))]
                replacement.currentIndex = 0
                selection.clear()
            } else {
                page.inputPaths = selectedFiles.map(function (f) {
                    return decodeURIComponent(f.toString().replace("file:///", ""))
                })
            }
        }
    }
    FolderDialog {
        id: inputFolder
        title: I18n.s.choose_mod_folder
        onAccepted: page.inputPaths = [decodeURIComponent(
                        selectedFolder.toString().replace("file:///", ""))]
    }
    FolderDialog {
        id: outputFolder
        title: I18n.s.choose_output_folder
        onAccepted: page.outputPath = decodeURIComponent(
                        selectedFolder.toString().replace("file:///", ""))
    }

    Component.onCompleted: refreshHistory()
    Connections {
        target: App
        function onCnsHistoryChanged() { page.refreshHistory() }
    }
}
