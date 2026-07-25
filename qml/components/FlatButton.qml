import QtQuick
import QtQuick.Controls.Basic
import ".."

// Botón plano legible sobre los paneles del tema. El estilo Basic dibuja los
// botones flat sin fondo y con el color de texto por defecto (oscuro), así que
// sobre el panel oscuro quedaban invisibles.
Button {
    id: control
    flat: true

    contentItem: Label {
        text: control.text
        color: control.enabled
               ? (control.hovered ? Theme.accent : Theme.textDim)
               : Theme.border
        font: control.font
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        Behavior on color { ColorAnimation { duration: 100 } }
    }

    background: Rectangle {
        radius: Theme.radius
        color: control.pressed ? Theme.panelAlt : "transparent"
        border.color: control.hovered ? Theme.border : "transparent"
    }
}
