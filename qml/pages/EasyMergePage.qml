import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import ".."
import "../components"

Item {
    id: page

    signal openSettings()

    // Orquestación: analizar (si hace falta) -> resolver por prioridad -> mergear.
    property bool pendingMerge: false
    property string pendingOut: ""

    // Auto-análisis: al terminar una importación (busy -> false) con mods
    // cargados y sin análisis, analizar solo (construye baseline si hace falta).
    function maybeAutoAnalyze() {
        if (visible && !App.busy && !App.analyzed && App.modModel.rowCount() > 0
            && App.toolsAvailable && !pendingMerge)
            App.analyze()
    }

    function resolvedOut() {
        if (outField.text.length > 0) return outField.text
        return App.defaultOutDir()
    }

    function startMerge() {
        if (App.busy || App.modModel.rowCount() === 0) return
        var out = resolvedOut()
        if (out.length === 0) { outField.forceActiveFocus(); return }
        pendingOut = "file:///" + out.replace(/\\/g, "/")
        if (App.analyzed) {
            App.resolveAllByPriority()
            App.merge(pendingOut)
        } else {
            pendingMerge = true
            App.analyze()
        }
    }

    Connections {
        target: App
        function onBusyChanged() {
            // Cuando termina el análisis (busy vuelve a false), disparar el merge.
            if (page.pendingMerge && App.analyzed && !App.busy) {
                page.pendingMerge = false
                App.resolveAllByPriority()
                App.merge(page.pendingOut)
                return
            }
            page.maybeAutoAnalyze()
        }
        function onAnalysisChanged() { page.maybeAutoAnalyze() }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: I18n.s.easy_title
                color: Theme.text
                font.pixelSize: 22
                font.bold: true
            }
            Item { Layout.fillWidth: true }
            Button {
                text: I18n.s.easy_clear
                visible: App.modModel.rowCount() > 0
                enabled: !App.busy
                onClicked: App.clearMods()
            }
        }
        Label {
            text: I18n.s.easy_subtitle
            color: Theme.textDim
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        // Setup pendiente: sin juego configurado no se leen mods Zen.
        Rectangle {
            visible: !App.hasGamePath
            Layout.fillWidth: true
            Layout.preferredHeight: setupRow.height + 20
            radius: Theme.radius
            color: Qt.rgba(0.9, 0.7, 0.33, 0.10)
            border.color: Theme.warn
            RowLayout {
                id: setupRow
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.margins: 12
                spacing: 10
                Label { text: "⚠"; color: Theme.warn; font.pixelSize: 18 }
                Label {
                    text: I18n.s.easy_setup_banner
                    color: Theme.text
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
                Button {
                    text: I18n.s.easy_setup_btn
                    highlighted: true
                    onClicked: page.openSettings()
                }
            }
        }

        // Drop zone grande (multi-archivo)
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 110
            radius: Theme.radius
            color: drop.containsDrag ? Theme.panelAlt : Theme.panel
            border.color: drop.containsDrag ? Theme.accent : Theme.border
            border.width: 2

            ColumnLayout {
                anchors.centerIn: parent
                spacing: 8
                Label {
                    text: I18n.s.easy_dropzone
                    color: Theme.textDim
                    Layout.alignment: Qt.AlignHCenter
                }
                Button {
                    text: I18n.s.easy_add
                    enabled: !App.busy
                    Layout.alignment: Qt.AlignHCenter
                    onClicked: addDialog.open()
                }
            }
            DropArea {
                id: drop
                anchors.fill: parent
                onDropped: (d) => {
                    for (let i = 0; i < d.urls.length; ++i)
                        App.addMod(d.urls[i])
                }
            }
        }

        // Lista compacta de mods
        ListView {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, 150)
            model: App.modModel
            clip: true
            spacing: 4
            delegate: Rectangle {
                required property string name
                required property int tableCount
                required property int otherCount
                required property int index
                width: ListView.view.width
                height: 40
                radius: Theme.radius
                color: Theme.panel
                border.color: Theme.border
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 6
                    spacing: 8
                    Label { text: (index + 1) + "."; color: Theme.accent; font.bold: true }
                    Label { text: name; color: Theme.text; elide: Text.ElideRight; Layout.fillWidth: true }
                    Label {
                        text: tableCount + " ⊞"
                        color: Theme.textDim; font.pixelSize: 12
                    }
                    Button { text: "✕"; flat: true; enabled: !App.busy; onClicked: App.removeMod(index) }
                }
            }
        }

        // Destino + botón merge
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label { text: I18n.s.easy_out; color: Theme.textDim }
            TextField {
                id: outField
                Layout.fillWidth: true
                text: App.defaultOutDir()
                placeholderText: I18n.s.easy_out_placeholder
                color: Theme.text
                background: Rectangle { color: Theme.panel; border.color: Theme.border; radius: Theme.radius }
            }
            Button { text: I18n.s.merge_choose; enabled: !App.busy; onClicked: outDialog.open() }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            Button {
                text: I18n.s.easy_merge_btn
                highlighted: true
                enabled: !App.busy && App.modModel.rowCount() > 0 && App.toolsAvailable
                onClicked: page.startMerge()
            }
            Label {
                text: App.analyzed
                      ? I18n.s.easy_summary.replace("%1", App.changeModel.totalCount)
                                           .replace("%2", App.conflictModel.rowCount())
                      : (App.modModel.rowCount() > 0
                         ? (App.busy ? I18n.s.easy_analyzing_hint : "")
                         : I18n.s.easy_need_analyze)
                color: Theme.textDim
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
        }

        RowLayout {
            visible: App.lastMergeResult.length > 0
            Layout.fillWidth: true
            spacing: 10
            Label {
                text: App.lastMergeResult
                color: App.lastMergeOk ? Theme.ok : Theme.danger
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Button {
                visible: App.lastMergeOk
                text: I18n.s.easy_open_folder
                onClicked: App.openDir(page.resolvedOut())
            }
            Button {
                visible: App.lastMergeOk && App.disableableSourceCount() > 0
                text: I18n.s.easy_disable_sources.replace("%1", App.disableableSourceCount())
                onClicked: disableDialog.open()
            }
        }

        // ---- Advanced: habilitar/deshabilitar cambios ----
        RowLayout {
            Layout.fillWidth: true
            CheckBox {
                id: advToggle
                enabled: !App.busy && App.modModel.rowCount() > 0
                text: I18n.s.easy_advanced
                contentItem: Label {
                    text: advToggle.text
                    color: advToggle.enabled ? Theme.text : Theme.textDim
                    leftPadding: advToggle.indicator.width + 6
                    verticalAlignment: Text.AlignVCenter
                }
                // Al expandir sin análisis previo, analizar (construye baseline si hace falta).
                onToggled: if (checked && !App.analyzed) App.analyze()
            }
            Item { Layout.fillWidth: true }
            CheckBox {
                id: onlyConf
                visible: advToggle.checked
                text: I18n.s.changes_only_conflicts
                onCheckedChanged: App.changeModel.onlyConflicts = checked
                contentItem: Label {
                    text: onlyConf.text; color: Theme.text
                    leftPadding: onlyConf.indicator.width + 6
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
        Label {
            visible: advToggle.checked
            text: I18n.s.easy_advanced_hint
            color: Theme.textDim
            font.pixelSize: 12
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        ListView {
            visible: advToggle.checked
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
                height: 30
                color: Theme.panelAlt
                radius: Theme.radius
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    Label { text: section; color: Theme.accent; font.bold: true; Layout.fillWidth: true }
                    Button { text: I18n.s.changes_all; flat: true; onClicked: App.changeModel.setTableChecked(section, true) }
                    Button { text: I18n.s.changes_none; flat: true; onClicked: App.changeModel.setTableChecked(section, false) }
                }
            }
            delegate: Rectangle {
                required property var model
                required property string summary
                required property int conflictId
                required property string modName
                required property int index
                width: ListView.view.width
                height: 32
                color: conflictId >= 0 ? Qt.rgba(0.9, 0.7, 0.33, 0.08) : "transparent"
                radius: 4
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    spacing: 8
                    CheckBox {
                        id: cb
                        checked: model.checked
                        onToggled: App.changeModel.setChecked(index, cb.checked)
                    }
                    Label { text: summary; color: Theme.text; elide: Text.ElideRight; Layout.fillWidth: true }
                    Button {
                        text: "✎"
                        flat: true
                        visible: App.changeModel.canEdit(index)
                        onClicked: editDialog.openFor(index, summary)
                    }
                    Label { text: modName; color: Theme.textDim; font.pixelSize: 12 }
                }
            }
        }

        Item { visible: !advToggle.checked; Layout.fillHeight: true }
    }

    EditValueDialog {
        id: editDialog
        anchors.centerIn: parent
    }

    Dialog {
        id: disableDialog
        modal: true
        anchors.centerIn: parent
        width: 480
        background: Rectangle { color: Theme.panel; border.color: Theme.border; radius: Theme.radius }
        contentItem: ColumnLayout {
            spacing: 12
            Label {
                text: I18n.s.easy_disable_confirm.replace("%1", App.disableableSourceCount())
                color: Theme.text
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            RowLayout {
                Item { Layout.fillWidth: true }
                Button { text: I18n.s.cancel; onClicked: disableDialog.close() }
                Button {
                    text: I18n.s.ok
                    highlighted: true
                    onClicked: {
                        var n = App.disableSourceMods()
                        disableDialog.close()
                        disabledResult.text = I18n.s.easy_disabled_done.replace("%1", n)
                        disabledResult.visible = true
                    }
                }
            }
        }
    }
    Label {
        id: disabledResult
        visible: false
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.margins: 18
        color: Theme.ok
    }

    FileDialog {
        id: addDialog
        nameFilters: ["Mods (*.pak *.zip)"]
        fileMode: FileDialog.OpenFiles
        onAccepted: { for (let i = 0; i < selectedFiles.length; ++i) App.addMod(selectedFiles[i]) }
    }
    FolderDialog {
        id: outDialog
        onAccepted: {
            let p = selectedFolder.toString().replace("file:///", "")
            outField.text = decodeURIComponent(p).replace(/\//g, "\\")
        }
    }
}
