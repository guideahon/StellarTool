import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import "."
import "pages"

ApplicationWindow {
    id: win
    visible: true
    width: 1180
    height: 760
    minimumWidth: 900
    minimumHeight: 560
    title: "Stellar Tool — merge de mods de Stellar Blade"
    color: Theme.bg

    property int currentPage: 0

    // ---- Barra lateral ----
    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.preferredWidth: 190
            Layout.fillHeight: true
            color: Theme.panel

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 6

                Label {
                    text: "Stellar Tool"
                    color: Theme.text
                    font.pixelSize: 20
                    font.bold: true
                    Layout.margins: 6
                }

                Repeater {
                    model: [
                        { label: "Mods", page: 0 },
                        { label: "Cambios", page: 1 },
                        { label: "Conflictos", page: 2 },
                        { label: "Merge", page: 3 },
                    ]
                    delegate: Rectangle {
                        required property var modelData
                        Layout.fillWidth: true
                        height: 40
                        radius: Theme.radius
                        color: win.currentPage === modelData.page ? Theme.panelAlt : "transparent"
                        border.color: win.currentPage === modelData.page ? Theme.border : "transparent"
                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.leftMargin: 14
                            text: modelData.label
                            color: win.currentPage === modelData.page ? Theme.text : Theme.textDim
                            font.pixelSize: 15
                        }
                        Rectangle {
                            visible: modelData.page === 2 && App.conflictModel.pendingCount > 0
                            anchors.right: parent.right
                            anchors.rightMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                            width: pendLabel.width + 12; height: 20; radius: 10
                            color: Theme.warn
                            Label {
                                id: pendLabel
                                anchors.centerIn: parent
                                text: App.conflictModel.pendingCount
                                color: "#14161c"
                                font.pixelSize: 12
                                font.bold: true
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: win.currentPage = modelData.page
                        }
                    }
                }

                Item { Layout.fillHeight: true }

                Label {
                    visible: !App.toolsAvailable
                    Layout.fillWidth: true
                    text: App.toolsError
                    color: Theme.danger
                    wrapMode: Text.Wrap
                    font.pixelSize: 12
                }
                Label {
                    visible: !App.hasBaseline
                    Layout.fillWidth: true
                    text: "Sin baseline vanilla: los cambios se muestran sin \"antes → después\" y las filas aparecen completas."
                    color: Theme.warn
                    wrapMode: Text.Wrap
                    font.pixelSize: 12
                }
                Label {
                    Layout.fillWidth: true
                    text: App.statusText
                    color: Theme.textDim
                    wrapMode: Text.Wrap
                    font.pixelSize: 12
                }
                BusyIndicator {
                    running: App.busy
                    visible: App.busy
                    Layout.alignment: Qt.AlignHCenter
                    implicitWidth: 32; implicitHeight: 32
                }
            }
        }

        Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: Theme.border }

        // ---- Contenido ----
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: win.currentPage

            HomePage {}
            ChangesPage {}
            ConflictsPage {}
            MergePage {}
        }
    }

    // ---- Errores ----
    Dialog {
        id: errorDialog
        modal: true
        title: "Error"
        anchors.centerIn: parent
        width: Math.min(640, win.width - 80)
        standardButtons: Dialog.Ok
        background: Rectangle { color: Theme.panel; border.color: Theme.border; radius: Theme.radius }
        contentItem: Label {
            text: errorDialog.errorText
            color: Theme.text
            wrapMode: Text.Wrap
        }
        property string errorText: ""
    }
    Connections {
        target: App
        function onErrorOccurred(message) {
            errorDialog.errorText = message
            errorDialog.open()
        }
    }
}
