import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import ".."

// Editor del valor final de un cambio (fila visible del ChangeListModel).
Dialog {
    id: root
    modal: true
    width: 460
    background: Rectangle { color: Theme.panel; border.color: Theme.border; radius: Theme.radius }

    property int row: -1
    property string summary: ""

    function openFor(visibleRow, summaryText) {
        row = visibleRow
        summary = summaryText
        valueField.text = App.changeModel.valueText(visibleRow)
        errorLabel.visible = false
        open()
        valueField.forceActiveFocus()
        valueField.selectAll()
    }

    contentItem: ColumnLayout {
        spacing: 10
        Label {
            text: I18n.s.edit_value_title
            color: Theme.text
            font.pixelSize: 17
            font.bold: true
        }
        Label {
            text: root.summary
            color: Theme.textDim
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        TextField {
            id: valueField
            Layout.fillWidth: true
            color: Theme.text
            background: Rectangle { color: Theme.panelAlt; border.color: Theme.border; radius: Theme.radius }
            onAccepted: applyBtn.clicked()
        }
        Label {
            id: errorLabel
            visible: false
            text: I18n.s.edit_value_invalid
            color: Theme.danger
            font.pixelSize: 12
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            Button { text: I18n.s.cancel; onClicked: root.close() }
            Button {
                id: applyBtn
                text: I18n.s.ok
                highlighted: true
                onClicked: {
                    if (App.changeModel.setEditedValue(root.row, valueField.text))
                        root.close()
                    else
                        errorLabel.visible = true
                }
            }
        }
    }
}
