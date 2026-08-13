import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import ".."
import "../components"

Page {
    id: root
    background: Rectangle { color: Theme.bg }
    property var presetModel: []

    function refresh() {
        try { presetModel = JSON.parse(ReShade.presetsJson || "[]") }
        catch (e) { presetModel = [] }
    }
    function showStatus(message, good) {
        statusLabel.text = message
        statusLabel.color = good ? Theme.ok : Theme.danger
    }

    Connections {
        target: ReShade
        function onChanged() { root.refresh() }
        function onErrorOccurred(message) { root.showStatus(message, false) }
        function onOperationFinished(message) { root.showStatus(message, true) }
    }
    Component.onCompleted: root.refresh()
    onVisibleChanged: if (visible) ReShade.refresh()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 14

        Label { text: "ReShade"; color: Theme.text; font.pixelSize: 28; font.bold: true }
        Label {
            text: I18n.s.reshade_desc
            color: Theme.textDim; wrapMode: Text.Wrap; Layout.fillWidth: true
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: locationColumn.implicitHeight + 20
            radius: Theme.radius; color: Theme.panel; border.color: ReShade.available ? Theme.ok : Theme.warn
            ColumnLayout {
                id: locationColumn
                anchors.fill: parent; anchors.margins: 10; spacing: 5
                Label {
                    text: ReShade.available ? I18n.s.reshade_detected : I18n.s.reshade_not_detected
                    color: ReShade.available ? Theme.ok : Theme.warn; font.bold: true
                }
                Label { text: ReShade.reshadeDir || I18n.s.reshade_game_missing; color: Theme.textDim; elide: Text.ElideMiddle; Layout.fillWidth: true }
                Label {
                    visible: ReShade.activePresetPath !== ""
                    text: I18n.s.reshade_active + ": " + ReShade.activePresetPath
                    color: Theme.textDim; elide: Text.ElideMiddle; Layout.fillWidth: true
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true; spacing: 8
            Button { text: I18n.s.reshade_save; enabled: ReShade.available; onClicked: saveDialog.open() }
            Button { text: I18n.s.reshade_import; onClicked: importDialog.open() }
            Button { text: I18n.s.reshade_open_folder; enabled: ReShade.reshadeDir !== ""; onClicked: ReShade.openReshadeDir() }
            Item { Layout.fillWidth: true }
        }

        Label {
            text: I18n.s.reshade_backup_hint + (ReShade.lastBackupPath ? "\n" + ReShade.lastBackupPath : "")
            visible: ReShade.lastBackupPath !== ""
            color: Theme.textDim; wrapMode: Text.Wrap; Layout.fillWidth: true
        }
        Label { id: statusLabel; Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.textDim }

        Label { text: I18n.s.reshade_saved; color: Theme.text; font.pixelSize: 18; font.bold: true }
        Label {
            visible: root.presetModel.length === 0
            text: I18n.s.reshade_empty; color: Theme.textDim
        }
        ListView {
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true; spacing: 8; model: root.presetModel
            delegate: Rectangle {
                required property var modelData
                width: ListView.view.width; height: cardColumn.implicitHeight + 20
                radius: Theme.radius; color: Theme.panel; border.color: Theme.border
                ColumnLayout {
                    id: cardColumn
                    anchors.fill: parent; anchors.margins: 10; spacing: 5
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: modelData.name; color: Theme.text; font.bold: true; font.pixelSize: 16; Layout.fillWidth: true }
                        Button { text: I18n.s.reshade_restore; enabled: ReShade.available; onClicked: ReShade.restorePreset(modelData.name) }
                        Button { text: I18n.s.reshade_rename; onClicked: { renameDialog.oldName = modelData.name; renameField.text = modelData.name; renameDialog.open() } }
                        Button { text: I18n.s.reshade_export; onClicked: { exportDialog.presetName = modelData.name; exportDialog.open() } }
                        Button { text: I18n.s.reshade_delete; onClicked: ReShade.deletePreset(modelData.name) }
                    }
                    Label {
                        visible: modelData.warning !== ""
                        text: "⚠ " + modelData.warning
                        color: Theme.warn; wrapMode: Text.Wrap; Layout.fillWidth: true
                    }
                }
            }
        }
    }

    Dialog {
        id: saveDialog; modal: true; title: I18n.s.reshade_save_title
        anchors.centerIn: parent; standardButtons: Dialog.Ok | Dialog.Cancel
        contentItem: TextField { id: saveField; placeholderText: I18n.s.reshade_name; implicitWidth: 360; onAccepted: saveDialog.accept() }
        onOpened: { saveField.text = ""; saveField.forceActiveFocus() }
        onAccepted: { if (!ReShade.saveCurrentPreset(saveField.text)) root.showStatus(I18n.s.reshade_save_failed, false) }
    }
    Dialog {
        id: renameDialog; modal: true; title: I18n.s.reshade_rename_title
        property string oldName: ""
        anchors.centerIn: parent; standardButtons: Dialog.Ok | Dialog.Cancel
        contentItem: TextField { id: renameField; placeholderText: I18n.s.reshade_name; implicitWidth: 360; onAccepted: renameDialog.accept() }
        onAccepted: { if (!ReShade.renamePreset(oldName, renameField.text)) root.showStatus(I18n.s.reshade_rename_failed, false) }
    }
    FileDialog {
        id: importDialog; title: I18n.s.reshade_import_title; fileMode: FileDialog.OpenFile
        nameFilters: ["ReShade (*.ini)"]
        onAccepted: ReShade.importPreset(selectedFile)
    }
    FileDialog {
        id: exportDialog; property string presetName: ""; title: I18n.s.reshade_export_title
        fileMode: FileDialog.SaveFile; defaultSuffix: "ini"; nameFilters: ["ReShade (*.ini)"]
        onAccepted: ReShade.exportPreset(presetName, selectedFile)
    }
}
