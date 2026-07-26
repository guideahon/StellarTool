import QtQuick
import QtQuick.Controls.Basic
import ".."

// ComboBox tematizado (respeta Theme claro/oscuro). Reemplaza el look gris
// del estilo Basic por defecto.
ComboBox {
    id: control
    implicitHeight: 38

    background: Rectangle {
        radius: Theme.radius
        color: Theme.panel
        border.color: control.activeFocus ? Theme.accent : Theme.border
    }

    contentItem: Text {
        leftPadding: 12
        rightPadding: 34
        text: control.displayText
        color: Theme.text
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    indicator: Text {
        x: control.width - 24
        y: (control.height - height) / 2
        text: "▾"
        color: Theme.textDim
        font.pixelSize: 12
    }

    delegate: ItemDelegate {
        width: control.width
        height: 38
        contentItem: Text {
            text: control.textAt(index)
            color: highlighted ? Theme.accent : Theme.text
            verticalAlignment: Text.AlignVCenter
            leftPadding: 8
        }
        highlighted: control.highlightedIndex === index
        background: Rectangle {
            color: highlighted ? Theme.panelAlt : "transparent"
        }
    }

    popup: Popup {
        y: control.height + 4
        width: control.width
        height: Math.min(control.count * 38 + padding * 2, 268)
        padding: 4
        background: Rectangle {
            radius: Theme.radius
            color: Theme.panel
            border.color: Theme.border
        }
        contentItem: ListView {
            clip: true
            model: control.popup.visible ? control.delegateModel : null
            ScrollIndicator.vertical: ScrollIndicator {}
        }
    }
}
