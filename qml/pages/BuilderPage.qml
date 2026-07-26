import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import QtMultimedia
import ".."
import "../components"

// Stellar Souls Builder: cuestionario nativo que compila un mod personalizado
// llamando a App.runBuilder (QProcess -> build_custom.py).
Item {
    id: root

    property string resultZip: ""
    property string gamePath: ""
    property var historyModel: []
    property var installed: ({ paks: [], helper: false })

    Component.onCompleted: {
        gamePath = App.detectStellarBlade()
        refreshHistory()
        refreshInstalled()
    }
    function refreshHistory() {
        try { historyModel = JSON.parse(App.builderHistory() || "[]") }
        catch (e) { historyModel = [] }
    }
    function refreshInstalled() {
        try { installed = JSON.parse(App.installedStatus() || "{}") }
        catch (e) { installed = { paks: [], helper: false } }
    }
    function applyTemplate(a) {
        if (a.combatProfile) combat.currentIndex = combat.indexOfValue(a.combatProfile)
        outfit.checked = a.outfitSkinSuit !== false
        if (a.miniBoss) miniboss.currentIndex = miniboss.indexOfValue(a.miniBoss)
        if (a.miniBossDensity) density.currentIndex = density.indexOfValue(a.miniBossDensity)
        if (a.miniBossDifficulty) difficulty.currentIndex = difficulty.indexOfValue(a.miniBossDifficulty)
        variety.checked = a.enemyVariety === true
        var ex = a.gameplayExtras || []
        exQol.checked = ex.indexOf("playerQol") >= 0
        exTachy.checked = ex.indexOf("longerTachy") >= 0
        exDrain.checked = ex.indexOf("hpDrain") >= 0
        exFall.checked = ex.indexOf("noFallDamage") >= 0
        exEnv.checked = ex.indexOf("noEnvDeath") >= 0
        exTachyR.checked = ex.indexOf("tachyReduce") >= 0
        exGear.checked = ex.indexOf("strongerGear") >= 0
        exGauge.checked = ex.indexOf("autoGaugeRecovery") >= 0
        exJust.checked = ex.indexOf("forgivingJust") >= 0
        exAirDodge.checked = ex.indexOf("extraAirDodge") >= 0
        exHarder.checked = ex.indexOf("harderEnemies") >= 0
        if (a.harderEnemiesMult) harderMult.currentIndex = harderMult.indexOfValue(a.harderEnemiesMult)
        tomlField.text = a.customPatchesDir || ""
        if (a.helperMode) helper.currentIndex = helper.indexOfValue(a.helperMode)
        if (a.helperIntervalSeconds) interval.value = a.helperIntervalSeconds
        if (a.lang) lang.currentIndex = Math.max(0, lang.find(a.lang))
    }
    function toFolderUrl(txt) {
        if (txt.length === 0) return ""
        if (txt.indexOf("file:") === 0) return txt
        var p = txt.replace(/\\/g, "/")
        return p.indexOf("/") === 0 ? "file://" + p : "file:///" + p
    }

    Connections {
        target: App
        function onBuilderFinished(zipPath) { root.resultZip = zipPath; root.refreshHistory(); root.refreshInstalled() }
        function onUninstalled() { root.refreshInstalled() }
    }

    FolderDialog {
        id: outDialog
        onAccepted: { outField.text = outDialog.selectedFolder }
    }
    FolderDialog {
        id: tomlDialog
        onAccepted: { tomlField.text = tomlDialog.selectedFolder.toString().replace("file:///", "").replace(/\//g, "\\") }
    }

    // Confirmacion de desinstalacion (accion destructiva sobre el juego).
    Dialog {
        id: uninstallDialog
        property string what: ""
        function ask(w) { what = w; open() }
        anchors.centerIn: Overlay.overlay
        modal: true
        title: I18n.s.builder_uninstall_confirm_title || "¿Desinstalar?"
        standardButtons: Dialog.Yes | Dialog.No
        contentItem: Text {
            text: (I18n.s.builder_uninstall_confirm || "Se quitara del juego lo que esta herramienta instalo. Cerra Stellar Blade antes.")
            color: Theme.text; wrapMode: Text.Wrap; width: 340
        }
        onAccepted: uninstallDialog.what === "helper" ? App.uninstallHelper() : App.uninstallMod()
    }

    // Old-school keygen music (chiptune CC0 generado para este tool).
    SoundEffect {
        id: music
        source: "qrc:/assets/audio/keygen.wav"
        loops: SoundEffect.Infinite
        volume: 0.6
    }

    // ---- helpers de estilo ----
    component FieldLabel: Text { color: Theme.textDim; font.pixelSize: 13 }
    component Card: Rectangle {
        radius: Theme.radius; color: Theme.panel; border.color: Theme.border
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        // Columna central con ancho maximo para legibilidad.
        Item {
            width: parent.width
            implicitHeight: content.implicitHeight + 48

            ColumnLayout {
                id: content
                width: Math.min(parent.width - 48, 680)
                anchors.horizontalCenter: parent.horizontalCenter
                y: 24
                spacing: 16

                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: "Stellar Souls — " + (I18n.s.builder_title || "Crea tu mod")
                        color: Theme.text; font.pixelSize: 24; font.bold: true
                    }
                    // Old school music toggle
                    Rectangle {
                        Layout.preferredHeight: 34
                        Layout.preferredWidth: musicRow.implicitWidth + 20
                        radius: Theme.radius
                        color: music.playing ? Theme.accent : Theme.panel
                        border.color: Theme.border
                        RowLayout {
                            id: musicRow
                            anchors.centerIn: parent; spacing: 6
                            Text { text: "♪"; color: music.playing ? Theme.accentText : Theme.textDim; font.pixelSize: 15 }
                            Text {
                                text: I18n.s.builder_music || "Old school music"
                                color: music.playing ? Theme.accentText : Theme.text; font.pixelSize: 13
                            }
                        }
                        MouseArea {
                            anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                            onClicked: music.playing ? music.stop() : music.play()
                        }
                    }
                }

                // ---- Formulario ----
                Card {
                    Layout.fillWidth: true
                    implicitHeight: form.implicitHeight + 32
                    ColumnLayout {
                        id: form
                        anchors.fill: parent; anchors.margins: 16; spacing: 14

                        FieldLabel { text: I18n.s.builder_q_combat || "Perfil de combate" }
                        FieldCombo {
                            id: combat
                            Layout.fillWidth: true
                            textRole: "label"; valueRole: "value"
                            model: [
                                { label: (I18n.s.builder_combat_full || "Completo"), value: "full" },
                                { label: (I18n.s.builder_combat_firstRun || "First Run"), value: "firstRun" },
                                { label: (I18n.s.builder_combat_none || "Ninguno"), value: "none" }
                            ]
                        }

                        RowLayout {
                            spacing: 10
                            CheckBox { id: outfit; checked: true }
                            Text { text: I18n.s.builder_q_outfit || "Skin Suit al romper escudo (necesita helper)"
                                   color: Theme.text; wrapMode: Text.Wrap; Layout.fillWidth: true }
                        }

                        FieldLabel { text: I18n.s.builder_q_miniboss || "Mini-bosses + drops NG+" }
                        FieldCombo {
                            id: miniboss
                            Layout.fillWidth: true
                            textRole: "label"; valueRole: "value"
                            model: [
                                { label: (I18n.s.builder_mb_off || "No"), value: "off" },
                                { label: (I18n.s.builder_mb_all || "Sí, todas las regiones"), value: "allRegions" },
                                { label: (I18n.s.builder_mb_gd || "Sí, solo Great Desert"), value: "greatDesert" }
                            ]
                        }

                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 6
                            visible: miniboss.currentValue !== "off"
                            FieldLabel { text: I18n.s.builder_q_density || "Densidad de mini-bosses" }
                            FieldCombo {
                                id: density
                                Layout.fillWidth: true
                                textRole: "label"; valueRole: "value"; currentIndex: 1
                                model: [ { label: "10%", value: "p10" }, { label: "20%", value: "p20" }, { label: "33%", value: "p33" } ]
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 6
                            visible: miniboss.currentValue !== "off"
                            RowLayout {
                                spacing: 8
                                FieldLabel { text: I18n.s.builder_q_difficulty || "Curva de dificultad" }
                                Rectangle {
                                    radius: 4; color: Theme.warn
                                    implicitWidth: betaTxt.width + 12; implicitHeight: 18
                                    Text { id: betaTxt; anchors.centerIn: parent; text: "BETA"
                                           color: Theme.warnText; font.pixelSize: 10; font.bold: true }
                                }
                            }
                            FieldCombo {
                                id: difficulty
                                Layout.fillWidth: true
                                textRole: "label"; valueRole: "value"
                                model: [
                                    { label: (I18n.s.builder_diff_flat || "Pareja (misma densidad)"), value: "flat" },
                                    { label: (I18n.s.builder_diff_progressive || "Progresiva (zonas tardías más densas)"), value: "progressive" }
                                ]
                            }
                            Text {
                                Layout.fillWidth: true; wrapMode: Text.Wrap
                                text: "ⓘ " + (I18n.s.builder_beta_note || "Mejoras mini-boss (anti-farm + dificultad) — BETA, validá in-game.")
                                color: Theme.textDim; font.pixelSize: 11
                            }
                            RowLayout {
                                spacing: 10; Layout.topMargin: 4
                                CheckBox { id: variety }
                                Text { text: I18n.s.builder_q_variety || "Variedad de enemigos (elites + únicos en zonas repetitivas)"
                                       color: Theme.text; wrapMode: Text.Wrap; Layout.fillWidth: true }
                                Rectangle {
                                    radius: 4; color: Theme.warn
                                    implicitWidth: vbeta.width + 12; implicitHeight: 18
                                    Text { id: vbeta; anchors.centerIn: parent; text: "BETA"
                                           color: Theme.warnText; font.pixelSize: 10; font.bold: true }
                                }
                            }
                        }

                        // Extras y patches TOML: NO dependen de mini-boss.
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 6

                            // Extras de gameplay (BETA)
                            RowLayout {
                                spacing: 6; Layout.topMargin: 8
                                FieldLabel { text: I18n.s.builder_extras || "Extras de gameplay" }
                                Rectangle { radius: 4; color: Theme.warn; implicitWidth: xbeta.width+12; implicitHeight: 18
                                    Text { id: xbeta; anchors.centerIn: parent; text: "BETA"; color: Theme.warnText; font.pixelSize: 10; font.bold: true } }
                            }
                            RowLayout { spacing: 10
                                CheckBox { id: exQol }
                                Text { text: I18n.s.builder_ex_qol || "Player QoL (stacks altos, más shield-regen, attack speed)"
                                       color: Theme.text; wrapMode: Text.Wrap; Layout.fillWidth: true } }
                            RowLayout { spacing: 10
                                CheckBox { id: exTachy }
                                Text { text: I18n.s.builder_ex_tachy || "Tachy más largo"
                                       color: Theme.text; wrapMode: Text.Wrap; Layout.fillWidth: true } }
                            RowLayout { spacing: 10
                                CheckBox { id: exDrain }
                                Text { text: I18n.s.builder_ex_drain || "HP Drain (curás al pegar)"
                                       color: Theme.text; wrapMode: Text.Wrap; Layout.fillWidth: true } }
                            RowLayout { spacing: 10
                                CheckBox { id: exFall }
                                Text { text: I18n.s.builder_ex_fall || "Sin daño de caída"
                                       color: Theme.text; wrapMode: Text.Wrap; Layout.fillWidth: true } }
                            RowLayout { spacing: 10
                                CheckBox { id: exEnv }
                                Text { text: I18n.s.builder_ex_env || "Sin muerte por agua/arena"
                                       color: Theme.text; wrapMode: Text.Wrap; Layout.fillWidth: true } }
                            RowLayout { spacing: 10
                                CheckBox { id: exTachyR }
                                Text { text: I18n.s.builder_ex_tachyr || "Menos consumo de Tachy"
                                       color: Theme.text; wrapMode: Text.Wrap; Layout.fillWidth: true } }
                            RowLayout { spacing: 10
                                CheckBox { id: exGear }
                                Text { text: I18n.s.builder_ex_gear || "Engranajes más fuertes (x2)"
                                       color: Theme.text; wrapMode: Text.Wrap; Layout.fillWidth: true } }
                            RowLayout { spacing: 10
                                CheckBox { id: exGauge }
                                Text { text: I18n.s.builder_ex_gauge || "Beta al parry perfecto / Burst al dodge perfecto (sin skill tree)"
                                       color: Theme.text; wrapMode: Text.Wrap; Layout.fillWidth: true } }
                            RowLayout { spacing: 10
                                CheckBox { id: exJust }
                                Text { text: I18n.s.builder_ex_just || "Ventana de parry/dodge perfecto más amplia (x1.5)"
                                       color: Theme.text; wrapMode: Text.Wrap; Layout.fillWidth: true } }
                            RowLayout { spacing: 10
                                CheckBox { id: exAirDodge }
                                Text { text: I18n.s.builder_ex_airdodge || "Doble esquive aéreo"
                                       color: Theme.text; wrapMode: Text.Wrap; Layout.fillWidth: true } }
                            RowLayout { spacing: 10
                                CheckBox { id: exHarder }
                                Text { text: I18n.s.builder_ex_harder || "Enemigos más duros"
                                       color: Theme.text; Layout.fillWidth: false }
                                FieldCombo {
                                    id: harderMult
                                    Layout.preferredWidth: 90
                                    visible: exHarder.checked
                                    textRole: "label"; valueRole: "value"
                                    model: [ {label:"x2",value:2},{label:"x3",value:3},{label:"x4",value:4},{label:"x5",value:5},{label:"x6",value:6} ]
                                }
                            }

                            // Patches TOML propios (opcional, avanzado)
                            RowLayout {
                                spacing: 6; Layout.topMargin: 8
                                FieldLabel { text: I18n.s.builder_toml || "Patches TOML propios (carpeta <Tabla>.toml)" }
                                Rectangle { radius: 4; color: Theme.warn; implicitWidth: tbeta.width+12; implicitHeight: 18
                                    Text { id: tbeta; anchors.centerIn: parent; text: "BETA"; color: Theme.warnText; font.pixelSize: 10; font.bold: true } }
                            }
                            RowLayout {
                                spacing: 8; Layout.fillWidth: true
                                TextField {
                                    id: tomlField
                                    Layout.fillWidth: true; color: Theme.text
                                    placeholderText: I18n.s.builder_toml_ph || "(opcional) carpeta con patches TOML"
                                    background: Rectangle { radius: Theme.radius; color: Theme.panelAlt; border.color: Theme.border }
                                }
                                Button { text: "📁"; onClicked: tomlDialog.open() }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 6
                            visible: outfit.checked
                            FieldLabel { text: I18n.s.builder_q_helper || "Comportamiento del outfit CNS" }
                            FieldCombo {
                                id: helper
                                Layout.fillWidth: true
                                textRole: "label"; valueRole: "value"
                                model: [
                                    { label: (I18n.s.builder_helper_last || "Restaurar último outfit"), value: "last" },
                                    { label: (I18n.s.builder_helper_random || "Outfit aleatorio"), value: "randomAny" },
                                    { label: (I18n.s.builder_helper_periodic || "Aleatorio + periódico"), value: "randomPeriodic" }
                                ]
                            }
                        }
                        RowLayout {
                            spacing: 10
                            visible: outfit.checked && helper.currentValue === "randomPeriodic"
                            FieldLabel { text: I18n.s.builder_q_interval || "Intervalo (s)" }
                            SpinBox { id: interval; from: 5; to: 600; value: 30; Layout.preferredWidth: 140 }
                        }

                        FieldLabel { text: I18n.s.builder_q_lang || "Idioma" }
                        FieldCombo {
                            id: lang
                            Layout.preferredWidth: 220
                            model: ["es","en","fr","it","de","ja","ko","pt_BR","ru","zh_Hans"]
                        }

                        FieldLabel { text: I18n.s.builder_out || "Carpeta de salida del ZIP" }
                        RowLayout {
                            spacing: 8; Layout.fillWidth: true
                            TextField {
                                id: outField
                                Layout.fillWidth: true
                                color: Theme.text
                                text: App.defaultOutDir()
                                background: Rectangle { radius: Theme.radius; color: Theme.panelAlt; border.color: Theme.border }
                            }
                            Button { text: "📁"; onClicked: outDialog.open() }
                        }
                    }
                }

                // ---- Juego + instalacion ----
                Card {
                    Layout.fillWidth: true
                    implicitHeight: instCol.implicitHeight + 28
                    ColumnLayout {
                        id: instCol
                        anchors.fill: parent; anchors.margins: 14; spacing: 8
                        Text {
                            text: (root.gamePath.length > 0
                                   ? "✓ " + (I18n.s.builder_game_found || "Juego detectado") + ": " + root.gamePath
                                   : "⚠ " + (I18n.s.builder_game_missing || "Juego no detectado — se generara ZIP para instalar manual"))
                            color: root.gamePath.length > 0 ? Theme.ok : Theme.warn
                            wrapMode: Text.WrapAnywhere; Layout.fillWidth: true
                        }
                        RowLayout {
                            spacing: 10; enabled: root.gamePath.length > 0
                            CheckBox { id: instPaks }
                            Text { text: I18n.s.builder_install_paks || "Instalar el mod directamente en ~mods"
                                   color: enabled ? Theme.text : Theme.textDim; wrapMode: Text.Wrap; Layout.fillWidth: true }
                        }
                        RowLayout {
                            spacing: 10; enabled: root.gamePath.length > 0 && outfit.checked
                            CheckBox { id: instHelper }
                            Text { text: I18n.s.builder_install_helper || "Instalar y activar el helper (edita mods.txt)"
                                   color: enabled ? Theme.text : Theme.textDim; wrapMode: Text.Wrap; Layout.fillWidth: true }
                        }
                        Text {
                            visible: instPaks.checked || instHelper.checked
                            text: "⚠ " + (I18n.s.builder_install_warn || "Se modificaran archivos del juego. Cerra Stellar Blade antes.")
                            color: Theme.warn; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true
                        }
                    }
                }

                Button {
                    Layout.fillWidth: true
                    text: I18n.s.builder_build || "Compilar mi mod"
                    enabled: !App.busy && outField.text.length > 0
                    onClicked: {
                        var mb = miniboss.currentValue
                        if (combat.currentValue === "firstRun" && mb === "off") mb = "allRegions"
                        var a = {
                            combatProfile: combat.currentValue,
                            outfitSkinSuit: outfit.checked,
                            miniBoss: mb,
                            miniBossDensity: density.currentValue,
                            miniBossDifficulty: difficulty.currentValue,
                            enemyVariety: variety.checked,
                            gameplayExtras: [
                                exQol.checked ? "playerQol" : "",
                                exTachy.checked ? "longerTachy" : "",
                                exDrain.checked ? "hpDrain" : "",
                                exFall.checked ? "noFallDamage" : "",
                                exEnv.checked ? "noEnvDeath" : "",
                                exTachyR.checked ? "tachyReduce" : "",
                                exGear.checked ? "strongerGear" : "",
                                exGauge.checked ? "autoGaugeRecovery" : "",
                                exJust.checked ? "forgivingJust" : "",
                                exAirDodge.checked ? "extraAirDodge" : "",
                                exHarder.checked ? "harderEnemies" : ""
                            ].filter(function(x){ return x.length > 0 }),
                            harderEnemiesMult: harderMult.currentValue || 2,
                            customPatchesDir: tomlField.text,
                            helperMode: helper.currentValue,
                            helperIntervalSeconds: interval.value,
                            lang: lang.currentText
                        }
                        root.resultZip = ""
                        App.runBuilder(JSON.stringify(a), toFolderUrl(outField.text),
                                       instPaks.checked, instHelper.checked, root.gamePath)
                    }
                }

                Text { visible: App.busy; text: App.statusText; color: Theme.textDim }

                Card {
                    visible: root.resultZip.length > 0
                    Layout.fillWidth: true
                    color: Theme.panelAlt
                    implicitHeight: resCol.implicitHeight + 24
                    ColumnLayout {
                        id: resCol
                        anchors.fill: parent; anchors.margins: 12; spacing: 6
                        Text { text: "✓ " + (I18n.s.builder_done || "Listo. Tu mod:"); color: Theme.ok; font.bold: true }
                        Text { text: root.resultZip; color: Theme.textDim; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true }
                    }
                }

                // ---- Instalado por la tool (desinstalar) ----
                Card {
                    visible: (root.installed.paks && root.installed.paks.length > 0) || root.installed.helper
                    Layout.fillWidth: true
                    color: Theme.panelAlt
                    implicitHeight: uninstCol.implicitHeight + 24
                    ColumnLayout {
                        id: uninstCol
                        anchors.fill: parent; anchors.margins: 12; spacing: 8
                        Text { text: I18n.s.builder_installed || "Instalado por esta herramienta"
                               color: Theme.text; font.bold: true }
                        RowLayout {
                            visible: root.installed.paks && root.installed.paks.length > 0
                            spacing: 10; Layout.fillWidth: true
                            Text { Layout.fillWidth: true; color: Theme.textDim; wrapMode: Text.WrapAnywhere
                                   text: "🧩 " + (root.installed.paks ? root.installed.paks.join(", ") : "") }
                            Button { text: I18n.s.builder_uninstall_mod || "Desinstalar mod"
                                     enabled: !App.busy; onClicked: uninstallDialog.ask("mod") }
                        }
                        RowLayout {
                            visible: root.installed.helper
                            spacing: 10; Layout.fillWidth: true
                            Text { Layout.fillWidth: true; color: Theme.textDim; text: "🧵 Helper (StellarSoulsOutfitRestore)" }
                            Button { text: I18n.s.builder_uninstall_helper || "Desinstalar helper"
                                     enabled: !App.busy; onClicked: uninstallDialog.ask("helper") }
                        }
                    }
                }

                // ---- Historial ----
                ColumnLayout {
                    visible: root.historyModel.length > 0
                    Layout.fillWidth: true; spacing: 8
                    Text { text: I18n.s.builder_history || "Configuraciones anteriores"
                           color: Theme.text; font.bold: true; font.pixelSize: 16 }
                    Repeater {
                        model: root.historyModel
                        delegate: Card {
                            Layout.fillWidth: true
                            color: Theme.panelAlt
                            implicitHeight: hrow.implicitHeight + 16
                            RowLayout {
                                id: hrow
                                anchors.fill: parent; anchors.margins: 8; spacing: 10
                                Text { Layout.fillWidth: true
                                       text: modelData.label + "  ·  " + modelData.timestamp; color: Theme.text }
                                Button { text: I18n.s.builder_use_template || "Usar de plantilla"
                                         onClicked: root.applyTemplate(modelData.answers) }
                                Button { text: I18n.s.builder_reexport || "Re-exportar ZIP"
                                         enabled: !App.busy && outField.text.length > 0
                                         onClicked: App.reexportBuild(modelData.id, toFolderUrl(outField.text)) }
                            }
                        }
                    }
                }
            }
        }
    }
}
