import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import ".."

Dialog {
    id: root
    modal: true
    closePolicy: Popup.NoAutoClose
    width: 460
    padding: 0
    background: Rectangle { color: Theme.panel; border.color: Theme.border; radius: Theme.radius }

    property string selected: I18n.language

    contentItem: ColumnLayout {
        spacing: 0

        Label {
            Layout.fillWidth: true
            Layout.margins: 18
            Layout.bottomMargin: 4
            text: I18n.s.lang_popup_title
            color: Theme.text
            font.pixelSize: 20
            font.bold: true
        }
        Label {
            Layout.fillWidth: true
            Layout.leftMargin: 18
            Layout.rightMargin: 18
            text: I18n.s.lang_popup_desc
            color: Theme.textDim
            wrapMode: Text.Wrap
        }

        GridLayout {
            Layout.fillWidth: true
            Layout.margins: 18
            columns: 2
            columnSpacing: 8
            rowSpacing: 8
            Repeater {
                model: I18n.languages
                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    height: 44
                    radius: Theme.radius
                    color: root.selected === modelData.code ? Theme.panelAlt : "transparent"
                    border.color: root.selected === modelData.code ? Theme.accent : Theme.border
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
                        onClicked: root.selected = modelData.code
                    }
                }
            }
        }

        Button {
            Layout.fillWidth: true
            Layout.margins: 18
            Layout.topMargin: 0
            text: I18n.s.lang_popup_confirm
            highlighted: true
            onClicked: {
                I18n.language = root.selected
                root.close()
            }
        }
    }

    // Previsualización en vivo: al elegir, cambia el idioma de inmediato.
    onSelectedChanged: I18n.language = selected
}
