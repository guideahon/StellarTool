pragma Singleton
import QtQuick

QtObject {
    id: theme
    // Modo: true = oscuro (default), false = claro. Cambiar desde Settings.
    property bool dark: true

    readonly property color bg:       dark ? "#14161c" : "#f4f6fa"
    readonly property color panel:    dark ? "#1d2129" : "#ffffff"
    readonly property color panelAlt: dark ? "#232834" : "#eef1f6"
    readonly property color border:   dark ? "#323a49" : "#d3d9e2"
    readonly property color text:     dark ? "#e6e9ef" : "#1b2028"
    readonly property color textDim:  dark ? "#9aa3b2" : "#5b6472"
    readonly property color accent:   dark ? "#5aa2ff" : "#2f6fe0"
    readonly property color warn:     dark ? "#e6b455" : "#b7791f"
    readonly property color danger:   dark ? "#e06c75" : "#c0392b"
    readonly property color ok:       dark ? "#7fc97f" : "#2e8b57"
    readonly property int radius: 8
}
