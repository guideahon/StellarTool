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
            text: I18n.s.merge_title
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
                    text: I18n.s.merge_changes_detected.replace("%1", App.changeModel.totalCount)
                    color: Theme.text
                }
                Label {
                    text: I18n.s.merge_unresolved.replace("%1", App.conflictModel.pendingCount)
                    color: App.conflictModel.pendingCount > 0 ? Theme.warn : Theme.ok
                }
                Label {
                    text: I18n.s.merge_output_note
                    color: Theme.textDim
                    font.pixelSize: 12
                }
                Label {
                    visible: !App.hasBaseline
                    text: I18n.s.merge_nobaseline_note
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
                placeholderText: I18n.s.merge_outdir_placeholder
                color: Theme.text
                background: Rectangle { color: Theme.panel; border.color: Theme.border; radius: Theme.radius }
            }
            Button {
                text: I18n.s.merge_choose
                onClicked: outDirDialog.open()
            }
        }

        CheckBox {
            id: zipCheck
            checked: App.exportZip
            onToggled: App.exportZip = checked
            text: I18n.s.merge_zip_check
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
                text: I18n.s.merge_generate
                highlighted: true
                enabled: !App.busy && App.analyzed && App.conflictModel.pendingCount === 0
                          && outDir.text.length > 0
                onClicked: App.merge("file:///" + outDir.text.replace(/\\/g, "/"))
            }
            Button {
                text: I18n.s.merge_save_project
                enabled: App.analyzed
                onClicked: saveDialog.open()
            }
            Button {
                text: I18n.s.merge_load_project
                enabled: !App.busy
                onClicked: loadDialog.open()
            }
            Item { Layout.fillWidth: true }
        }

        Label {
            text: App.lastMergeResult
            color: App.lastMergeOk ? Theme.ok : Theme.danger
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
