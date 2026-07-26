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
        var cf = a.combatFeatures
        function hasCombatFeature(name) { return !cf || cf.indexOf(name) >= 0 }
        betaRow.selected = hasCombatFeature("betaBurstDamage")
        droneRow.selected = hasCombatFeature("droneDamage")
        dashRow.selected = hasCombatFeature("dashDamage")
        eveRow.selected = hasCombatFeature("eveDamage")
        enemyRow.selected = hasCombatFeature("enemyDamage")
        combatDodge.checked = hasCombatFeature("perfectDodge")
        tachyRow.selected = hasCombatFeature("tachyDuration")
        vulnRow.selected = hasCombatFeature("enemyVulnerability")
        blasterRow.selected = !a.gaugeTweaks || a.gaugeTweaks.indexOf("blasterCellX2") >= 0
        var cef = a.combatEconomyFeatures
        var legacyEconomy = a.combatEconomy === "full"
        gainRow.selected = cef ? cef.indexOf("slowerGain") >= 0 : legacyEconomy || a.combatEconomy === undefined
        capacityRow.selected = cef ? cef.indexOf("lowerCapacity") >= 0 : legacyEconomy || a.combatEconomy === undefined
        cooldownRow.selected = cef ? cef.indexOf("cooldown") >= 0 : legacyEconomy || a.combatEconomy === undefined
        outfit.checked = a.outfitSkinSuit !== false
        var densities = a.miniBossRegionDensity || {}
        function loadRegion(check, spin, code) {
            var value = Number(densities[code] || 0); check.checked = value > 0
            if (value > 0) spin.value = value
        }
        function loadRegionRow(row, code) {
            var value = Number(densities[code] || 0); row.selected = value > 0
            if (value > 0) row.density = value
        }
        loadRegionRow(wlaRow, "WLA"); loadRegionRow(atlRow, "ATL")
        loadRegionRow(meRow, "ME"); loadRegionRow(wlbRow, "WLB")
        loadRegionRow(aylRow, "AYL"); loadRegionRow(ded40Row, "DED40")
        loadRegionRow(dedaRow, "DEDA"); loadRegionRow(seRow, "SE")
        variety.checked = a.enemyVariety === true
        var ex = a.gameplayExtras || []
        var legacyQol = ex.indexOf("playerQol") >= 0
        exAmmo.checked = legacyQol || ex.indexOf("ammoStacks") >= 0
        exConsumables.checked = legacyQol || ex.indexOf("consumableStacks") >= 0
        exShieldRegen.checked = legacyQol || ex.indexOf("shieldRegen") >= 0
        exAttackSpeed.checked = legacyQol || ex.indexOf("attackSpeed") >= 0
        exTachy.checked = ex.indexOf("longerTachy") >= 0
        exDrain.checked = ex.indexOf("hpDrain") >= 0
        exFall.checked = ex.indexOf("noFallDamage") >= 0
        exWater.checked = ex.indexOf("noEnvDeath") >= 0 || ex.indexOf("noWaterDeath") >= 0
        exSand.checked = ex.indexOf("noEnvDeath") >= 0 || ex.indexOf("noSandDeath") >= 0
        exTachyR.checked = ex.indexOf("tachyReduce") >= 0
        exGear.checked = ex.indexOf("strongerGear") >= 0
        exBetaParry.checked = ex.indexOf("autoGaugeRecovery") >= 0 || ex.indexOf("betaParryRecovery") >= 0
        exBurstDodge.checked = ex.indexOf("autoGaugeRecovery") >= 0 || ex.indexOf("burstDodgeRecovery") >= 0
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
    function setCombatPreset(kind) {
        var on = kind !== "vanilla"
        betaRow.selected=on; droneRow.selected=on; dashRow.selected=on
        eveRow.selected=on; enemyRow.selected=on; combatDodge.checked=on
        tachyRow.selected=on; vulnRow.selected=on; blasterRow.selected=on
        gainRow.selected=on; capacityRow.selected=on; cooldownRow.selected=on
        if (kind === "first") {
            betaMild.checked=true; droneMild.checked=true; dashMild.checked=true
            eveMild.checked=true; enemyMild.checked=true; tachyMild.checked=true
            vulnMild.checked=true; blaster2.checked=true; gain25.checked=true
            capacityFirst.checked=true; cooldown2.checked=true
        } else if (kind === "full") {
            betaFull.checked=true; droneFull.checked=true; dashFull.checked=true
            eveFull.checked=true; enemyFull.checked=true; tachyFull.checked=true
            vulnFull.checked=true; blaster3.checked=true; gain50.checked=true
            capacityFull.checked=true; cooldown3.checked=true
        }
    }
    function anyMiniBossRegion() {
        return wlaRow.selected || atlRow.selected || meRow.selected || wlbRow.selected ||
               aylRow.selected || ded40Row.selected || dedaRow.selected || seRow.selected
    }
    function miniBossDensities() {
        return {WLA:wlaRow.selected?wlaRow.density:0, ATL:atlRow.selected?atlRow.density:0,
                ME:meRow.selected?meRow.density:0, WLB:wlbRow.selected?wlbRow.density:0,
                AYL:aylRow.selected?aylRow.density:0, DED40:ded40Row.selected?ded40Row.density:0,
                DEDA:dedaRow.selected?dedaRow.density:0, SE:seRow.selected?seRow.density:0}
    }
    function setMiniBossPreset(on) {
        wlaRow.selected=on; atlRow.selected=on; meRow.selected=on; wlbRow.selected=on
        aylRow.selected=on; ded40Row.selected=on; dedaRow.selected=on; seRow.selected=on
        if (on) {
            wlaRow.density=5; atlRow.density=5; meRow.density=10; wlbRow.density=10
            aylRow.density=10; ded40Row.density=15; dedaRow.density=15; seRow.density=15
            mbHealthRow.selected=true; mbAttackRow.selected=true; mbScaleRow.selected=true
            mbShield.checked=true; mbRewards.checked=true; mbXp.checked=true
            mbPersistent.checked=true; mbBossType.checked=true; mbExecution.checked=true
        }
    }
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

                        FieldLabel { text: "Presets de combate (solo preseleccionan opciones)" }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 18
                            Button { text: "Aplicar Full"; onClicked: root.setCombatPreset("full") }
                            Button { text: "Aplicar First Run"; onClicked: root.setCombatPreset("first") }
                            Button { text: "Restaurar vanilla"; onClicked: root.setCombatPreset("vanilla") }
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
                        component ChangeRow: RowLayout {
                            property alias label: toggle.text
                            property alias selected: toggle.checked
                            default property alias choices: choiceBox.data
                            Layout.fillWidth: true; spacing: 8
                            CheckBox { id: toggle; checked: true; Layout.preferredWidth: 330 }
                            RowLayout { id: choiceBox; enabled: toggle.checked; spacing: 3 }
                        }
                        ChangeRow { id: betaRow; label: "Daño Beta/Burst"
                            CheckBox { id: combatBeta; checked: betaRow.selected; visible: false }
                            ButtonGroup { id: betaGroup } RadioButton { id: betaMild; text: "÷2"; ButtonGroup.group: betaGroup }
                            RadioButton { id: betaFull; text: "÷3"; checked: true; ButtonGroup.group: betaGroup } }
                        ChangeRow { id: droneRow; label: "Daño de drones"
                            CheckBox { id: combatDrone; checked: droneRow.selected; visible: false }
                            ButtonGroup { id: droneGroup } RadioButton { id: droneMild; text: "÷2"; ButtonGroup.group: droneGroup }
                            RadioButton { id: droneFull; text: "÷3"; checked: true; ButtonGroup.group: droneGroup } }
                        ChangeRow { id: dashRow; label: "Daño de dash cargado"
                            CheckBox { id: combatDash; checked: dashRow.selected; visible: false }
                            ButtonGroup { id: dashGroup } RadioButton { id: dashMild; text: "x0,75"; ButtonGroup.group: dashGroup }
                            RadioButton { id: dashFull; text: "÷2"; checked: true; ButtonGroup.group: dashGroup } }
                        ChangeRow { id: eveRow; label: "Ataques normales de EVE"
                            CheckBox { id: combatEve; checked: eveRow.selected; visible: false }
                            ButtonGroup { id: eveGroup } RadioButton { id: eveMild; text: "x1,5"; ButtonGroup.group: eveGroup }
                            RadioButton { id: eveFull; text: "x3"; checked: true; ButtonGroup.group: eveGroup } }
                        ChangeRow { id: enemyRow; label: "Daño de enemigos"
                            CheckBox { id: combatEnemy; checked: enemyRow.selected; visible: false }
                            ButtonGroup { id: enemyGroup } RadioButton { id: enemyMild; text: "x1,5"; ButtonGroup.group: enemyGroup }
                            RadioButton { id: enemyFull; text: "x3"; checked: true; ButtonGroup.group: enemyGroup } }
                        CheckBox { id: combatDodge; checked: true; text: "Perfect dodge sin lock-on" }
                        ChangeRow { id: tachyRow; label: "Duración de Tachy"
                            CheckBox { id: combatTachy; checked: tachyRow.selected; visible: false }
                            ButtonGroup { id: tachyGroup } RadioButton { id: tachyMild; text: "x0,75"; ButtonGroup.group: tachyGroup }
                            RadioButton { id: tachyFull; text: "÷2"; checked: true; ButtonGroup.group: tachyGroup } }
                        ChangeRow { id: vulnRow; label: "Daño recibido por enemigos"
                            CheckBox { id: combatVulnerability; checked: vulnRow.selected; visible: false }
                            ButtonGroup { id: vulnGroup } RadioButton { id: vulnMild; text: "x1,25"; ButtonGroup.group: vulnGroup }
                            RadioButton { id: vulnFull; text: "x1,5"; checked: true; ButtonGroup.group: vulnGroup } }
                        ChangeRow { id: blasterRow; label: "Daño de Blaster Cell"
                            CheckBox { id: combatBlaster; checked: blasterRow.selected; visible: false }
                            ButtonGroup { id: blasterGroup } RadioButton { id: blaster2; text: "x2"; ButtonGroup.group: blasterGroup }
                            RadioButton { id: blaster3; text: "x3"; checked: true; ButtonGroup.group: blasterGroup } }

                        FieldLabel {
                            text: I18n.s.builder_combat_economy || "Economía Beta/Burst"
                            font.bold: true
                        }
                        ChangeRow { id: gainRow; label: "Ganancia de Beta"
                            CheckBox { id: economyGain; checked: gainRow.selected; visible: false }
                            ButtonGroup { id: gainGroup } RadioButton { id: gain25; text: "-25%"; ButtonGroup.group: gainGroup }
                            RadioButton { id: gain50; text: "-50%"; checked: true; ButtonGroup.group: gainGroup } }
                        ChangeRow { id: capacityRow; label: "Capacidad Beta/Burst"
                            CheckBox { id: economyCapacity; checked: capacityRow.selected; visible: false }
                            ButtonGroup { id: capacityGroup } RadioButton { id: capacityFirst; text: "700 / 1200"; ButtonGroup.group: capacityGroup }
                            RadioButton { id: capacityFull; text: "400 / 800"; checked: true; ButtonGroup.group: capacityGroup } }
                        ChangeRow { id: cooldownRow; label: "Cooldown Beta/Burst"
                            CheckBox { id: economyCooldown; checked: cooldownRow.selected; visible: false }
                            ButtonGroup { id: cooldownGroup } RadioButton { id: cooldown2; text: "2 s"; ButtonGroup.group: cooldownGroup }
                            RadioButton { id: cooldown3; text: "3 s"; checked: true; ButtonGroup.group: cooldownGroup } }

                        RowLayout {
                            spacing: 10
                            CheckBox { id: outfit; checked: true }
                            Text { text: I18n.s.builder_q_outfit || "Skin Suit al romper escudo (necesita helper)"
                                   color: Theme.text; wrapMode: Text.Wrap; Layout.fillWidth: true }
                        }

                        RowLayout {
                            FieldLabel { text: "Mini-bosses por región"; font.bold: true; Layout.fillWidth: true }
                            Button { text: "Aplicar preset NG+"; onClicked: root.setMiniBossPreset(true) }
                            Button { text: "Desmarcar"; onClicked: root.setMiniBossPreset(false) }
                        }
                        Text { text: "Cada región se activa por separado y acepta una densidad de 1% a 100%."
                               color: Theme.textDim; font.pixelSize: 11 }
                        component RegionRow: RowLayout {
                            property alias selected: regionCheck.checked
                            property alias label: regionCheck.text
                            property alias density: regionPct.value
                            Layout.fillWidth: true
                            CheckBox { id: regionCheck; Layout.preferredWidth: 260 }
                            FieldLabel { text: "Densidad" }
                            SpinBox { id: regionPct; from: 1; to: 100; value: 10; enabled: regionCheck.checked }
                            FieldLabel { text: "%" }
                        }
                        RegionRow { id: wlaRow; label: "Wasteland" }
                        RegionRow { id: atlRow; label: "Altess Levoire" }
                        RegionRow { id: meRow; label: "Matrix 11" }
                        RegionRow { id: wlbRow; label: "Great Desert" }
                        RegionRow { id: aylRow; label: "Abyss Levoire" }
                        RegionRow { id: ded40Row; label: "Eidos 7 / DED40" }
                        RegionRow { id: dedaRow; label: "Eidos 9 / DEDA" }
                        RegionRow { id: seRow; label: "Spire 4" }

                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 6
                            visible: root.anyMiniBossRegion()
                            FieldLabel { text: "Características de mini-boss"; font.bold: true }
                            ChangeRow { id: mbHealthRow; label: "Aumentar vida"
                                CheckBox { id: mbHealth; checked:mbHealthRow.selected; visible:false }
                                ButtonGroup { id: mbHpGroup } RadioButton { id: mbHp15; text:"x1,5"; ButtonGroup.group:mbHpGroup }
                                RadioButton { id: mbHp3; text:"x3"; ButtonGroup.group:mbHpGroup }
                                RadioButton { id: mbHp45; text:"x4,5"; checked:true; ButtonGroup.group:mbHpGroup } }
                            ChangeRow { id: mbAttackRow; label: "Aumentar ataque"
                                CheckBox { id: mbAttack; checked:mbAttackRow.selected; visible:false }
                                ButtonGroup { id: mbAtkGroup } RadioButton { id: mbAtk13; text:"x1,3"; ButtonGroup.group:mbAtkGroup }
                                RadioButton { id: mbAtk16; text:"x1,6"; checked:true; ButtonGroup.group:mbAtkGroup }
                                RadioButton { id: mbAtk2; text:"x2"; ButtonGroup.group:mbAtkGroup }
                                RadioButton { id: mbAtk3; text:"x3"; ButtonGroup.group:mbAtkGroup } }
                            ChangeRow { id: mbScaleRow; label: "Aumentar tamaño"
                                CheckBox { id: mbScale; checked:mbScaleRow.selected; visible:false }
                                ButtonGroup { id: mbScaleGroup } RadioButton { id: mbScale12; text:"x1,2"; ButtonGroup.group:mbScaleGroup }
                                RadioButton { id: mbScale16; text:"x1,6"; checked:true; ButtonGroup.group:mbScaleGroup }
                                RadioButton { id: mbScale2; text:"x2"; ButtonGroup.group:mbScaleGroup } }
                            CheckBox { id: mbShield; checked:true; text:"Quitar escudo" }
                            CheckBox { id: mbRewards; checked:true; text:"Agregar recompensas y drops NG+" }
                            CheckBox { id: mbXp; checked:true; text:"Aumentar XP" }
                            CheckBox { id: mbPersistent; checked:true; text:"Persistencia de muerte" }
                            CheckBox { id: mbBossType; checked:true; text:"Tratar como tipo Boss" }
                            CheckBox { id: mbExecution; checked:true; text:"Inmunidad a ejecución instantánea" }
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
                            CheckBox { id: exAmmo; text: "Munición acumulable: 999" }
                            CheckBox { id: exConsumables; text: "Consumibles acumulables: 99" }
                            CheckBox { id: exShieldRegen; text: "Regeneración de escudo aumentada" }
                            CheckBox { id: exAttackSpeed; text: "Velocidad de ataque x1,3" }
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
                            CheckBox { id: exWater; text: "Sin muerte por agua profunda" }
                            CheckBox { id: exSand; text: "Sin muerte por arena" }
                            RowLayout { spacing: 10
                                CheckBox { id: exTachyR }
                                Text { text: I18n.s.builder_ex_tachyr || "Menos consumo de Tachy"
                                       color: Theme.text; wrapMode: Text.Wrap; Layout.fillWidth: true } }
                            RowLayout { spacing: 10
                                CheckBox { id: exGear }
                                Text { text: I18n.s.builder_ex_gear || "Engranajes más fuertes (x2)"
                                       color: Theme.text; wrapMode: Text.Wrap; Layout.fillWidth: true } }
                            CheckBox { id: exBetaParry; text: "Beta al parry perfecto (sin skill tree)" }
                            CheckBox { id: exBurstDodge; text: "Burst al dodge perfecto (sin skill tree)" }
                            RowLayout { spacing: 10
                                CheckBox { id: exTumbler }
                                Text { text: I18n.s.builder_ex_tumbler || "Tumbler: curación base 60%"
                                       color: Theme.text; wrapMode: Text.Wrap; Layout.fillWidth: true } }
                            RowLayout { spacing: 10
                                CheckBox { id: exJust }
                                Text { text: I18n.s.builder_ex_just || "Ventana de parry/dodge perfecto más amplia (x1.5)"
                                       color: Theme.text; wrapMode: Text.Wrap; Layout.fillWidth: true }
                                ButtonGroup { id: justGroup }
                                RadioButton { id: just15; checked:true; text:"x1,5"; ButtonGroup.group:justGroup; enabled:exJust.checked }
                                RadioButton { id: just2; text:"x2"; ButtonGroup.group:justGroup; enabled:exJust.checked }
                                RadioButton { id: just3; text:"x3"; ButtonGroup.group:justGroup; enabled:exJust.checked } }
                            RowLayout { spacing: 10
                                CheckBox { id: exAirDodge }
                                Text { text: I18n.s.builder_ex_airdodge || "Doble esquive aéreo"
                                       color: Theme.text; wrapMode: Text.Wrap; Layout.fillWidth: true }
                                ButtonGroup { id: airGroup }
                                RadioButton { id: air2; checked:true; text:"x2"; ButtonGroup.group:airGroup; enabled:exAirDodge.checked }
                                RadioButton { id: air3; text:"x3"; ButtonGroup.group:airGroup; enabled:exAirDodge.checked } }
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
                        var mb = anyMiniBossRegion() ? "allRegions" : "off"
                        var a = {
                            combatProfile: "full",
                            combatEconomyFeatures: [
                                gainRow.selected ? "slowerGain" : "",
                                capacityRow.selected ? "lowerCapacity" : "",
                                cooldownRow.selected ? "cooldown" : ""
                            ].filter(function(x){ return x.length > 0 }),
                            combatEconomyLevels: {
                                slowerGain: gain25.checked ? -0.25 : -0.5,
                                lowerCapacity: capacityFirst.checked
                                    ? {MaxBetaGauge:700, MaxBurstGauge:1200}
                                    : {MaxBetaGauge:400, MaxBurstGauge:800},
                                cooldown: cooldown2.checked ? 2 : 3
                            },
                            combatFeatures: [
                                betaRow.selected ? "betaBurstDamage" : "",
                                droneRow.selected ? "droneDamage" : "",
                                dashRow.selected ? "dashDamage" : "",
                                eveRow.selected ? "eveDamage" : "",
                                enemyRow.selected ? "enemyDamage" : "",
                                combatDodge.checked ? "perfectDodge" : "",
                                tachyRow.selected ? "tachyDuration" : "",
                                vulnRow.selected ? "enemyVulnerability" : ""
                            ].filter(function(x){ return x.length > 0 }),
                            combatFeatureLevels: {
                                betaBurstDamage: betaMild.checked ? 1 : 1.333333,
                                droneDamage: droneMild.checked ? 1 : 1.333333,
                                dashDamage: dashMild.checked ? 1 : 2,
                                eveDamage: eveMild.checked ? 1 : 4,
                                enemyDamage: enemyMild.checked ? 0.25 : 1,
                                tachyDuration: tachyMild.checked ? 0.5 : 1,
                                enemyVulnerability: vulnMild.checked ? 0.5 : 1
                            },
                            gaugeTweaks: blasterRow.selected ? ["blasterCellX2"] : [],
                            blasterMultiplier: blaster2.checked ? 2 : 3,
                            outfitSkinSuit: outfit.checked,
                            miniBoss: mb,
                            miniBossRegionDensity: miniBossDensities(),
                            miniBossConfig: {
                                health: mbHealthRow.selected,
                                healthMultiplier: mbHp15.checked ? 1.5 : (mbHp3.checked ? 3 : 4.5),
                                attack: mbAttackRow.selected,
                                attackMultiplier: mbAtk13.checked ? 1.3 : (mbAtk16.checked ? 1.6 : (mbAtk2.checked ? 2 : 3)),
                                scale: mbScaleRow.selected,
                                scaleMultiplier: mbScale12.checked ? 1.2 : (mbScale16.checked ? 1.6 : 2),
                                removeShield: mbShield.checked, rewards: mbRewards.checked,
                                xpRewards: mbXp.checked, persistent: mbPersistent.checked,
                                bossType: mbBossType.checked, executionImmunity: mbExecution.checked
                            },
                            enemyVariety: variety.checked,
                            gameplayExtras: [
                                exAmmo.checked ? "ammoStacks" : "",
                                exConsumables.checked ? "consumableStacks" : "",
                                exShieldRegen.checked ? "shieldRegen" : "",
                                exAttackSpeed.checked ? "attackSpeed" : "",
                                exTachy.checked ? "longerTachy" : "",
                                exDrain.checked ? "hpDrain" : "",
                                exFall.checked ? "noFallDamage" : "",
                                exWater.checked ? "noWaterDeath" : "",
                                exSand.checked ? "noSandDeath" : "",
                                exTachyR.checked ? "tachyReduce" : "",
                                exGear.checked ? "strongerGear" : "",
                                exBetaParry.checked ? "betaParryRecovery" : "",
                                exBurstDodge.checked ? "burstDodgeRecovery" : "",
                                exJust.checked ? "forgivingJust" : "",
                                exAirDodge.checked ? "extraAirDodge" : "",
                                exHarder.checked ? "harderEnemies" : "",
                                exTumbler.checked ? "tumblerHeal" : ""
                            ].filter(function(x){ return x.length > 0 }),
                            harderEnemiesMult: harderValue(),
                            forgivingJustMult: just15.checked ? 1.5 : (just2.checked ? 2 : 3),
                            airDodgeCount: air2.checked ? 2 : 3,
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
