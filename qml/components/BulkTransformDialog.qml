import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import ".."

// Transformación masiva de valores numéricos sobre los cambios visibles.
// Selección de filas por regex (opcional) + operación (×N, +, −, /, set,
// clamp, min, max). Aplica solo a cambios editables numéricos visibles.
Dialog {
    id: root
    modal: true
    width: 520
    background: Rectangle { color: Theme.panel; border.color: Theme.border; radius: Theme.radius }

    // op -> requiere segundo operando (solo clamp)
    property var ops: [
        { key: "mul",   label: "× (multiplicar)" },
        { key: "add",   label: "+ (sumar)" },
        { key: "sub",   label: "− (restar)" },
        { key: "div",   label: "÷ (dividir)" },
        { key: "set",   label: "= (fijar valor)" },
        { key: "clamp", label: "clamp (min…max)" },
        { key: "min",   label: "min (tope inferior)" },
        { key: "max",   label: "max (tope superior)" }
    ]
    property int opIndex: 0
    readonly property bool needsTwo: ops[opIndex].key === "clamp"

    onOpened: { resultLabel.text = ""; }

    contentItem: ColumnLayout {
        spacing: 10
        Label {
            text: I18n.s.bulk_title
            color: Theme.text; font.pixelSize: 17; font.bold: true
        }
        Label {
            text: I18n.s.bulk_desc
            color: Theme.textDim; wrapMode: Text.Wrap; Layout.fillWidth: true
        }

        Label { text: I18n.s.bulk_regex; color: Theme.textDim; font.pixelSize: 12 }
        TextField {
            id: regexField
            Layout.fillWidth: true
            color: Theme.text
            placeholderText: I18n.s.bulk_regex_hint
            background: Rectangle { color: Theme.panelAlt; border.color: Theme.border; radius: Theme.radius }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            ComboBox {
                id: opBox
                Layout.preferredWidth: 200
                model: root.ops.map(function(o) { return o.label })
                currentIndex: root.opIndex
                onCurrentIndexChanged: root.opIndex = currentIndex
            }
            TextField {
                id: aField
                Layout.preferredWidth: 90
                color: Theme.text
                text: root.needsTwo ? "" : "2"
                placeholderText: root.needsTwo ? I18n.s.bulk_min : "N"
                background: Rectangle { color: Theme.panelAlt; border.color: Theme.border; radius: Theme.radius }
            }
            TextField {
                id: bField
                Layout.preferredWidth: 90
                visible: root.needsTwo
                color: Theme.text
                placeholderText: I18n.s.bulk_max
                background: Rectangle { color: Theme.panelAlt; border.color: Theme.border; radius: Theme.radius }
            }
        }

        Label {
            id: resultLabel
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.textDim
            text: ""
        }

        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            Button { text: I18n.s.cancel; onClicked: root.close() }
            Button {
                text: I18n.s.bulk_apply
                highlighted: true
                onClicked: {
                    var a = parseFloat(aField.text)
                    var b = root.needsTwo ? parseFloat(bField.text) : 0
                    if (isNaN(a) || (root.needsTwo && isNaN(b))) {
                        resultLabel.text = I18n.s.bulk_bad_number
                        resultLabel.color = Theme.danger
                        return
                    }
                    var n = App.changeModel.applyTransform(root.ops[root.opIndex].key, a, b, regexField.text)
                    if (n < 0) {
                        resultLabel.text = I18n.s.bulk_bad_regex
                        resultLabel.color = Theme.danger
                    } else {
                        resultLabel.text = I18n.s.bulk_applied.arg(n)
                        resultLabel.color = Theme.ok
                    }
                }
            }
        }
    }
}
