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
            spacing: 10
            Label {
                text: "Cambios (" + App.changeModel.totalCount + ")"
                color: Theme.text
                font.pixelSize: 22
                font.bold: true
            }
            Item { Layout.fillWidth: true }
            CheckBox {
                id: onlyConflicts
                text: "Solo conflictos"
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
                placeholderText: "Buscar..."
                Layout.preferredWidth: 240
                onTextChanged: App.changeModel.filterText = text
                color: Theme.text
                background: Rectangle {
                    color: Theme.panel; border.color: Theme.border; radius: Theme.radius
                }
            }
        }

        Label {
            visible: !App.analyzed
            text: "Cargá mods y ejecutá \"Analizar cambios\" en la página Mods."
            color: Theme.textDim
        }

        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: App.changeModel
            clip: true
            spacing: 2

            section.property: "tableName"
            section.criteria: ViewSection.FullString
            section.delegate: Rectangle {
                required property string section
                width: ListView.view.width
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
                    Button {
                        text: "Todo"
                        flat: true
                        onClicked: App.changeModel.setTableChecked(section, true)
                    }
                    Button {
                        text: "Nada"
                        flat: true
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
                width: ListView.view.width
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
                    Rectangle {
                        visible: conflictId >= 0
                        width: confLabel.width + 14; height: 20; radius: 10
                        color: Theme.warn
                        Label {
                            id: confLabel
                            anchors.centerIn: parent
                            text: "conflicto"
                            font.pixelSize: 11; font.bold: true
                            color: "#14161c"
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
}
