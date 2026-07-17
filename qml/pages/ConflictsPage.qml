import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import ".."

Item {
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 10

        RowLayout {
            Label {
                text: I18n.s.conflicts_title
                color: Theme.text
                font.pixelSize: 22
                font.bold: true
            }
            Rectangle {
                visible: App.conflictModel.pendingCount > 0
                width: pendingLabel.width + 16; height: 24; radius: 12
                color: Theme.warn
                Label {
                    id: pendingLabel
                    anchors.centerIn: parent
                    text: I18n.s.conflicts_unresolved.replace("%1", App.conflictModel.pendingCount)
                    color: "#14161c"; font.bold: true; font.pixelSize: 12
                }
            }
            Item { Layout.fillWidth: true }
        }

        Label {
            visible: App.analyzed && App.conflictModel.rowCount() === 0
            text: I18n.s.conflicts_none
            color: Theme.ok
        }

        // Resolución masiva por mod
        RowLayout {
            visible: App.conflictModel.rowCount() > 0
            spacing: 8
            Label { text: I18n.s.conflicts_prefer_all; color: Theme.textDim }
            Repeater {
                model: App.modModel
                delegate: Button {
                    required property string name
                    required property string modId
                    text: name
                    onClicked: App.resolveAllWithMod(modId)
                }
            }
        }

        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: App.conflictModel
            clip: true
            spacing: 8

            delegate: Rectangle {
                required property string title
                required property var candidates
                required property string resolvedMod
                required property int groupId
                required property string baseText
                width: ListView.view.width
                height: content.height + 24
                radius: Theme.radius
                color: Theme.panel
                border.color: resolvedMod === "" ? Theme.warn : Theme.border

                ColumnLayout {
                    id: content
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 12
                    spacing: 6

                    Label { text: title; color: Theme.text; font.bold: true; font.pixelSize: 15 }
                    Label {
                        visible: App.hasBaseline
                        text: I18n.s.conflicts_vanilla.replace("%1", baseText)
                        color: Theme.textDim
                        font.pixelSize: 12
                    }
                    Repeater {
                        model: candidates
                        delegate: RowLayout {
                            required property var modelData
                            spacing: 8
                            RadioButton {
                                checked: modelData.chosen
                                onClicked: App.resolveConflict(groupId, modelData.modId)
                            }
                            Label { text: modelData.modName; color: Theme.accent; font.bold: true }
                            Label {
                                text: modelData.valueText
                                color: Theme.text
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }
                    }
                }
            }
        }
    }
}
