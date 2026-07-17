import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import ".."

Item {
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 12

        Label {
            text: I18n.s.settings_title
            color: Theme.text
            font.pixelSize: 22
            font.bold: true
        }

        Label {
            text: I18n.s.settings_language
            color: Theme.text
            font.pixelSize: 16
            font.bold: true
        }
        Label {
            text: I18n.s.settings_language_desc
            color: Theme.textDim
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 3
            columnSpacing: 8
            rowSpacing: 8
            Repeater {
                model: I18n.languages
                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    height: 44
                    radius: Theme.radius
                    color: I18n.language === modelData.code ? Theme.panelAlt : Theme.panel
                    border.color: I18n.language === modelData.code ? Theme.accent : Theme.border
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        spacing: 10
                        Label { text: modelData.flag; font.pixelSize: 20 }
                        Label {
                            text: modelData.name
                            color: Theme.text
                            font.pixelSize: 15
                            Layout.fillWidth: true
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: I18n.language = modelData.code
                    }
                }
            }
        }

        Item { Layout.fillHeight: true }
    }
}
