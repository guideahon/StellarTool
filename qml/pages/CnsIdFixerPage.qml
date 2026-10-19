import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import ".."

Item {
    id: page
    property string modsPath: App.defaultOutDir()
    property string appearanceInput: ""
    property string appearanceOutput: App.defaultBuildOutDir()
    property string appearanceRepairResult: ""
    property bool appearanceRepairRunning: false

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
                text: I18n.s.cns_fixer_title
                color: Theme.text
                font.pixelSize: 22
                font.bold: true
            }
            Label {
                Layout.fillWidth: true
                text: I18n.s.cns_fixer_desc
                color: Theme.textDim
                wrapMode: Text.Wrap
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: appearanceForm.implicitHeight + 28
                radius: Theme.radius
                color: Theme.panel
                border.color: Theme.border

                ColumnLayout {
                    id: appearanceForm
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 10

                    Label {
                        text: I18n.s.cns_fixer_appearance_title
                        color: Theme.text
                        font.pixelSize: 17
                        font.bold: true
                    }
                    Label {
                        Layout.fillWidth: true
                        text: I18n.s.cns_fixer_appearance_desc
                        color: Theme.textDim
                        wrapMode: Text.Wrap
                    }

                    Label {
                        text: I18n.s.cns_fixer_appearance_input
                        color: Theme.text
                        font.bold: true
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        TextField {
                            Layout.fillWidth: true
                            text: page.appearanceInput
                            placeholderText: I18n.s.cns_input_placeholder
                            color: Theme.text
                            onTextEdited: page.appearanceInput = text
                        }
                        Button {
                            text: I18n.s.files
                            onClicked: appearanceFileDialog.open()
                        }
                        Button {
                            text: I18n.s.folder
                            onClicked: appearanceFolderDialog.open()
                        }
                    }

                    Label {
                        text: I18n.s.output_folder
                        color: Theme.text
                        font.bold: true
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        TextField {
                            Layout.fillWidth: true
                            text: page.appearanceOutput
                            color: Theme.text
                            onTextEdited: page.appearanceOutput = text
                        }
                        Button {
                            text: I18n.s.choose
                            onClicked: appearanceOutputDialog.open()
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: I18n.s.cns_fixer_appearance_note
                        color: Theme.textDim
                        wrapMode: Text.Wrap
                    }
                    Button {
                        Layout.alignment: Qt.AlignRight
                        text: I18n.s.cns_fixer_appearance_button
                        highlighted: true
                        enabled: !App.busy && page.appearanceInput.length > 0
                                 && page.appearanceOutput.length > 0
                        onClicked: {
                            page.appearanceRepairResult = ""
                            page.appearanceRepairRunning = true
                            var sourceName = page.appearanceInput
                                    .replace(/[\\/]+$/, "")
                                    .replace(/^.*[\\/]/, "")
                                    .replace(/\.[^.]+$/, "")
                            if (sourceName.length === 0) sourceName = "Outfit"
                            var stamp = new Date().toISOString()
                                    .replace(/[^0-9]/g, "").slice(0, 17)
                            App.convertCns(
                                page.folderUrl(page.appearanceInput),
                                page.folderUrl(page.appearanceOutput),
                                sourceName + "_StellarTool_Fixed_" + stamp,
                                "cns")
                        }
                    }
                    Label {
                        visible: page.appearanceRepairResult.length > 0
                        Layout.fillWidth: true
                        text: page.appearanceRepairResult
                        color: Theme.text
                        wrapMode: Text.Wrap
                    }
                }
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
                        text: I18n.s.mod_folder
                        color: Theme.text
                        font.bold: true
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        TextField {
                            Layout.fillWidth: true
                            text: page.modsPath
                            placeholderText: I18n.s.mods_folder_placeholder
                            color: Theme.text
                            onTextEdited: page.modsPath = text
                        }
                        Button {
                            text: I18n.s.choose
                            onClicked: folderDialog.open()
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: I18n.s.cns_fixer_hint
                        color: Theme.textDim
                        wrapMode: Text.Wrap
                    }

                    RowLayout {
                        Layout.alignment: Qt.AlignRight
                        Button {
                            text: I18n.s.analyze
                            enabled: !App.busy && page.modsPath.length > 0
                            onClicked: App.runCnsIdFixer(page.folderUrl(page.modsPath), false)
                        }
                        Button {
                            text: I18n.s.fix_duplicates
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
        title: I18n.s.choose_mod_folder
        onAccepted: page.modsPath = decodeURIComponent(
            selectedFolder.toString().replace("file:///", ""))
    }

    FileDialog {
        id: appearanceFileDialog
        title: I18n.s.choose_mods
        fileMode: FileDialog.OpenFile
        nameFilters: [I18n.s.cns_fixer_mod_filter, I18n.s.all_files_filter]
        onAccepted: page.appearanceInput = decodeURIComponent(
            selectedFile.toString().replace("file:///", ""))
    }

    FolderDialog {
        id: appearanceFolderDialog
        title: I18n.s.choose_mod_folder
        onAccepted: page.appearanceInput = decodeURIComponent(
            selectedFolder.toString().replace("file:///", ""))
    }

    FolderDialog {
        id: appearanceOutputDialog
        title: I18n.s.choose_output_folder
        onAccepted: page.appearanceOutput = decodeURIComponent(
            selectedFolder.toString().replace("file:///", ""))
    }

    Dialog {
        id: confirmDialog
        anchors.centerIn: parent
        modal: true
        title: I18n.s.fix_duplicates_title
        standardButtons: Dialog.Yes | Dialog.Cancel
        background: Rectangle {
            color: Theme.panel
            border.color: Theme.border
            radius: Theme.radius
        }
        contentItem: Label {
            text: I18n.s.fix_duplicates_confirm
            color: Theme.text
            wrapMode: Text.Wrap
        }
        onAccepted: App.runCnsIdFixer(page.folderUrl(page.modsPath), true)
    }

    Connections {
        target: App
        function onCnsConversionFinished(ok, outputPath) {
            if (!page.appearanceRepairRunning) return
            page.appearanceRepairRunning = false
            page.appearanceRepairResult = App.lastCnsResult
        }
    }
}
