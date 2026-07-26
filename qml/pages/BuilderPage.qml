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
        if (a.combatProfile === "firstRun") combatFirst.checked = true
        else if (a.combatProfile === "none") combatNone.checked = true
        else combatFull.checked = true
        var cf = a.combatFeatures
        function hasCombatFeature(name) { return !cf || cf.indexOf(name) >= 0 }
        combatBeta.checked = hasCombatFeature("betaBurstDamage")
        combatDrone.checked = hasCombatFeature("droneDamage")
        combatDash.checked = hasCombatFeature("dashDamage")
        combatEve.checked = hasCombatFeature("eveDamage")
        combatEnemy.checked = hasCombatFeature("enemyDamage")
        combatDodge.checked = hasCombatFeature("perfectDodge")
        combatTachy.checked = hasCombatFeature("tachyDuration")
        combatVulnerability.checked = hasCombatFeature("enemyVulnerability")
        combatBlaster.checked = !a.gaugeTweaks || a.gaugeTweaks.indexOf("blasterCellX2") >= 0
        var cef = a.combatEconomyFeatures
        var legacyEconomy = a.combatEconomy === "full"
        economyGain.checked = cef ? cef.indexOf("slowerGain") >= 0 : legacyEconomy || a.combatEconomy === undefined
        economyCapacity.checked = cef ? cef.indexOf("lowerCapacity") >= 0 : legacyEconomy || a.combatEconomy === undefined
        economyCooldown.checked = cef ? cef.indexOf("cooldown") >= 0 : legacyEconomy || a.combatEconomy === undefined
        outfit.checked = a.outfitSkinSuit !== false
        if (a.miniBoss === "allRegions") minibossAll.checked = true
        else if (a.miniBoss === "greatDesert") minibossDesert.checked = true
        else minibossOff.checked = true
        if (a.miniBossDensity === "p10") density10.checked = true
        else if (a.miniBossDensity === "p33") density33.checked = true
        else density20.checked = true
        difficultyProgressive.checked = a.miniBossDifficulty === "progressive"
        difficultyFlat.checked = !difficultyProgressive.checked
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
        exTumbler.checked = ex.indexOf("tumblerHeal") >= 0
        var hm = Number(a.harderEnemiesMult || 2)
        harder2.checked = hm === 2; harder3.checked = hm === 3; harder4.checked = hm === 4
        harder5.checked = hm === 5; harder6.checked = hm === 6
        tomlField.text = a.customPatchesDir || ""
        helperRandom.checked = a.helperMode === "randomAny"
        helperPeriodic.checked = a.helperMode === "randomPeriodic"
        helperLast.checked = !helperRandom.checked && !helperPeriodic.checked
        if (a.helperIntervalSeconds) interval.value = a.helperIntervalSeconds
    }
    function toFolderUrl(txt) {
        if (txt.length === 0) return ""
        if (txt.indexOf("file:") === 0) return txt
        var p = txt.replace(/\\/g, "/")
        return p.indexOf("/") === 0 ? "file://" + p : "file:///" + p
    }
    function combatValue() { return combatFirst.checked ? "firstRun" : (combatNone.checked ? "none" : "full") }
    function miniBossValue() { return minibossAll.checked ? "allRegions" : (minibossDesert.checked ? "greatDesert" : "off") }
    function densityValue() { return density10.checked ? "p10" : (density33.checked ? "p33" : "p20") }
    function difficultyValue() { return difficultyProgressive.checked ? "progressive" : "flat" }
    function harderValue() { return harder6.checked ? 6 : (harder5.checked ? 5 : (harder4.checked ? 4 : (harder3.checked ? 3 : 2))) }
    function helperValue() { return helperPeriodic.checked ? "randomPeriodic" : (helperRandom.checked ? "randomAny" : "last") }

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
                        ButtonGroup { id: combatGroup }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 18
                            RadioButton { id: combatFull; checked: true; ButtonGroup.group: combatGroup
                                text: I18n.s.builder_combat_full || "Completo" }
                            RadioButton { id: combatFirst; ButtonGroup.group: combatGroup
                                text: I18n.s.builder_combat_firstRun || "First Run" }
                            RadioButton { id: combatNone; ButtonGroup.group: combatGroup
                                text: I18n.s.builder_combat_none || "Ninguno" }
                        }

                        FieldLabel {
                            text: I18n.s.builder_combat_changes || "Cambios de combate incluidos"
                            font.bold: true
                        }
                        Text {
                            Layout.fillWidth: true
                            text: I18n.s.builder_combat_changes_hint
                                  || "Cada opción corresponde a un cambio real de Stellar Souls. Podés combinarlas libremente."
                            color: Theme.textDim; font.pixelSize: 11; wrapMode: Text.Wrap
                        }
                        GridLayout {
                            Layout.fillWidth: true
                            columns: width >= 560 ? 2 : 1
                            columnSpacing: 18; rowSpacing: 4
                            component FeatureCheck: CheckBox {
                                Layout.fillWidth: true
                                checked: true
                                palette.windowText: Theme.text
                            }
                            FeatureCheck { id: combatBeta; text: I18n.s.builder_combat_beta || "Daño Beta/Burst reducido" }
                            FeatureCheck { id: combatDrone; text: I18n.s.builder_combat_drone || "Daño de drones reducido" }
                            FeatureCheck { id: combatDash; text: I18n.s.builder_combat_dash || "Dash cargado equilibrado" }
                            FeatureCheck { id: combatEve; text: I18n.s.builder_combat_eve || "Ataques normales de EVE reforzados" }
                            FeatureCheck { id: combatEnemy; text: I18n.s.builder_combat_enemy || "Daño de enemigos aumentado" }
                            FeatureCheck { id: combatDodge; text: I18n.s.builder_combat_dodge || "Perfect dodge sin lock-on" }
                            FeatureCheck { id: combatTachy; text: I18n.s.builder_combat_tachy || "Duración de Tachy reducida" }
                            FeatureCheck { id: combatVulnerability; text: I18n.s.builder_combat_vulnerability || "Enemigos reciben más daño" }
                            FeatureCheck { id: combatBlaster; text: I18n.s.builder_combat_blaster || "Blaster Cell reforzada (x2)" }
                        }

                        FieldLabel {
                            text: I18n.s.builder_combat_economy || "Economía Beta/Burst"
                            font.bold: true
                        }
                        GridLayout {
                            Layout.fillWidth: true; columns: width >= 560 ? 2 : 1
                            columnSpacing: 18; rowSpacing: 4
                            CheckBox { id: economyGain; checked: true; text: I18n.s.builder_economy_gain || "Recarga Beta 50% más lenta" }
                            CheckBox { id: economyCapacity; checked: true; text: I18n.s.builder_economy_capacity || "Capacidad Beta/Burst reducida" }
                            CheckBox { id: economyCooldown; checked: true; text: I18n.s.builder_economy_cooldown || "Cooldown de skills Beta/Burst: 3 s" }
                        }

                        RowLayout {
                            spacing: 10
                            CheckBox { id: outfit; checked: true }
                            Text { text: I18n.s.builder_q_outfit || "Skin Suit al romper escudo (necesita helper)"
                                   color: Theme.text; wrapMode: Text.Wrap; Layout.fillWidth: true }
                        }

                        FieldLabel { text: I18n.s.builder_q_miniboss || "Mini-bosses + drops NG+" }
                        ButtonGroup { id: minibossGroup }
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 2
                            RadioButton { id: minibossOff; checked: true; ButtonGroup.group: minibossGroup
                                text: I18n.s.builder_mb_off || "No" }
                            RadioButton { id: minibossAll; ButtonGroup.group: minibossGroup
                                text: I18n.s.builder_mb_all || "Sí, todas las regiones" }
                            RadioButton { id: minibossDesert; ButtonGroup.group: minibossGroup
                                text: I18n.s.builder_mb_gd || "Sí, solo Great Desert" }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 6
                            visible: !minibossOff.checked
                            FieldLabel { text: I18n.s.builder_q_density || "Densidad de mini-bosses" }
                            ButtonGroup { id: densityGroup }
                            RowLayout {
                                spacing: 18
                                RadioButton { id: density10; ButtonGroup.group: densityGroup; text: "10%" }
                                RadioButton { id: density20; checked: true; ButtonGroup.group: densityGroup; text: "20%" }
                                RadioButton { id: density33; ButtonGroup.group: densityGroup; text: "33%" }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 6
                            visible: !minibossOff.checked
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
                            ButtonGroup { id: difficultyGroup }
                            ColumnLayout {
                                spacing: 2
                                RadioButton { id: difficultyFlat; checked: true; ButtonGroup.group: difficultyGroup
                                    text: I18n.s.builder_diff_flat || "Pareja (misma densidad)" }
                                RadioButton { id: difficultyProgressive; ButtonGroup.group: difficultyGroup
                                    text: I18n.s.builder_diff_progressive || "Progresiva (zonas tardías más densas)" }
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
                                CheckBox { id: exTumbler }
                                Text { text: I18n.s.builder_ex_tumbler || "Tumbler: curación base 60%"
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
                                ButtonGroup { id: harderGroup }
                                RowLayout {
                                    visible: exHarder.checked
                                    spacing: 4
                                    RadioButton { id: harder2; checked: true; ButtonGroup.group: harderGroup; text: "x2" }
                                    RadioButton { id: harder3; ButtonGroup.group: harderGroup; text: "x3" }
                                    RadioButton { id: harder4; ButtonGroup.group: harderGroup; text: "x4" }
                                    RadioButton { id: harder5; ButtonGroup.group: harderGroup; text: "x5" }
                                    RadioButton { id: harder6; ButtonGroup.group: harderGroup; text: "x6" }
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
                            ButtonGroup { id: helperGroup }
                            ColumnLayout {
                                spacing: 2
                                RadioButton { id: helperLast; checked: true; ButtonGroup.group: helperGroup
                                    text: I18n.s.builder_helper_last || "Restaurar último outfit" }
                                RadioButton { id: helperRandom; ButtonGroup.group: helperGroup
                                    text: I18n.s.builder_helper_random || "Outfit aleatorio" }
                                RadioButton { id: helperPeriodic; ButtonGroup.group: helperGroup
                                    text: I18n.s.builder_helper_periodic || "Aleatorio + periódico" }
                            }
                        }
                        RowLayout {
                            spacing: 10
                            visible: outfit.checked && helperPeriodic.checked
                            FieldLabel { text: I18n.s.builder_q_interval || "Intervalo (s)" }
                            SpinBox { id: interval; from: 5; to: 600; value: 30; Layout.preferredWidth: 140 }
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
                        var mb = miniBossValue()
                        if (combatValue() === "firstRun" && mb === "off") mb = "allRegions"
                        var a = {
                            combatProfile: combatValue(),
                            combatEconomyFeatures: [
                                economyGain.checked ? "slowerGain" : "",
                                economyCapacity.checked ? "lowerCapacity" : "",
                                economyCooldown.checked ? "cooldown" : ""
                            ].filter(function(x){ return x.length > 0 }),
                            combatFeatures: [
                                combatBeta.checked ? "betaBurstDamage" : "",
                                combatDrone.checked ? "droneDamage" : "",
                                combatDash.checked ? "dashDamage" : "",
                                combatEve.checked ? "eveDamage" : "",
                                combatEnemy.checked ? "enemyDamage" : "",
                                combatDodge.checked ? "perfectDodge" : "",
                                combatTachy.checked ? "tachyDuration" : "",
                                combatVulnerability.checked ? "enemyVulnerability" : ""
                            ].filter(function(x){ return x.length > 0 }),
                            gaugeTweaks: combatBlaster.checked ? ["blasterCellX2"] : [],
                            outfitSkinSuit: outfit.checked,
                            miniBoss: mb,
                            miniBossDensity: densityValue(),
                            miniBossDifficulty: difficultyValue(),
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
                                exHarder.checked ? "harderEnemies" : "",
                                exTumbler.checked ? "tumblerHeal" : ""
                            ].filter(function(x){ return x.length > 0 }),
                            harderEnemiesMult: harderValue(),
                            customPatchesDir: tomlField.text,
                            helperMode: helperValue(),
                            helperIntervalSeconds: interval.value,
                            lang: I18n.language
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
