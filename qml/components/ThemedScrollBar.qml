import QtQuick
import QtQuick.Controls.Basic
import ".."

// Barra de scroll visible sobre el tema oscuro/claro. El estilo Basic dibuja una
// barra casi invisible por defecto, así que las listas largas no daban ninguna
// pista de que se podía scrollear.
ScrollBar {
    id: bar
    policy: ScrollBar.AsNeeded
    width: 12

    contentItem: Rectangle {
        implicitWidth: 8
        radius: 4
        color: bar.pressed ? Theme.accent
                           : (bar.hovered ? Theme.textDim : Theme.border)
        opacity: bar.policy === ScrollBar.AlwaysOn || bar.active ? 1 : 0.6
        Behavior on color { ColorAnimation { duration: 120 } }
    }

    background: Rectangle {
        color: Theme.panelAlt
        opacity: bar.active ? 0.5 : 0
        radius: 6
        Behavior on opacity { NumberAnimation { duration: 120 } }
    }
}
