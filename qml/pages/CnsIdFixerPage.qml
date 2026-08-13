import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import ".."

Item {
    id: page
    property string modsPath: App.defaultOutDir()

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
}
