import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import ".."
import "../components"

Item {
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 10

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 8
            Label {
                text: I18n.s.changes_title + " (" + App.changeModel.totalCount + ")"
                color: Theme.text
                font.pixelSize: 22
                font.bold: true
                Layout.fillWidth: true
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                CheckBox {
                    id: onlyConflicts
                    text: I18n.s.changes_only_conflicts
                    onCheckedChanged: App.changeModel.onlyConflicts = checked
                    contentItem: Label {
                        text: onlyConflicts.text
                        color: Theme.text
                        leftPadding: onlyConflicts.indicator.width + 6
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                TextField {
                    id: search
                    placeholderText: I18n.s.changes_search
                    Layout.fillWidth: true
                    Layout.minimumWidth: 140
                    Layout.preferredWidth: 280
                    onTextChanged: App.changeModel.filterText = text
                    color: Theme.text
                    background: Rectangle {
                        color: Theme.panel; border.color: Theme.border; radius: Theme.radius
                    }
                }
                Button {
                    text: I18n.s.bulk_title
                    enabled: App.analyzed
                    onClicked: bulkDialog.open()
                }
                Button {
                    text: I18n.s.toml_import
                    enabled: !App.busy
                    onClicked: tomlImportDialog.open()
                }
                Button {
                    text: I18n.s.toml_export
                    enabled: App.analyzed
                    onClicked: tomlExportDialog.open()
                }
            }
        }

        RowLayout {
            visible: !App.analyzed
            spacing: 10
            Label {
                text: App.modModel.count > 0
                      ? I18n.s.needs_analyze.arg(App.modModel.count)
                      : I18n.s.changes_hint
                color: App.modModel.count > 0 ? Theme.warn : Theme.textDim
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Button {
                text: I18n.s.home_analyze
                visible: App.modModel.count > 0
                enabled: !App.busy && App.toolsAvailable
                highlighted: true
                onClicked: App.analyze()
            }
        }

        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.rightMargin: -18   // barra pegada al borde (ver ConflictsPage)
            model: App.changeModel
            clip: true
            spacing: 2
            readonly property real rowWidth: width - 6 - (vbar.visible ? vbar.width : 0)
            ScrollBar.vertical: ThemedScrollBar { id: vbar }

            section.property: "tableName"
            section.criteria: ViewSection.FullString
            section.delegate: Rectangle {
                required property string section
                width: ListView.view.rowWidth
                height: 34
                color: Theme.panelAlt
                radius: Theme.radius

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    Label {
                        text: section
                        color: Theme.accent
                        font.bold: true
                        Layout.fillWidth: true
                    }
                    FlatButton {
                        text: I18n.s.changes_all
                        onClicked: App.changeModel.setTableChecked(section, true)
                    }
                    FlatButton {
                        text: I18n.s.changes_none
                        onClicked: App.changeModel.setTableChecked(section, false)
                    }
                }
            }

            delegate: Rectangle {
                required property var model
                required property string summary
                required property int conflictId
                required property string modName
                required property int index
                width: ListView.view.rowWidth
                height: 36
                color: conflictId >= 0 ? Qt.rgba(0.9, 0.7, 0.33, 0.08) : "transparent"
                radius: 4

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    spacing: 8
                    CheckBox {
                        id: itemCheck
                        checked: model.checked
                        onToggled: App.changeModel.setChecked(index, itemCheck.checked)
                    }
                    Label {
                        text: summary
                        color: Theme.text
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    FlatButton {
                        text: "✎"
                        visible: App.changeModel.canEdit(index)
                        onClicked: editDialog.openFor(index, summary)
                    }
                    Rectangle {
                        visible: conflictId >= 0
                        width: confLabel.width + 14; height: 20; radius: 10
                        color: Theme.warn
                        Label {
                            id: confLabel
                            anchors.centerIn: parent
                            text: I18n.s.badge_conflict
                            font.pixelSize: 11; font.bold: true
                            color: Theme.warnText
                        }
                    }
                    Label {
                        text: modName
                        color: Theme.textDim
                        font.pixelSize: 12
                    }
                }
            }
        }
    }

    EditValueDialog {
        id: editDialog
        anchors.centerIn: parent
    }

    BulkTransformDialog {
        id: bulkDialog
        anchors.centerIn: parent
    }

    FileDialog {
        id: tomlImportDialog
        nameFilters: ["TOML patch (*.toml)"]
        onAccepted: App.importTomlPatch(selectedFile)
    }
    FolderDialog {
        id: tomlExportDialog
        onAccepted: App.exportTomlPatches(selectedFolder)
    }
}
