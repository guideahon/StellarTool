pragma Singleton
import QtQuick

// Tema visual con 3 modos: dark (default), light y oled (negro perfecto).
// Todos los colores derivan de App.themeMode persistido en QSettings.
// OLED reserva #000000 para el fondo y eleva solo controles para contraste.
QtObject {
    id: theme
    // Fuente única persistida en AppController/QSettings.
    readonly property string mode: App.themeMode
    readonly property bool dark: mode !== "light"
    readonly property bool oled: mode === "oled"

    // Dark usa superficies casi negras; OLED reserva negro perfecto para el
    // lienzo y sólo eleva los controles lo imprescindible para distinguirlos.
    readonly property color bg:       oled ? "#000000" : (dark ? "#090b0f" : "#f3f5f8")
    readonly property color panel:    oled ? "#050505" : (dark ? "#101319" : "#ffffff")
    readonly property color panelAlt: oled ? "#0d0d0d" : (dark ? "#181c24" : "#e9edf3")
    readonly property color border:   oled ? "#262626" : (dark ? "#29313d" : "#cbd3de")
    readonly property color text:     dark ? "#f1f4f8" : "#171b22"
    readonly property color textDim:  oled ? "#a8a8a8" : (dark ? "#a7b0bd" : "#566170")
    readonly property color accent:   oled ? "#66aaff" : (dark ? "#62a6ff" : "#2868d8")
    readonly property color warn:     dark ? "#f0bd62" : "#a96813"
    readonly property color danger:   dark ? "#f07882" : "#bc3428"
    readonly property color ok:       dark ? "#78d58b" : "#257a49"
    readonly property color accentText: "#ffffff"
    readonly property color warnText:   "#171b22"
    readonly property int radius: 8
}
