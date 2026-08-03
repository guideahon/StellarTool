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
    property var presetModel: []
    property var installed: ({ paks: [], helper: false, helpers: [] })
    property string conflictNewLabel: ""
    property string conflictCurrentLabel: ""
    property var conflictApplyNew: null

    Component.onCompleted: {
        gamePath = App.detectStellarBlade()
        // Sin juego no se pueden generar baselines vanilla (hardcore, dificultad):
        // se pide la carpeta una vez, en vez de fallar recien al compilar.
        if (gamePath.length === 0)
            gameMissingDialog.open()
        refreshHistory()
        refreshPresets()
        refreshInstalled()
    }
    function refreshGamePath() { gamePath = App.detectStellarBlade() }
    function refreshHistory() {
        try { historyModel = JSON.parse(App.builderHistory() || "[]") }
        catch (e) { historyModel = [] }
    }
    function refreshPresets() {
        try { presetModel = JSON.parse(App.builderPresets() || "[]") }
        catch (e) { presetModel = [] }
    }
    // Resultado de exportar/importar un preset: {ok, name|path, error}.
    function reportPresetIo(resultJson, isImport) {
        var res = {}
        try { res = JSON.parse(resultJson || "{}") } catch (e) { res = {} }
        presetIoDialog.failed = !res.ok
        if (res.ok) {
            presetIoDialog.message = isImport
                ? (I18n.s.builder_preset_imported || "Preset importado: %1").arg(res.name)
                : (I18n.s.builder_preset_exported || "Preset exportado a %1").arg(res.path)
            refreshPresets()
        } else {
            presetIoDialog.message = res.error || (I18n.s.error || "Error")
        }
        presetIoDialog.open()
    }
    function refreshInstalled() {
        try { installed = JSON.parse(App.installedStatus() || "{}") }
        catch (e) { installed = { paks: [], helper: false, helpers: [] } }
    }
    // El juego carga ~mods recursivamente: compilar ahi adentro deja las
    // carpetas intermedias del build como mods fantasma. Mismo chequeo que
    // build_custom.check_out_dir, adelantado para no dejar apretar Compilar.
    function normalizePath(p) {
        return String(p).replace(/^file:\/\/\//, "").replace(/\//g, "\\")
                        .replace(/\\+$/, "").toLowerCase()
    }
    function outDirInsideMods() {
        if (root.gamePath.length === 0 || outField.text.length === 0)
            return false
        var mods = normalizePath(root.gamePath) + "\\sb\\content\\paks\\~mods"
        var out = normalizePath(outField.text)
        return out === mods || out.indexOf(mods + "\\") === 0
    }
    function applyHistoryTemplate(id) {
        try {
            var answers = JSON.parse(App.builderTemplate(id) || "{}")
            applyTemplate(answers)
            builderScroll.contentItem.contentY = 0
        } catch (e) {
            console.warn("Could not load builder template " + id + ": " + e)
        }
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
        var levels = a.combatFeatureLevels || {}
        betaMild.checked = Number(levels.betaBurstDamage || 1.333333) < 1.2
        betaFull.checked = !betaMild.checked
        droneMild.checked = Number(levels.droneDamage || 1.333333) < 1.2
        droneFull.checked = !droneMild.checked
        dashMild.checked = Number(levels.dashDamage || 2) < 1.5
        dashFull.checked = !dashMild.checked
        eveMild.checked = Number(levels.eveDamage || 4) < 2
        eveFull.checked = !eveMild.checked
        enemyMild.checked = Number(levels.enemyDamage || 1) < 0.5
        enemyFull.checked = !enemyMild.checked
        tachyMild.checked = Number(levels.tachyDuration || 1) < 0.75
        tachyFull.checked = !tachyMild.checked
        vulnMild.checked = Number(levels.enemyVulnerability || 1) < 0.75
        vulnFull.checked = !vulnMild.checked
        blaster2.checked = Number(a.blasterMultiplier || 3) === 2
        blaster3.checked = !blaster2.checked
        var cef = a.combatEconomyFeatures
        var legacyEconomy = a.combatEconomy === "full"
        gainRow.selected = cef ? cef.indexOf("slowerGain") >= 0 : legacyEconomy || a.combatEconomy === undefined
        capacityRow.selected = cef ? cef.indexOf("lowerCapacity") >= 0 : legacyEconomy || a.combatEconomy === undefined
        cooldownRow.selected = cef ? cef.indexOf("cooldown") >= 0 : legacyEconomy || a.combatEconomy === undefined
        var economyLevels = a.combatEconomyLevels || {}
        gain25.checked = Number(economyLevels.slowerGain === undefined ? -0.5 : economyLevels.slowerGain) > -0.4
        gain50.checked = !gain25.checked
        var capacity = economyLevels.lowerCapacity || {}
        capacityFirst.checked = Number(capacity.MaxBetaGauge || 400) === 700
        capacityFull.checked = !capacityFirst.checked
        cooldown2.checked = Number(economyLevels.cooldown || 3) === 2
        cooldown3.checked = !cooldown2.checked
        // Proyectos viejos traen solo el bool: el check suelto de QTE convivia
        // con el helper, asi que mapea a "helper" igual que en el builder.
        var om = a.outfitMode
        if (om !== "off" && om !== "helper" && om !== "noHelperAlpha")
            om = a.outfitSkinSuit === false ? "off" : "helper"
        outfitOff.checked = om === "off"
        outfitNoHelper.checked = om === "noHelperAlpha"
        outfitHelper.checked = om === "helper"
        // Proyectos previos a los arreglos: mismo default que el builder.
        outfitShieldRegen.checked = a.outfitVanillaShieldRegen !== false
        var densities = a.miniBossRegionDensity || {}
        function loadRegionRow(row, code) {
            var value = Number(densities[code] || 0); row.selected = value > 0
            if (value > 0) row.density = value
        }
        loadRegionRow(wlaRow, "WLA"); loadRegionRow(atlRow, "ATL")
        loadRegionRow(meRow, "ME"); loadRegionRow(wlbRow, "WLB")
        loadRegionRow(aylRow, "AYL"); loadRegionRow(ded40Row, "DED40")
        loadRegionRow(dedaRow, "DEDA"); loadRegionRow(seRow, "SE")
        if (a.miniBossRegionDensity === undefined
                && a.miniBoss !== undefined && a.miniBoss !== "off" && a.miniBoss !== false)
            setMiniBossPreset(true)
        var mb = a.miniBossConfig || {}
        mbHealthRow.selected = mb.health === undefined ? true : mb.health
        mbAttackRow.selected = mb.attack === undefined ? true : mb.attack
        mbScaleRow.selected = mb.scale === undefined ? true : mb.scale
        mbShield.checked = mb.removeShield === undefined ? true : mb.removeShield
        mbRewards.checked = mb.rewards === undefined ? true : mb.rewards
        mbXp.checked = mb.xpRewards === undefined ? true : mb.xpRewards
        mbPersistent.checked = mb.persistent === undefined ? true : mb.persistent
        mbBossType.checked = mb.bossType === undefined ? true : mb.bossType
        mbExecution.checked = mb.executionImmunity === undefined ? true : mb.executionImmunity
        var hpMult = Number(mb.healthMultiplier || 4.5)
        mbHp15.checked = hpMult === 1.5; mbHp3.checked = hpMult === 3
        mbHp45.checked = !mbHp15.checked && !mbHp3.checked
        var attackMult = Number(mb.attackMultiplier || 1.6)
        mbAtk13.checked = attackMult === 1.3; mbAtk16.checked = attackMult === 1.6
        mbAtk2.checked = attackMult === 2; mbAtk3.checked = attackMult === 3
        var scaleMult = Number(mb.scaleMultiplier || 1.6)
        mbScale12.checked = scaleMult === 1.2; mbScale16.checked = scaleMult === 1.6
        mbScale2.checked = scaleMult === 2
        variety.checked = a.enemyVariety === true
        var ex = a.gameplayExtras || []
        var xv = a.gameplayExtraValues || {}
        advancedQuantities.checked = a.advancedQuantitySelection === true
        function extraValue(group, key, fallback) {
            return xv[group] && xv[group][key] !== undefined
                    ? Number(xv[group][key]) : fallback
        }
        var legacyQol = ex.indexOf("playerQol") >= 0
        exAmmo.checked = legacyQol || ex.indexOf("ammoStacks") >= 0
        exAmmo100x.checked = ex.indexOf("ammo100x") >= 0
        exConsumables.checked = legacyQol || ex.indexOf("consumableStacks") >= 0
        exShieldRegen.checked = legacyQol || ex.indexOf("shieldRegen") >= 0
        exAttributeShieldRegen.checked = ex.indexOf("attributeShieldRegen") >= 0
        exAttackSpeed.checked = legacyQol || ex.indexOf("attackSpeed") >= 0
        exBaseAttributes.checked = ex.indexOf("baseAttributes") >= 0
        exHighGauge.checked = ex.indexOf("highGaugeCapacity") >= 0
        exPassiveHp.checked = ex.indexOf("passiveHpRegen") >= 0
        exFishing.checked = ex.indexOf("fishingPower") >= 0
        exTachy.checked = ex.indexOf("longerTachy") >= 0
        exDrain.checked = ex.indexOf("hpDrain") >= 0
        exFall.checked = ex.indexOf("noFallDamage") >= 0
        exWater.checked = ex.indexOf("noEnvDeath") >= 0 || ex.indexOf("noWaterDeath") >= 0
        exSand.checked = ex.indexOf("noEnvDeath") >= 0 || ex.indexOf("noSandDeath") >= 0
        exTachyR.checked = ex.indexOf("tachyReduce") >= 0
        exGear.checked = ex.indexOf("strongerGear") >= 0
        exBetaParry.checked = ex.indexOf("autoGaugeRecovery") >= 0 || ex.indexOf("betaParryRecovery") >= 0
        exBurstDodge.checked = ex.indexOf("autoGaugeRecovery") >= 0 || ex.indexOf("burstDodgeRecovery") >= 0
        exGaugeOverTime.checked = ex.indexOf("gaugeRecoveryOverTime") >= 0
        exDashCooldown.checked = ex.indexOf("dashCooldown4") >= 0
        exDroneScan.checked = ex.indexOf("droneScanBoost") >= 0
        exGunRotation.checked = ex.indexOf("gunGorgonRotation") >= 0
        exJust.checked = ex.indexOf("forgivingJust") >= 0
        exAirDodge.checked = ex.indexOf("extraAirDodge") >= 0
        var wt = a.worldTweaks || []
        var wv = a.worldTweakValues || {}
        function worldValue(group, key, fallback) {
            return wv[group] && wv[group][key] !== undefined
                    ? Number(wv[group][key]) : fallback
        }
        wShop.checked = wt.indexOf("shopPrices") >= 0
        wDrops.checked = wt.indexOf("dropRates") >= 0
        wSp.checked = wt.indexOf("spExp") >= 0
        wUpgrades.checked = wt.indexOf("upgradeCosts") >= 0
        wFishing.checked = wt.indexOf("fishing") >= 0
        shopPrice.scaledValue = worldValue("shop_prices", "price_percent", 50)
        dropChance.scaledValue = worldValue("drop_rates", "chance_percent", 200)
        dropCount.scaledValue = worldValue("drop_rates", "count_percent", 100)
        spExpPercent.scaledValue = worldValue("sp_exp", "exp_percent", 50)
        upgradeCost.scaledValue = worldValue("upgrade_costs", "cost_percent", 50)
        fishStamina.scaledValue = worldValue("fishing", "stamina_percent", 50)
        fishTime.scaledValue = worldValue("fishing", "fighting_time_percent", 200)
        var hardcore = a.hardcoreEnemyBoost || "off"
        exHarder.checked = hardcore !== "off"
        harderMain.checked = hardcore === "main"
        harderInsane.checked = hardcore === "insane"
        exTumbler.checked = ex.indexOf("tumblerHeal") >= 0
        baseHp.scaledValue = extraValue("base_attributes", "max_hp", 3000)
        baseShield.scaledValue = extraValue("base_attributes", "max_shield", 1000)
        baseReduction.scaledValue = extraValue("base_attributes", "shield_reduction_percent", 20) * 10
        ammoStacksValue.scaledValue = extraValue("ammo_stacks", "stack_size", 999)
        ammoMultiplier.scaledValue = extraValue("ammo_100x", "multiplier", 100)
        consumableStacksValue.scaledValue = extraValue("consumable_stacks", "stack_size", 99)
        var ammoValues = xv.ammo_stacks && xv.ammo_stacks.values || {}
        ammoStack1.scaledValue = Number(ammoValues.StackBullet1 || ammoStacksValue.realValue)
        ammoStack2.scaledValue = Number(ammoValues.StackBullet2 || ammoStacksValue.realValue)
        ammoStack3.scaledValue = Number(ammoValues.StackBullet3 || ammoStacksValue.realValue)
        ammoStack4.scaledValue = Number(ammoValues.StackBullet4 || ammoStacksValue.realValue)
        ammoStack5.scaledValue = Number(ammoValues.StackBullet5 || ammoStacksValue.realValue)
        ammoStack6.scaledValue = Number(ammoValues.StackBullet6 || ammoStacksValue.realValue)
        var capacityValues = xv.ammo_100x && xv.ammo_100x.values || {}
        ammoCapacity1.scaledValue = Number(capacityValues.StackBullet1 || 3000)
        ammoCapacity2.scaledValue = Number(capacityValues.StackBullet2 || 300)
        ammoCapacity3.scaledValue = Number(capacityValues.StackBullet3 || 1600)
        ammoCapacity4.scaledValue = Number(capacityValues.StackBullet4 || 1200)
        ammoCapacity5.scaledValue = Number(capacityValues.StackBullet5 || 6000)
        ammoCapacity6.scaledValue = Number(capacityValues.StackBullet6 || 800)
        var consumableValues = xv.consumable_stacks && xv.consumable_stacks.values || {}
        consumableStack1.scaledValue = Number(consumableValues.StackConsumable1 || consumableStacksValue.realValue)
        consumableStack2.scaledValue = Number(consumableValues.StackConsumable2 || consumableStacksValue.realValue)
        consumableStack3.scaledValue = Number(consumableValues.StackConsumable3 || consumableStacksValue.realValue)
        consumableStack4.scaledValue = Number(consumableValues.StackConsumable4 || consumableStacksValue.realValue)
        consumableStack5.scaledValue = Number(consumableValues.StackConsumable5 || consumableStacksValue.realValue)
        consumableStack6.scaledValue = Number(consumableValues.StackConsumable6 || consumableStacksValue.realValue)
        consumableStack7.scaledValue = Number(consumableValues.StackConsumable7 || consumableStacksValue.realValue)
        shieldNormal.scaledValue = extraValue("shield_regen", "normal", 120)
        shieldCombat.scaledValue = extraValue("shield_regen", "combat", 30)
        attributeShieldNormal.scaledValue = extraValue("attribute_shield_regen", "normal", 160)
        attributeShieldCombat.scaledValue = extraValue("attribute_shield_regen", "combat", 20)
        highGaugeBeta.scaledValue = extraValue("high_gauge_capacity", "beta", 1500)
        highGaugeBurst.scaledValue = extraValue("high_gauge_capacity", "burst", 2000)
        passiveHpValue.scaledValue = extraValue("passive_hp_regen", "per_second", 20) * 10
        fishingValue.scaledValue = extraValue("fishing_power", "power", 50)
        attackSpeedValue.scaledValue = extraValue("attack_speed", "multiplier", 1.3) * 10
        var tumbler = Number(a.tumblerHealPercent || 60)
        tumbler10.checked = tumbler === 10; tumbler20.checked = tumbler === 20
        tumbler30.checked = tumbler === 30; tumbler40.checked = tumbler === 40
        tumbler50.checked = tumbler === 50; tumbler60.checked = tumbler === 60
        tumbler70.checked = tumbler === 70; tumbler80.checked = tumbler === 80
        tumbler90.checked = tumbler === 90; tumbler100.checked = tumbler === 100
        var justMult = Number(a.forgivingJustMult || 1.5)
        just15.checked = justMult === 1.5; just2.checked = justMult === 2; just3.checked = justMult === 3
        var airCount = Number(a.airDodgeCount || 2)
        air2.checked = airCount === 2; air3.checked = airCount === 3
        tomlField.text = a.customPatchesDir || ""
        helperRandom.checked = a.helperMode === "randomAny"
        helperPeriodic.checked = a.helperMode === "randomPeriodic"
        helperLastNoCns.checked = a.helperMode === "lastNoCns"
        helperLast.checked = !helperRandom.checked && !helperPeriodic.checked
                             && !helperLastNoCns.checked
        if (a.helperIntervalSeconds) interval.value = a.helperIntervalSeconds
        var vh = a.vanillaHelperBuild || "off"
        alpha1.checked = vh === "alpha1"; alpha2.checked = vh === "alpha2"
        alpha3.checked = vh === "alpha3"; alpha4.checked = vh === "alpha4"
        alpha5.checked = vh === "alpha5"; alpha6.checked = vh === "alpha6"
        alphaOff.checked = vh === "off"
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
    function hardcoreValue() { return !exHarder.checked ? "off" : (harderInsane.checked ? "insane" : "main") }
    function tumblerValue() {
        if (tumbler10.checked) return 10
        if (tumbler20.checked) return 20
        if (tumbler30.checked) return 30
        if (tumbler40.checked) return 40
        if (tumbler50.checked) return 50
        if (tumbler60.checked) return 60
        if (tumbler70.checked) return 70
        if (tumbler80.checked) return 80
        if (tumbler90.checked) return 90
        return 100
    }
    function optionEnabled(option) {
        return option.propertyName === "selected"
                ? option.control.selected : option.control.checked
    }
    function setOptionEnabled(option, enabled) {
        if (option.propertyName === "selected")
            option.control.selected = enabled
        else
            option.control.checked = enabled
    }
    function showOptionConflict(newLabel, currentLabels, applyNew) {
        conflictNewLabel = newLabel
        conflictCurrentLabel = currentLabels.join(" + ")
        conflictApplyNew = applyNew
        optionConflictDialog.open()
    }
    function requestOption(newOption, conflicts, newLabel) {
        if (!optionEnabled(newOption))
            return
        var active = []
        var labels = []
        for (var i = 0; i < conflicts.length; ++i) {
            if (optionEnabled(conflicts[i])) {
                active.push(conflicts[i])
                labels.push(conflicts[i].label)
            }
        }
        if (active.length === 0)
            return
        setOptionEnabled(newOption, false)
        showOptionConflict(newLabel, labels, function() {
            for (var j = 0; j < active.length; ++j)
                setOptionEnabled(active[j], false)
            setOptionEnabled(newOption, true)
        })
    }
    function requestBaseAttributeEnhancement() {
        var possible = [
            {control: exAmmo, label: exAmmo.text},
            {control: exShieldRegen, label: exShieldRegen.text},
            {control: exBetaParry, label: exBetaParry.text},
            {control: exBurstDodge, label: exBurstDodge.text},
            {control: capacityRow, propertyName: "selected", label: capacityRow.label}
        ]
        var labels = []
        for (var i = 0; i < possible.length; ++i)
            if (optionEnabled(possible[i]))
                labels.push(possible[i].label)
        if (labels.length === 0) {
            setBaseAttributeEnhancement(true)
            return
        }
        showOptionConflict(I18n.s.builder_bae_group || "Base Attribute Enhancement",
                           labels, function() { setBaseAttributeEnhancement(true) })
    }
    function setBaseAttributeEnhancement(on) {
        exBaseAttributes.checked = on
        exAmmo100x.checked = on
        exAttributeShieldRegen.checked = on
        exHighGauge.checked = on
        exPassiveHp.checked = on
        exFishing.checked = on
        exGaugeOverTime.checked = on
        exDashCooldown.checked = on
        exDroneScan.checked = on
        exGunRotation.checked = on
        if (on) {
            exAmmo.checked = false
            exShieldRegen.checked = false
            exBetaParry.checked = false
            exBurstDodge.checked = false
            capacityRow.selected = false
        }
    }
    function outfitModeValue() { return outfitOff.checked ? "off" : (outfitNoHelper.checked ? "noHelperAlpha" : "helper") }
    function helperValue() {
        if (helperPeriodic.checked) return "randomPeriodic"
        if (helperRandom.checked) return "randomAny"
        if (helperLastNoCns.checked) return "lastNoCns"
        return "last"
    }
    function vanillaHelperValue() {
        if (alpha1.checked) return "alpha1"
        if (alpha2.checked) return "alpha2"
        if (alpha3.checked) return "alpha3"
        if (alpha4.checked) return "alpha4"
        if (alpha5.checked) return "alpha5"
        if (alpha6.checked) return "alpha6"
        return "off"
    }

    Connections {
        target: App
        function onBuilderFinished(zipPath) { root.resultZip = zipPath; root.refreshHistory(); root.refreshInstalled() }
        function onUninstalled() { root.refreshInstalled() }
        // El rollback pudo reponer (o sacar) paks y helper: releer que quedo.
        function onBuildCancelled() { root.refreshInstalled() }
        function onGamePathChanged() { root.refreshGamePath(); root.refreshInstalled() }
    }

    FolderDialog {
        id: gameDialog
        title: I18n.s.builder_game_pick || "Elegir la carpeta del juego"
        onAccepted: App.setGamePath(gameDialog.selectedFolder)
    }

    Dialog {
        id: gameMissingDialog
        anchors.centerIn: Overlay.overlay
        modal: true
        title: I18n.s.builder_game_prompt_title || "No se encontró Stellar Blade"
        contentItem: ColumnLayout {
            width: 440
            spacing: 12
            Text {
                Layout.fillWidth: true
                text: (I18n.s.builder_game_prompt_body
                       || "Stellar Tool no encontró tu instalación de Stellar Blade. Algunas opciones (enemigos hardcore, escalado de dificultad) la necesitan para compilar. Elegí la carpeta del juego: la que contiene SB\\Content\\Paks.")
                color: Theme.text
                wrapMode: Text.Wrap
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Button {
                    Layout.fillWidth: true
                    text: I18n.s.builder_game_later || "Ahora no"
                    onClicked: gameMissingDialog.close()
                }
                Button {
                    Layout.fillWidth: true
                    highlighted: true
                    text: I18n.s.builder_game_pick || "Elegir la carpeta del juego"
                    onClicked: { gameMissingDialog.close(); gameDialog.open() }
                }
            }
        }
    }

    FolderDialog {
        id: outDialog
        onAccepted: { outField.text = outDialog.selectedFolder }
    }
    FolderDialog {
        id: tomlDialog
        onAccepted: { tomlField.text = tomlDialog.selectedFolder.toString().replace("file:///", "").replace(/\//g, "\\") }
    }

    Dialog {
        id: optionConflictDialog
        anchors.centerIn: Overlay.overlay
        modal: true
        closePolicy: Popup.NoAutoClose
        title: I18n.s.builder_conflict_title || "Opciones incompatibles"
        contentItem: ColumnLayout {
            width: 440
            spacing: 12
            Text {
                Layout.fillWidth: true
                text: (I18n.s.builder_conflict_body
                       || "%1 no se puede combinar con %2. ¿Con cuál querés quedarte?")
                      .arg(root.conflictNewLabel).arg(root.conflictCurrentLabel)
                color: Theme.text
                wrapMode: Text.Wrap
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Button {
                    Layout.fillWidth: true
                    text: (I18n.s.builder_conflict_keep || "Conservar %1")
                          .arg(root.conflictCurrentLabel)
                    onClicked: optionConflictDialog.close()
                }
                Button {
                    Layout.fillWidth: true
                    highlighted: true
                    text: (I18n.s.builder_conflict_use || "Usar %1")
                          .arg(root.conflictNewLabel)
                    onClicked: {
                        var applyNew = root.conflictApplyNew
                        optionConflictDialog.close()
                        root.conflictApplyNew = null
                        if (applyNew)
                            applyNew()
                    }
                }
            }
        }
        onClosed: root.conflictApplyNew = null
    }

    Dialog {
        id: savePresetDialog
        anchors.centerIn: Overlay.overlay
        modal: true
        title: I18n.s.builder_preset_save || "Guardar preset"
        standardButtons: Dialog.Save | Dialog.Cancel
        contentItem: TextField {
            id: presetNameField
            width: 360
            placeholderText: I18n.s.builder_preset_name || "Nombre del preset"
            onAccepted: savePresetDialog.accept()
        }
        onOpened: {
            presetNameField.text = ""
            presetNameField.forceActiveFocus()
        }
        onAccepted: {
            if (App.saveBuilderPreset(presetNameField.text,
                                      JSON.stringify(buildButton.currentAnswers())))
                root.refreshPresets()
        }
    }

    // Presets como archivo: exportar el elegido / importar uno que te pasaron.
    FileDialog {
        id: presetExportDialog
        property string presetName: ""
        title: I18n.s.builder_preset_export_title || "Exportar preset a archivo"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "stpreset"
        nameFilters: [I18n.s.builder_preset_filter || "Presets de Stellar Tool (*.stpreset)"]
        onAccepted: root.reportPresetIo(App.exportBuilderPreset(presetName, selectedFile), false)
    }
    FileDialog {
        id: presetImportDialog
        title: I18n.s.builder_preset_import_title || "Importar preset desde archivo"
        fileMode: FileDialog.OpenFile
        nameFilters: [I18n.s.builder_preset_filter || "Presets de Stellar Tool (*.stpreset)"]
        onAccepted: root.reportPresetIo(App.importBuilderPreset(selectedFile), true)
    }

    Dialog {
        id: presetIoDialog
        property string message: ""
        property bool failed: false
        anchors.centerIn: Overlay.overlay
        modal: true
        title: I18n.s.builder_presets || "Presets"
        standardButtons: Dialog.Ok
        contentItem: Text {
            text: presetIoDialog.message
            color: presetIoDialog.failed ? Theme.warn : Theme.text
            wrapMode: Text.Wrap; width: 380
        }
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
        id: builderScroll
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

                        FieldLabel { text: I18n.s.builder_presets || "Combat presets (only preselect options)" }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 18
                            Button { text: I18n.s.builder_preset_full || "Apply Full"; onClicked: root.setCombatPreset("full") }
                            Button { text: I18n.s.builder_preset_first || "Apply First Run"; onClicked: root.setCombatPreset("first") }
                            Button { text: I18n.s.builder_preset_vanilla || "Restore vanilla"; onClicked: root.setCombatPreset("vanilla") }
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
                            id: changeRowRoot
                            property alias label: toggle.text
                            property alias selected: toggle.checked
                            default property alias choices: choiceBox.data
                            signal userToggled(bool checked)
                            Layout.fillWidth: true; spacing: 8
                            CheckBox {
                                id: toggle
                                checked: true
                                Layout.preferredWidth: 330
                                onClicked: changeRowRoot.userToggled(checked)
                            }
                            RowLayout { id: choiceBox; enabled: toggle.checked; spacing: 3 }
                        }
                        component NumericEditor: RowLayout {
                            id: numericEditorRoot
                            property string label: ""
                            property string technicalName: ""
                            property int scaledValue: 0
                            property int factor: 1
                            property int minimum: 0
                            property int maximum: 100
                            property int step: 1
                            readonly property real realValue: scaledValue / factor
                            Layout.fillWidth: true
                            Layout.leftMargin: 36
                            spacing: 8
                            Text {
                                text: numericEditorRoot.label
                                color: Theme.textDim
                                Layout.preferredWidth: 145
                                elide: Text.ElideRight
                                ToolTip.delay: 500
                                ToolTip.visible: labelHover.hovered
                                                     && numericEditorRoot.technicalName.length > 0
                                ToolTip.text: numericEditorRoot.technicalName
                                HoverHandler { id: labelHover }
                            }
                            Slider {
                                Layout.fillWidth: true
                                from: numericEditorRoot.minimum
                                to: numericEditorRoot.maximum
                                stepSize: numericEditorRoot.step
                                value: numericEditorRoot.scaledValue
                                onMoved: numericEditorRoot.scaledValue = Math.round(value)
                            }
                            SpinBox {
                                editable: true
                                from: numericEditorRoot.minimum
                                to: numericEditorRoot.maximum
                                stepSize: numericEditorRoot.step
                                value: numericEditorRoot.scaledValue
                                onValueModified: numericEditorRoot.scaledValue = value
                                textFromValue: function(value) {
                                    return numericEditorRoot.factor === 1
                                            ? String(value)
                                            : (value / numericEditorRoot.factor).toFixed(1)
                                }
                                valueFromText: function(text) {
                                    var parsed = Number(text.replace(",", "."))
                                    return isNaN(parsed) ? numericEditorRoot.scaledValue
                                                        : Math.round(parsed * numericEditorRoot.factor)
                                }
                            }
                        }
                        component QuantifiedExtra: ColumnLayout {
                            id: quantifiedExtraRoot
                            property alias text: quantifiedToggle.text
                            property alias checked: quantifiedToggle.checked
                            default property alias editors: quantifiedEditors.data
                            signal clicked()
                            signal vanillaRequested()
                            Layout.fillWidth: true
                            spacing: 4
                            RowLayout {
                                Layout.fillWidth: true
                                CheckBox {
                                    id: quantifiedToggle
                                    Layout.fillWidth: true
                                    onClicked: quantifiedExtraRoot.clicked()
                                }
                                Button {
                                    text: I18n.s.builder_vanilla || "Vanilla"
                                    onClicked: quantifiedExtraRoot.vanillaRequested()
                                }
                            }
                            ColumnLayout {
                                id: quantifiedEditors
                                Layout.fillWidth: true
                                enabled: quantifiedToggle.checked
                                opacity: enabled ? 1 : 0.45
                            }
                        }
                        ChangeRow { id: betaRow; label: I18n.s.builder_damage_beta || "Beta/Burst damage"
                            CheckBox { id: combatBeta; checked: betaRow.selected; visible: false }
                            ButtonGroup { id: betaGroup } RadioButton { id: betaMild; text: "÷2"; ButtonGroup.group: betaGroup }
                            RadioButton { id: betaFull; text: "÷3"; checked: true; ButtonGroup.group: betaGroup } }
                        ChangeRow { id: droneRow; label: I18n.s.builder_damage_drone || "Drone damage"
                            CheckBox { id: combatDrone; checked: droneRow.selected; visible: false }
                            ButtonGroup { id: droneGroup } RadioButton { id: droneMild; text: "÷2"; ButtonGroup.group: droneGroup }
                            RadioButton { id: droneFull; text: "÷3"; checked: true; ButtonGroup.group: droneGroup } }
                        ChangeRow { id: dashRow; label: I18n.s.builder_damage_dash || "Charged dash damage"
                            CheckBox { id: combatDash; checked: dashRow.selected; visible: false }
                            ButtonGroup { id: dashGroup } RadioButton { id: dashMild; text: "x0,75"; ButtonGroup.group: dashGroup }
                            RadioButton { id: dashFull; text: "÷2"; checked: true; ButtonGroup.group: dashGroup } }
                        ChangeRow { id: eveRow; label: I18n.s.builder_damage_eve || "Regular EVE attack damage"
                            CheckBox { id: combatEve; checked: eveRow.selected; visible: false }
                            ButtonGroup { id: eveGroup } RadioButton { id: eveMild; text: "x1,5"; ButtonGroup.group: eveGroup }
                            RadioButton { id: eveFull; text: "x3"; checked: true; ButtonGroup.group: eveGroup } }
                        ChangeRow { id: enemyRow; label: I18n.s.builder_damage_enemy || "Enemy damage"
                            CheckBox { id: combatEnemy; checked: enemyRow.selected; visible: false }
                            ButtonGroup { id: enemyGroup } RadioButton { id: enemyMild; text: "x1,5"; ButtonGroup.group: enemyGroup }
                            RadioButton { id: enemyFull; text: "x3"; checked: true; ButtonGroup.group: enemyGroup } }
                        CheckBox { id: combatDodge; checked: true; text: I18n.s.builder_dodge_unlock || "Perfect dodge without lock-on" }
                        ChangeRow { id: tachyRow; label: I18n.s.builder_tachy_duration || "Tachy duration"
                            CheckBox { id: combatTachy; checked: tachyRow.selected; visible: false }
                            ButtonGroup { id: tachyGroup } RadioButton { id: tachyMild; text: "x0,75"; ButtonGroup.group: tachyGroup }
                            RadioButton { id: tachyFull; text: "÷2"; checked: true; ButtonGroup.group: tachyGroup } }
                        ChangeRow { id: vulnRow; label: I18n.s.builder_enemy_damage_taken || "Damage taken by enemies"
                            CheckBox { id: combatVulnerability; checked: vulnRow.selected; visible: false }
                            ButtonGroup { id: vulnGroup } RadioButton { id: vulnMild; text: "x1,25"; ButtonGroup.group: vulnGroup }
                            RadioButton { id: vulnFull; text: "x1,5"; checked: true; ButtonGroup.group: vulnGroup } }
                        ChangeRow { id: blasterRow; label: I18n.s.builder_blaster_damage || "Blaster Cell damage"
                            CheckBox { id: combatBlaster; checked: blasterRow.selected; visible: false }
                            ButtonGroup { id: blasterGroup } RadioButton { id: blaster2; text: "x2"; ButtonGroup.group: blasterGroup }
                            RadioButton { id: blaster3; text: "x3"; checked: true; ButtonGroup.group: blasterGroup } }

                        FieldLabel {
                            text: I18n.s.builder_combat_economy || "Economía Beta/Burst"
                            font.bold: true
                        }
                        ChangeRow { id: gainRow; label: I18n.s.builder_beta_gain || "Beta gain"
                            CheckBox { id: economyGain; checked: gainRow.selected; visible: false }
                            ButtonGroup { id: gainGroup } RadioButton { id: gain25; text: "-25%"; ButtonGroup.group: gainGroup }
                            RadioButton { id: gain50; text: "-50%"; checked: true; ButtonGroup.group: gainGroup } }
                        ChangeRow { id: capacityRow; label: I18n.s.builder_beta_capacity || "Beta/Burst capacity"
                            onUserToggled: if (checked) root.requestOption(
                                {control: capacityRow, propertyName: "selected"},
                                [{control: exHighGauge, label: exHighGauge.text}],
                                capacityRow.label)
                            CheckBox { id: economyCapacity; checked: capacityRow.selected; visible: false }
                            ButtonGroup { id: capacityGroup } RadioButton { id: capacityFirst; text: "700 / 1200"; ButtonGroup.group: capacityGroup }
                            RadioButton { id: capacityFull; text: "400 / 800"; checked: true; ButtonGroup.group: capacityGroup } }
                        ChangeRow { id: cooldownRow; label: I18n.s.builder_beta_cooldown || "Beta/Burst cooldown"
                            CheckBox { id: economyCooldown; checked: cooldownRow.selected; visible: false }
                            ButtonGroup { id: cooldownGroup } RadioButton { id: cooldown2; text: "2 s"; ButtonGroup.group: cooldownGroup }
                            RadioButton { id: cooldown3; text: "3 s"; checked: true; ButtonGroup.group: cooldownGroup } }

                        // El swap tiene dos caminos y no se pueden pedir juntos:
                        // con helper de UE4SS, o solo table-side (ALPHA).
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 2
                            FieldLabel { text: I18n.s.builder_q_outfit_mode || "Skin Suit al romper escudo" }
                            ButtonGroup { id: outfitGroup }
                            RadioButton { id: outfitOff; ButtonGroup.group: outfitGroup
                                text: I18n.s.builder_q_outfit_off || "No incluir" }
                            RadioButton { id: outfitHelper; checked: true; ButtonGroup.group: outfitGroup
                                text: I18n.s.builder_q_outfit || "Skin Suit al romper escudo (necesita helper)" }
                            RadioButton { id: outfitNoHelper; ButtonGroup.group: outfitGroup
                                text: I18n.s.builder_q_outfit_no_helper
                                      || "Skin Suit al romper escudo (ALPHA sin helper)" }
                            Text {
                                visible: outfitNoHelper.checked
                                text: I18n.s.builder_outfit_qte_alpha_hint
                                      || "Confirmado in-game: restaura el outfit al romper el escudo sin ningún helper. Falta probarlo con outfits especiales de historia."
                                color: Theme.textDim; font.pixelSize: 11
                                Layout.leftMargin: 26
                                wrapMode: Text.Wrap; Layout.fillWidth: true
                            }
                        }

                        // El swap engancha dos filas vanilla de EffectTable y de
                        // paso pisa lo que ya hacian. El FX de campamento se
                        // devuelve siempre; los 4 s sin regen son opcionales.
                        ColumnLayout {
                            Layout.fillWidth: true; Layout.leftMargin: 26; spacing: 2
                            visible: !outfitOff.checked
                            CheckBox { id: outfitShieldRegen; checked: true
                                text: I18n.s.builder_q_shield_regen || "Conservar el bloqueo vanilla de regen de escudo (4 s)" }
                            Text { text: I18n.s.builder_outfit_fix_hint
                                         || "El swap de outfit engancha dos filas vanilla. El FX de descanso en campamento se devuelve siempre; esto devuelve además los 4 s sin regen de escudo tras romperlo."
                                   color: Theme.textDim; font.pixelSize: 11
                                   wrapMode: Text.Wrap; Layout.fillWidth: true }
                        }

                        // El comportamiento del outfit depende del modo de
                        // arriba: va pegado a el, no al final del formulario.
                        // Sin helper no hay nada que configurar.
                        ColumnLayout {
                            Layout.fillWidth: true; Layout.leftMargin: 26; spacing: 6
                            visible: outfitHelper.checked
                            FieldLabel { text: I18n.s.builder_q_helper || "Comportamiento del outfit" }
                            ButtonGroup { id: helperGroup }
                            ColumnLayout {
                                spacing: 2
                                RadioButton { id: helperLast; checked: true; ButtonGroup.group: helperGroup
                                    text: I18n.s.builder_helper_last || "Restaurar último outfit (CNS)" }
                                RadioButton { id: helperRandom; ButtonGroup.group: helperGroup
                                    text: I18n.s.builder_helper_random || "Outfit aleatorio (CNS)" }
                                RadioButton { id: helperPeriodic; ButtonGroup.group: helperGroup
                                    text: I18n.s.builder_helper_periodic || "Aleatorio + periódico (CNS)" }
                                RadioButton { id: helperLastNoCns; ButtonGroup.group: helperGroup
                                    text: I18n.s.builder_helper_last_nocns || "Restaurar último outfit (SIN CNS)" }
                            }
                            Text {
                                visible: helperLastNoCns.checked
                                text: I18n.s.builder_helper_nocns_hint
                                      || "Usa el helper vanilla en vez del de CNS, con la ALPHA elegida más abajo (alpha6 si no elegís ninguna)."
                                color: Theme.textDim; font.pixelSize: 11
                                wrapMode: Text.Wrap; Layout.fillWidth: true
                            }
                            RowLayout {
                                spacing: 10
                                visible: helperPeriodic.checked
                                FieldLabel { text: I18n.s.builder_q_interval || "Intervalo (s)" }
                                SpinBox { id: interval; from: 5; to: 600; value: 30; Layout.preferredWidth: 140 }
                            }
                        }

                        RowLayout {
                            FieldLabel { text: I18n.s.builder_miniboss_regions || "Mini-bosses by region"; font.bold: true; Layout.fillWidth: true }
                            Button { text: I18n.s.builder_miniboss_preset || "Apply NG+ preset"; onClicked: root.setMiniBossPreset(true) }
                            Button { text: I18n.s.builder_clear || "Clear"; onClicked: root.setMiniBossPreset(false) }
                        }
                        Text { text: I18n.s.builder_region_hint || "Enable each region separately and set its density from 1% to 100%."
                               color: Theme.textDim; font.pixelSize: 11 }
                        component RegionRow: RowLayout {
                            property alias selected: regionCheck.checked
                            property alias label: regionCheck.text
                            property alias density: regionPct.value
                            Layout.fillWidth: true
                            CheckBox { id: regionCheck; Layout.preferredWidth: 260 }
                            FieldLabel { text: I18n.s.builder_density || "Density" }
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
                            FieldLabel { text: I18n.s.builder_miniboss_traits || "Mini-boss attributes"; font.bold: true }
                            ChangeRow { id: mbHealthRow; label: I18n.s.builder_mb_health || "Increase health"
                                CheckBox { id: mbHealth; checked:mbHealthRow.selected; visible:false }
                                ButtonGroup { id: mbHpGroup } RadioButton { id: mbHp15; text:"x1,5"; ButtonGroup.group:mbHpGroup }
                                RadioButton { id: mbHp3; text:"x3"; ButtonGroup.group:mbHpGroup }
                                RadioButton { id: mbHp45; text:"x4,5"; checked:true; ButtonGroup.group:mbHpGroup } }
                            ChangeRow { id: mbAttackRow; label: I18n.s.builder_mb_attack || "Increase attack"
                                CheckBox { id: mbAttack; checked:mbAttackRow.selected; visible:false }
                                ButtonGroup { id: mbAtkGroup } RadioButton { id: mbAtk13; text:"x1,3"; ButtonGroup.group:mbAtkGroup }
                                RadioButton { id: mbAtk16; text:"x1,6"; checked:true; ButtonGroup.group:mbAtkGroup }
                                RadioButton { id: mbAtk2; text:"x2"; ButtonGroup.group:mbAtkGroup }
                                RadioButton { id: mbAtk3; text:"x3"; ButtonGroup.group:mbAtkGroup } }
                            ChangeRow { id: mbScaleRow; label: I18n.s.builder_mb_scale || "Increase size"
                                CheckBox { id: mbScale; checked:mbScaleRow.selected; visible:false }
                                ButtonGroup { id: mbScaleGroup } RadioButton { id: mbScale12; text:"x1,2"; ButtonGroup.group:mbScaleGroup }
                                RadioButton { id: mbScale16; text:"x1,6"; checked:true; ButtonGroup.group:mbScaleGroup }
                                RadioButton { id: mbScale2; text:"x2"; ButtonGroup.group:mbScaleGroup } }
                            CheckBox { id: mbShield; checked:true; text: I18n.s.builder_mb_remove_shield || "Remove shield" }
                            CheckBox { id: mbRewards; checked:true; text: I18n.s.builder_mb_rewards || "Add NG+ rewards and drops" }
                            CheckBox { id: mbXp; checked:true; text: I18n.s.builder_mb_xp || "Increase XP" }
                            CheckBox { id: mbPersistent; checked:true; text: I18n.s.builder_mb_persistent || "Persist death state" }
                            CheckBox { id: mbBossType; checked:true; text: I18n.s.builder_mb_boss_type || "Treat as Boss type" }
                            CheckBox { id: mbExecution; checked:true; text: I18n.s.builder_mb_execution || "Instant-execution immunity" }
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
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                FieldLabel {
                                    text: I18n.s.builder_bae_group || "Base Attribute Enhancement"
                                    Layout.fillWidth: true
                                }
                                Button {
                                    text: I18n.s.builder_bae_apply || "Apply all"
                                    onClicked: root.requestBaseAttributeEnhancement()
                                }
                                Button {
                                    text: I18n.s.builder_bae_clear || "Clear"
                                    onClicked: root.setBaseAttributeEnhancement(false)
                                }
                            }
                            CheckBox {
                                id: advancedQuantities
                                Layout.fillWidth: true
                                text: I18n.s.builder_advanced_selection || "Selección avanzada"
                            }
                            Text {
                                visible: advancedQuantities.checked
                                Layout.fillWidth: true
                                Layout.leftMargin: 36
                                text: I18n.s.builder_advanced_selection_hint
                                      || "Personalizá por separado cada propiedad interna agrupada."
                                color: Theme.textDim
                                font.pixelSize: 11
                                wrapMode: Text.Wrap
                            }
                            QuantifiedExtra {
                                id: exBaseAttributes
                                text: I18n.s.builder_ex_base_attributes || "Base HP 3000, shield 1000 and shield reduction 20%"
                                onVanillaRequested: {
                                    checked = false; baseHp.scaledValue = 2000
                                    baseShield.scaledValue = 500; baseReduction.scaledValue = 175
                                }
                                NumericEditor { id: baseHp; label: "HP"; minimum: 100; maximum: 10000; step: 100; scaledValue: 3000 }
                                NumericEditor { id: baseShield; label: I18n.s.builder_value_shield || "Escudo"; minimum: 0; maximum: 5000; step: 50; scaledValue: 1000 }
                                NumericEditor { id: baseReduction; label: I18n.s.builder_value_reduction || "Reducción (%)"; factor: 10; minimum: 0; maximum: 1000; step: 5; scaledValue: 200 }
                            }
                            QuantifiedExtra {
                                id: exAmmo
                                text: I18n.s.builder_ex_ammo || "Ammo stack size: 999"
                                onClicked: root.requestOption(
                                    {control: exAmmo},
                                    [{control: exAmmo100x, label: exAmmo100x.text}],
                                    exAmmo.text)
                                onVanillaRequested: {
                                    checked = false; ammoStacksValue.scaledValue = 30
                                    ammoStack1.scaledValue = 30; ammoStack2.scaledValue = 3
                                    ammoStack3.scaledValue = 16; ammoStack4.scaledValue = 12
                                    ammoStack5.scaledValue = 60; ammoStack6.scaledValue = 8
                                }
                                NumericEditor { id: ammoStacksValue; visible: !advancedQuantities.checked; label: I18n.s.builder_value_quantity || "Cantidad"; minimum: 1; maximum: 9999; scaledValue: 999 }
                                NumericEditor { id: ammoStack1; visible: advancedQuantities.checked; label: I18n.s.builder_item_slug || "Slug"; technicalName: "StackBullet1"; minimum: 1; maximum: 9999; scaledValue: 999 }
                                NumericEditor { id: ammoStack2; visible: advancedQuantities.checked; label: I18n.s.builder_item_blaster_cell || "Blaster Cell"; technicalName: "StackBullet2"; minimum: 1; maximum: 9999; scaledValue: 999 }
                                NumericEditor { id: ammoStack3; visible: advancedQuantities.checked; label: I18n.s.builder_item_stinger || "Stinger"; technicalName: "StackBullet3"; minimum: 1; maximum: 9999; scaledValue: 999 }
                                NumericEditor { id: ammoStack4; visible: advancedQuantities.checked; label: I18n.s.builder_item_shotgun_shell || "Shotgun Shell"; technicalName: "StackBullet4"; minimum: 1; maximum: 9999; scaledValue: 999 }
                                NumericEditor { id: ammoStack5; visible: advancedQuantities.checked; label: I18n.s.builder_item_nikke_ammo || "NIKKE ammunition"; technicalName: "StackBullet5"; minimum: 1; maximum: 9999; scaledValue: 999 }
                                NumericEditor { id: ammoStack6; visible: advancedQuantities.checked; label: I18n.s.builder_item_explosive_shell || "Explosive Shell"; technicalName: "StackBullet6"; minimum: 1; maximum: 9999; scaledValue: 999 }
                            }
                            QuantifiedExtra {
                                id: exAmmo100x
                                text: I18n.s.builder_ex_ammo_100x || "Ammo capacity x100"
                                onClicked: root.requestOption(
                                    {control: exAmmo100x},
                                    [{control: exAmmo, label: exAmmo.text}],
                                    exAmmo100x.text)
                                onVanillaRequested: {
                                    checked = false; ammoMultiplier.scaledValue = 1
                                    ammoCapacity1.scaledValue = 30; ammoCapacity2.scaledValue = 3
                                    ammoCapacity3.scaledValue = 16; ammoCapacity4.scaledValue = 12
                                    ammoCapacity5.scaledValue = 60; ammoCapacity6.scaledValue = 8
                                }
                                NumericEditor { id: ammoMultiplier; visible: !advancedQuantities.checked; label: I18n.s.builder_value_multiplier || "Multiplicador"; minimum: 1; maximum: 500; scaledValue: 100 }
                                NumericEditor { id: ammoCapacity1; visible: advancedQuantities.checked; label: I18n.s.builder_item_slug || "Slug"; technicalName: "StackBullet1"; minimum: 1; maximum: 30000; scaledValue: 3000 }
                                NumericEditor { id: ammoCapacity2; visible: advancedQuantities.checked; label: I18n.s.builder_item_blaster_cell || "Blaster Cell"; technicalName: "StackBullet2"; minimum: 1; maximum: 30000; scaledValue: 300 }
                                NumericEditor { id: ammoCapacity3; visible: advancedQuantities.checked; label: I18n.s.builder_item_stinger || "Stinger"; technicalName: "StackBullet3"; minimum: 1; maximum: 30000; scaledValue: 1600 }
                                NumericEditor { id: ammoCapacity4; visible: advancedQuantities.checked; label: I18n.s.builder_item_shotgun_shell || "Shotgun Shell"; technicalName: "StackBullet4"; minimum: 1; maximum: 30000; scaledValue: 1200 }
                                NumericEditor { id: ammoCapacity5; visible: advancedQuantities.checked; label: I18n.s.builder_item_nikke_ammo || "NIKKE ammunition"; technicalName: "StackBullet5"; minimum: 1; maximum: 30000; scaledValue: 6000 }
                                NumericEditor { id: ammoCapacity6; visible: advancedQuantities.checked; label: I18n.s.builder_item_explosive_shell || "Explosive Shell"; technicalName: "StackBullet6"; minimum: 1; maximum: 30000; scaledValue: 800 }
                            }
                            QuantifiedExtra {
                                id: exConsumables
                                text: I18n.s.builder_ex_consumables || "Consumable stack size: 99"
                                onVanillaRequested: {
                                    checked = false; consumableStacksValue.scaledValue = 10
                                    consumableStack1.scaledValue = 10; consumableStack2.scaledValue = 10
                                    consumableStack3.scaledValue = 10; consumableStack4.scaledValue = 10
                                    consumableStack5.scaledValue = 10; consumableStack6.scaledValue = 10
                                    consumableStack7.scaledValue = 10
                                }
                                NumericEditor { id: consumableStacksValue; visible: !advancedQuantities.checked; label: I18n.s.builder_value_quantity || "Cantidad"; minimum: 1; maximum: 999; scaledValue: 99 }
                                NumericEditor { id: consumableStack1; visible: advancedQuantities.checked; label: I18n.s.builder_item_shock_grenade || "Shock Grenade"; technicalName: "StackConsumable1"; minimum: 1; maximum: 999; scaledValue: 99 }
                                NumericEditor { id: consumableStack2; visible: advancedQuantities.checked; label: I18n.s.builder_item_lingering_potion || "Lingering Potion"; technicalName: "StackConsumable2"; minimum: 1; maximum: 999; scaledValue: 99 }
                                NumericEditor { id: consumableStack3; visible: advancedQuantities.checked; label: I18n.s.builder_item_smart_mine || "Smart Mine"; technicalName: "StackConsumable3"; minimum: 1; maximum: 999; scaledValue: 99 }
                                NumericEditor { id: consumableStack4; visible: advancedQuantities.checked; label: I18n.s.builder_item_concentrated_potion || "Highly Concentrated Potion"; technicalName: "StackConsumable4"; minimum: 1; maximum: 999; scaledValue: 99 }
                                NumericEditor { id: consumableStack5; visible: advancedQuantities.checked; label: I18n.s.builder_item_pulse_grenade || "Pulse Grenade"; technicalName: "StackConsumable5"; minimum: 1; maximum: 999; scaledValue: 99 }
                                NumericEditor { id: consumableStack6; visible: advancedQuantities.checked; label: I18n.s.builder_item_wb_pump || "WB Pump"; technicalName: "StackConsumable6"; minimum: 1; maximum: 999; scaledValue: 99 }
                                NumericEditor { id: consumableStack7; visible: advancedQuantities.checked; label: I18n.s.builder_item_sonic_grenade || "Sonic Grenade"; technicalName: "StackConsumable7"; minimum: 1; maximum: 999; scaledValue: 99 }
                            }
                            QuantifiedExtra {
                                id: exShieldRegen
                                text: I18n.s.builder_ex_shield_regen || "Increased shield regeneration"
                                onClicked: root.requestOption(
                                    {control: exShieldRegen},
                                    [{control: exAttributeShieldRegen,
                                      label: exAttributeShieldRegen.text}],
                                    exShieldRegen.text)
                                onVanillaRequested: {
                                    checked = false; shieldNormal.scaledValue = 80
                                    shieldCombat.scaledValue = 10
                                }
                                NumericEditor { id: shieldNormal; label: I18n.s.builder_value_normal || "Normal / s"; minimum: 0; maximum: 500; scaledValue: 120 }
                                NumericEditor { id: shieldCombat; label: I18n.s.builder_value_combat || "Combate / s"; minimum: 0; maximum: 200; scaledValue: 30 }
                            }
                            QuantifiedExtra {
                                id: exAttributeShieldRegen
                                text: I18n.s.builder_ex_attribute_shield_regen || "Shield regeneration: 160/s, 20/s in combat"
                                onClicked: root.requestOption(
                                    {control: exAttributeShieldRegen},
                                    [{control: exShieldRegen, label: exShieldRegen.text}],
                                    exAttributeShieldRegen.text)
                                onVanillaRequested: {
                                    checked = false; attributeShieldNormal.scaledValue = 80
                                    attributeShieldCombat.scaledValue = 10
                                }
                                NumericEditor { id: attributeShieldNormal; label: I18n.s.builder_value_normal || "Normal / s"; minimum: 0; maximum: 500; scaledValue: 160 }
                                NumericEditor { id: attributeShieldCombat; label: I18n.s.builder_value_combat || "Combate / s"; minimum: 0; maximum: 200; scaledValue: 20 }
                            }
                            QuantifiedExtra {
                                id: exHighGauge
                                text: I18n.s.builder_ex_high_gauge || "Beta 1500 / Burst 2000 capacity"
                                onClicked: root.requestOption(
                                    {control: exHighGauge},
                                    [{control: capacityRow, propertyName: "selected",
                                      label: capacityRow.label}],
                                    exHighGauge.text)
                                onVanillaRequested: {
                                    checked = false; highGaugeBeta.scaledValue = 1000
                                    highGaugeBurst.scaledValue = 1600
                                }
                                NumericEditor { id: highGaugeBeta; label: "Beta"; minimum: 100; maximum: 5000; step: 50; scaledValue: 1500 }
                                NumericEditor { id: highGaugeBurst; label: "Burst"; minimum: 100; maximum: 5000; step: 50; scaledValue: 2000 }
                            }
                            QuantifiedExtra {
                                id: exPassiveHp
                                text: I18n.s.builder_ex_passive_hp || "Passive HP regeneration: 20/s"
                                onVanillaRequested: { checked = false; passiveHpValue.scaledValue = 0 }
                                NumericEditor { id: passiveHpValue; label: I18n.s.builder_value_per_second || "Por segundo"; factor: 10; minimum: 0; maximum: 1000; step: 5; scaledValue: 200 }
                            }
                            QuantifiedExtra {
                                id: exFishing
                                text: I18n.s.builder_ex_fishing || "Fishing power: 50"
                                onVanillaRequested: { checked = false; fishingValue.scaledValue = 15 }
                                NumericEditor { id: fishingValue; label: I18n.s.builder_value_power || "Potencia"; minimum: 0; maximum: 500; scaledValue: 50 }
                            }
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.topMargin: 4
                                Layout.bottomMargin: 4
                                height: 1
                                color: Theme.border
                            }
                            QuantifiedExtra {
                                id: exAttackSpeed
                                text: I18n.s.builder_ex_attack_speed || "Attack speed x1.3"
                                onVanillaRequested: { checked = false; attackSpeedValue.scaledValue = 10 }
                                NumericEditor { id: attackSpeedValue; label: I18n.s.builder_value_multiplier || "Multiplicador"; factor: 10; minimum: 5; maximum: 30; scaledValue: 13 }
                            }
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
                            CheckBox { id: exWater; text: I18n.s.builder_ex_water || "No deep-water death" }
                            CheckBox { id: exSand; text: I18n.s.builder_ex_sand || "No sand death" }
                            RowLayout { spacing: 10
                                CheckBox { id: exTachyR }
                                Text { text: I18n.s.builder_ex_tachyr || "Menos consumo de Tachy"
                                       color: Theme.text; wrapMode: Text.Wrap; Layout.fillWidth: true } }
                            RowLayout { spacing: 10
                                CheckBox { id: exGear }
                                Text { text: I18n.s.builder_ex_gear || "Engranajes más fuertes (x2)"
                                       color: Theme.text; wrapMode: Text.Wrap; Layout.fillWidth: true } }
                            CheckBox {
                                id: exBetaParry
                                text: I18n.s.builder_ex_beta_parry || "Beta on perfect parry (no skill tree)"
                                onClicked: root.requestOption(
                                    {control: exBetaParry},
                                    [{control: exGaugeOverTime, label: exGaugeOverTime.text}],
                                    exBetaParry.text)
                            }
                            CheckBox {
                                id: exBurstDodge
                                text: I18n.s.builder_ex_burst_dodge || "Burst on perfect dodge (no skill tree)"
                                onClicked: root.requestOption(
                                    {control: exBurstDodge},
                                    [{control: exGaugeOverTime, label: exGaugeOverTime.text}],
                                    exBurstDodge.text)
                            }
                            CheckBox {
                                id: exGaugeOverTime
                                text: I18n.s.builder_ex_gauge_over_time || "Sustained Beta/Burst recovery after perfect actions"
                                onClicked: root.requestOption(
                                    {control: exGaugeOverTime},
                                    [{control: exBetaParry, label: exBetaParry.text},
                                     {control: exBurstDodge, label: exBurstDodge.text}],
                                    exGaugeOverTime.text)
                            }
                            CheckBox { id: exDashCooldown; text: I18n.s.builder_ex_dash_cooldown || "Dash cooldown: 4 s" }
                            CheckBox { id: exDroneScan; text: I18n.s.builder_ex_drone_scan || "Drone scan: 5 s cooldown, 10 s marking" }
                            CheckBox { id: exGunRotation; text: I18n.s.builder_ex_gun_rotation || "Allow rotation during GunGorgon stance" }
                            RowLayout { spacing: 10
                                CheckBox { id: exTumbler }
                                Text { text: I18n.s.builder_ex_tumbler || "Tumbler base healing"
                                       color: Theme.text; wrapMode: Text.Wrap; Layout.fillWidth: true } }
                            GridLayout {
                                Layout.leftMargin: 36
                                Layout.fillWidth: true
                                columns: 5
                                columnSpacing: 4
                                rowSpacing: 2
                                enabled: exTumbler.checked
                                ButtonGroup { id: tumblerGroup }
                                RadioButton { id: tumbler10; text:"10%"; ButtonGroup.group:tumblerGroup }
                                RadioButton { id: tumbler20; text:"20%"; ButtonGroup.group:tumblerGroup }
                                RadioButton { id: tumbler30; text:"30%"; ButtonGroup.group:tumblerGroup }
                                RadioButton { id: tumbler40; text:"40%"; ButtonGroup.group:tumblerGroup }
                                RadioButton { id: tumbler50; text:"50%"; ButtonGroup.group:tumblerGroup }
                                RadioButton { id: tumbler60; text:"60%"; checked:true; ButtonGroup.group:tumblerGroup }
                                RadioButton { id: tumbler70; text:"70%"; ButtonGroup.group:tumblerGroup }
                                RadioButton { id: tumbler80; text:"80%"; ButtonGroup.group:tumblerGroup }
                                RadioButton { id: tumbler90; text:"90%"; ButtonGroup.group:tumblerGroup }
                                RadioButton { id: tumbler100; text:"100%"; ButtonGroup.group:tumblerGroup }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true; spacing: 2
                                CheckBox {
                                    id: exJust
                                    Layout.fillWidth: true
                                    text: I18n.s.builder_ex_just || "Ventana de parry/dodge perfecto más amplia (x1.5)"
                                }
                                RowLayout {
                                    Layout.leftMargin: 36
                                    enabled: exJust.checked
                                    ButtonGroup { id: justGroup }
                                    RadioButton { id: just15; checked:true; text:"x1,5"; ButtonGroup.group:justGroup }
                                    RadioButton { id: just2; text:"x2"; ButtonGroup.group:justGroup }
                                    RadioButton { id: just3; text:"x3"; ButtonGroup.group:justGroup }
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true; spacing: 2
                                CheckBox {
                                    id: exAirDodge
                                    Layout.fillWidth: true
                                    text: I18n.s.builder_ex_airdodge || "Doble esquive aéreo"
                                }
                                RowLayout {
                                    Layout.leftMargin: 36
                                    enabled: exAirDodge.checked
                                    ButtonGroup { id: airGroup }
                                    RadioButton { id: air2; checked:true; text:"x2"; ButtonGroup.group:airGroup }
                                    RadioButton { id: air3; text:"x3"; ButtonGroup.group:airGroup }
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true; spacing: 2
                                CheckBox {
                                    id: exHarder
                                    Layout.fillWidth: true
                                    text: I18n.s.builder_ex_harder || "Bosses más duros (sólo Hardcore)"
                                }
                                RowLayout {
                                    Layout.leftMargin: 36
                                    visible: exHarder.checked
                                    ButtonGroup { id: harderGroup }
                                    RadioButton { id: harderMain; checked: true; ButtonGroup.group: harderGroup
                                        text: I18n.s.builder_hardcore_main || "Main" }
                                    RadioButton { id: harderInsane; ButtonGroup.group: harderGroup
                                        text: I18n.s.builder_hardcore_insane || "Insane" }
                                }
                            }
                            Text {
                                visible: exHarder.checked
                                text: I18n.s.builder_hardcore_note ||
                                      "Main: bosses x2 HP/x1,25 escudo. Insane: x3 HP/x2 escudo. Daño boss x1,25. No modifica enemigos normales; Maelstrom queda excluido."
                                color: Theme.textDim; wrapMode: Text.Wrap; Layout.fillWidth: true
                            }

                            // ---- Mundo y progresion: tablas que el pak de
                            // combate/outfit no toca (tienda, drops, SP, mejoras, pesca).
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.topMargin: 8
                                height: 1
                                color: Theme.border
                            }
                            RowLayout {
                                spacing: 6
                                FieldLabel {
                                    text: I18n.s.builder_world_title || "Mundo y progresión"
                                    font.bold: true
                                }
                                Rectangle { radius: 4; color: Theme.warn; implicitWidth: worldBeta.width+12; implicitHeight: 18
                                    Text { id: worldBeta; anchors.centerIn: parent; text: "BETA"; color: Theme.warnText; font.pixelSize: 10; font.bold: true } }
                            }
                            Text {
                                Layout.fillWidth: true
                                text: I18n.s.builder_world_hint
                                      || "Cada valor es un % del valor vanilla (100 = vanilla). Salen en un pak aparte, así que se pueden combinar con cualquier mod de combate."
                                color: Theme.textDim; font.pixelSize: 11; wrapMode: Text.Wrap
                            }
                            QuantifiedExtra {
                                id: wShop
                                text: I18n.s.builder_world_shop || "Precios de tienda"
                                onVanillaRequested: { checked = false; shopPrice.scaledValue = 100 }
                                NumericEditor { id: shopPrice; label: I18n.s.builder_value_percent || "% de vanilla"; technicalName: "MoneyItemCount1-4"; minimum: 0; maximum: 400; step: 5; scaledValue: 50 }
                            }
                            QuantifiedExtra {
                                id: wDrops
                                text: I18n.s.builder_world_drops || "Drops de enemigos y cofres"
                                onVanillaRequested: {
                                    checked = false; dropChance.scaledValue = 100
                                    dropCount.scaledValue = 100
                                }
                                NumericEditor { id: dropChance; label: I18n.s.builder_value_chance || "Probabilidad %"; technicalName: "DropRate"; minimum: 0; maximum: 1000; step: 10; scaledValue: 200 }
                                NumericEditor { id: dropCount; label: I18n.s.builder_value_amount || "Cantidad %"; technicalName: "ItemMinCount / ItemMaxCount"; minimum: 100; maximum: 1000; step: 10; scaledValue: 100 }
                            }
                            QuantifiedExtra {
                                id: wSp
                                text: I18n.s.builder_world_sp || "EXP de SP requerida por nivel"
                                onVanillaRequested: { checked = false; spExpPercent.scaledValue = 100 }
                                NumericEditor { id: spExpPercent; label: I18n.s.builder_value_percent || "% de vanilla"; technicalName: "RequiredSPExp"; minimum: 5; maximum: 200; step: 5; scaledValue: 50 }
                            }
                            QuantifiedExtra {
                                id: wUpgrades
                                text: I18n.s.builder_world_upgrades || "Materiales para mejorar a EVE"
                                onVanillaRequested: { checked = false; upgradeCost.scaledValue = 100 }
                                NumericEditor { id: upgradeCost; label: I18n.s.builder_value_percent || "% de vanilla"; technicalName: "RequiredItemAmount1 / 2"; minimum: 0; maximum: 200; step: 5; scaledValue: 50 }
                            }
                            QuantifiedExtra {
                                id: wFishing
                                text: I18n.s.builder_world_fishing || "Pesca más fácil"
                                onVanillaRequested: {
                                    checked = false; fishStamina.scaledValue = 100
                                    fishTime.scaledValue = 100
                                }
                                NumericEditor { id: fishStamina; label: I18n.s.builder_value_stamina || "Stamina del pez %"; technicalName: "Stamina"; minimum: 5; maximum: 200; step: 5; scaledValue: 50 }
                                NumericEditor { id: fishTime; label: I18n.s.builder_value_fight_time || "Tiempo de pelea %"; technicalName: "FightingTime"; minimum: 50; maximum: 500; step: 10; scaledValue: 200 }
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

                        // ---- Helper vanilla sin CNS: builds de prueba ALPHA ----
                        // Van despues de todo lo BETA a proposito: son mas
                        // crudos todavia (no confirmados in-game).
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 6; Layout.topMargin: 8

                            RowLayout {
                                spacing: 6
                                FieldLabel { text: I18n.s.builder_alpha_title || "Restaurar outfit sin CNS" }
                                Rectangle {
                                    radius: 4; color: Theme.danger
                                    implicitWidth: alphaBadge.width + 12; implicitHeight: 18
                                    // En claro el rojo es oscuro: el texto va blanco.
                                    Text { id: alphaBadge; anchors.centerIn: parent; text: "ALPHA"
                                           color: Theme.dark ? Theme.warnText : "#ffffff"
                                           font.pixelSize: 10; font.bold: true }
                                }
                            }
                            Text {
                                Layout.fillWidth: true; wrapMode: Text.Wrap
                                text: "⚠ " + (I18n.s.builder_alpha_warn
                                      || "ALPHA, mas crudo que BETA: sin confirmar in-game. Instala UNA por vez.")
                                color: Theme.danger; font.pixelSize: 12
                            }
                            Text {
                                Layout.fillWidth: true; wrapMode: Text.Wrap
                                text: I18n.s.builder_alpha_note
                                      || "Para quien NO usa CNS (outfits replacer). Necesita UE4SS. Log: %USERPROFILE%\\StellarSoulsVanillaRestore.log"
                                color: Theme.textDim; font.pixelSize: 11
                            }
                            ButtonGroup { id: alphaGroup }
                            ColumnLayout {
                                spacing: 2
                                RadioButton { id: alphaOff; checked: true; ButtonGroup.group: alphaGroup
                                    text: I18n.s.builder_alpha_off || "No incluir" }
                                RadioButton { id: alpha1; ButtonGroup.group: alphaGroup
                                    text: I18n.s.builder_alpha1 || "ALPHA1 probe — solo lee y diagnostica (empezar por esta)" }
                                RadioButton { id: alpha2; ButtonGroup.group: alphaGroup
                                    text: I18n.s.builder_alpha2 || "ALPHA2 meshrepaint — ApplyMeshInfo(); sin cheat manager ni id de traje" }
                                RadioButton { id: alpha3; ButtonGroup.group: alphaGroup
                                    text: I18n.s.builder_alpha3 || "ALPHA3 cheatequip — SBPlayerEquipItem; necesita CheatManagerEnablerMod" }
                                RadioButton { id: alpha4; ButtonGroup.group: alphaGroup
                                    text: I18n.s.builder_alpha4 || "ALPHA4 cheatequip-construct — igual, pero se crea su propio cheat manager" }
                                RadioButton { id: alpha5; ButtonGroup.group: alphaGroup
                                    text: I18n.s.builder_alpha5 || "ALPHA5 equiptoggle — desequipa y reequipa (el arreglo manual, automatico)" }
                                RadioButton { id: alpha6; ButtonGroup.group: alphaGroup
                                    text: I18n.s.builder_alpha6 || "ALPHA6 chain — prueba 2 → 3/4 → 5 y para en la que repinta" }
                            }
                        }

                        FieldLabel { text: I18n.s.builder_out || "Carpeta de salida del ZIP" }
                        RowLayout {
                            spacing: 8; Layout.fillWidth: true
                            TextField {
                                id: outField
                                Layout.fillWidth: true
                                color: Theme.text
                                text: App.defaultBuildOutDir()
                                background: Rectangle { radius: Theme.radius; color: Theme.panelAlt; border.color: Theme.border }
                            }
                            Button { text: "📁"; onClicked: outDialog.open() }
                        }
                        Text {
                            visible: root.outDirInsideMods()
                            text: "⚠ " + (I18n.s.builder_out_in_mods
                                  || "Esa carpeta esta dentro de ~mods. El juego la carga recursivamente, asi que los archivos intermedios del build quedarian como mods fantasma que pisan al mod instalado. Elegi una carpeta fuera del juego; la tool instala el pak en ~mods por su cuenta.")
                            color: Theme.warn
                            wrapMode: Text.Wrap; Layout.fillWidth: true
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
                        Button {
                            text: I18n.s.builder_game_pick || "Elegir la carpeta del juego"
                            highlighted: root.gamePath.length === 0
                            onClicked: gameDialog.open()
                        }
                        RowLayout {
                            spacing: 10; enabled: root.gamePath.length > 0
                            CheckBox { id: instPaks }
                            Text { text: I18n.s.builder_install_paks || "Instalar el mod directamente en ~mods"
                                   color: enabled ? Theme.text : Theme.textDim; wrapMode: Text.Wrap; Layout.fillWidth: true }
                        }
                        RowLayout {
                            // Tambien vale para la ALPHA vanilla: es otro mod de UE4SS.
                            // Siempre habilitado con el juego detectado: una build
                            // sin helper igual tiene estado de helper que sincronizar.
                            spacing: 10
                            enabled: root.gamePath.length > 0
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

                Card {
                    Layout.fillWidth: true
                    implicitHeight: presetColumn.implicitHeight + 28
                    ColumnLayout {
                        id: presetColumn
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 8
                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                Layout.fillWidth: true
                                text: I18n.s.builder_presets || "Presets"
                                color: Theme.text
                                font.bold: true
                                font.pixelSize: 16
                            }
                            Button {
                                text: I18n.s.builder_preset_import || "Importar preset"
                                onClicked: presetImportDialog.open()
                            }
                            Button {
                                text: I18n.s.builder_preset_save || "Guardar preset"
                                onClicked: savePresetDialog.open()
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: I18n.s.builder_preset_share_hint
                                  || "Los presets se exportan como archivo .stpreset: podés pasarle tu configuración a otra persona y que la compile con sus propios valores."
                            color: Theme.textDim; font.pixelSize: 11; wrapMode: Text.Wrap
                        }
                        Text {
                            visible: root.presetModel.length === 0
                            text: I18n.s.builder_preset_empty || "Todavía no guardaste presets."
                            color: Theme.textDim
                        }
                        Repeater {
                            model: root.presetModel
                            delegate: RowLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                Text {
                                    Layout.fillWidth: true
                                    text: modelData.name
                                    color: Theme.text
                                    elide: Text.ElideRight
                                }
                                Button {
                                    text: I18n.s.builder_preset_load || "Cargar"
                                    onClicked: root.applyTemplate(modelData.answers)
                                }
                                Button {
                                    text: I18n.s.builder_preset_export || "Exportar"
                                    onClicked: {
                                        presetExportDialog.presetName = modelData.name
                                        presetExportDialog.open()
                                    }
                                }
                                Button {
                                    text: I18n.s.builder_preset_delete || "Eliminar"
                                    onClicked: {
                                        App.deleteBuilderPreset(modelData.name)
                                        root.refreshPresets()
                                    }
                                }
                            }
                        }
                    }
                }

                Button {
                    id: buildButton
                    Layout.fillWidth: true
                    text: I18n.s.builder_build || "Compilar mi mod"
                    enabled: !App.busy && outField.text.length > 0 && !root.outDirInsideMods()
                    function currentAnswers() {
                        var mb = anyMiniBossRegion() ? "allRegions" : "off"
                        return {
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
                            outfitMode: outfitModeValue(),
                            outfitVanillaShieldRegen: outfitShieldRegen.checked,
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
                                exAmmo100x.checked ? "ammo100x" : "",
                                exConsumables.checked ? "consumableStacks" : "",
                                exShieldRegen.checked ? "shieldRegen" : "",
                                exAttributeShieldRegen.checked ? "attributeShieldRegen" : "",
                                exBaseAttributes.checked ? "baseAttributes" : "",
                                exHighGauge.checked ? "highGaugeCapacity" : "",
                                exPassiveHp.checked ? "passiveHpRegen" : "",
                                exFishing.checked ? "fishingPower" : "",
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
                                exGaugeOverTime.checked ? "gaugeRecoveryOverTime" : "",
                                exDashCooldown.checked ? "dashCooldown4" : "",
                                exDroneScan.checked ? "droneScanBoost" : "",
                                exGunRotation.checked ? "gunGorgonRotation" : "",
                                exJust.checked ? "forgivingJust" : "",
                                exAirDodge.checked ? "extraAirDodge" : "",
                                exTumbler.checked ? "tumblerHeal" : ""
                            ].filter(function(x){ return x.length > 0 }),
                            advancedQuantitySelection: advancedQuantities.checked,
                            gameplayExtraValues: {
                                base_attributes: {
                                    max_hp: baseHp.realValue,
                                    max_shield: baseShield.realValue,
                                    shield_reduction_percent: baseReduction.realValue
                                },
                                ammo_stacks: {
                                    stack_size: ammoStacksValue.realValue,
                                    values: advancedQuantities.checked ? {
                                        StackBullet1: ammoStack1.realValue,
                                        StackBullet2: ammoStack2.realValue,
                                        StackBullet3: ammoStack3.realValue,
                                        StackBullet4: ammoStack4.realValue,
                                        StackBullet5: ammoStack5.realValue,
                                        StackBullet6: ammoStack6.realValue
                                    } : {}
                                },
                                ammo_100x: {
                                    multiplier: ammoMultiplier.realValue,
                                    values: advancedQuantities.checked ? {
                                        StackBullet1: ammoCapacity1.realValue,
                                        StackBullet2: ammoCapacity2.realValue,
                                        StackBullet3: ammoCapacity3.realValue,
                                        StackBullet4: ammoCapacity4.realValue,
                                        StackBullet5: ammoCapacity5.realValue,
                                        StackBullet6: ammoCapacity6.realValue
                                    } : {}
                                },
                                consumable_stacks: {
                                    stack_size: consumableStacksValue.realValue,
                                    values: advancedQuantities.checked ? {
                                        StackConsumable1: consumableStack1.realValue,
                                        StackConsumable2: consumableStack2.realValue,
                                        StackConsumable3: consumableStack3.realValue,
                                        StackConsumable4: consumableStack4.realValue,
                                        StackConsumable5: consumableStack5.realValue,
                                        StackConsumable6: consumableStack6.realValue,
                                        StackConsumable7: consumableStack7.realValue
                                    } : {}
                                },
                                shield_regen: {
                                    normal: shieldNormal.realValue,
                                    combat: shieldCombat.realValue
                                },
                                attribute_shield_regen: {
                                    normal: attributeShieldNormal.realValue,
                                    combat: attributeShieldCombat.realValue
                                },
                                high_gauge_capacity: {
                                    beta: highGaugeBeta.realValue,
                                    burst: highGaugeBurst.realValue
                                },
                                passive_hp_regen: {per_second: passiveHpValue.realValue},
                                fishing_power: {power: fishingValue.realValue},
                                attack_speed: {multiplier: attackSpeedValue.realValue}
                            },
                            worldTweaks: [
                                wShop.checked ? "shopPrices" : "",
                                wDrops.checked ? "dropRates" : "",
                                wSp.checked ? "spExp" : "",
                                wUpgrades.checked ? "upgradeCosts" : "",
                                wFishing.checked ? "fishing" : ""
                            ].filter(function(x){ return x.length > 0 }),
                            worldTweakValues: {
                                shop_prices: {price_percent: shopPrice.realValue},
                                drop_rates: {
                                    chance_percent: dropChance.realValue,
                                    count_percent: dropCount.realValue
                                },
                                sp_exp: {exp_percent: spExpPercent.realValue},
                                upgrade_costs: {cost_percent: upgradeCost.realValue},
                                fishing: {
                                    stamina_percent: fishStamina.realValue,
                                    fighting_time_percent: fishTime.realValue
                                }
                            },
                            hardcoreEnemyBoost: hardcoreValue(),
                            forgivingJustMult: just15.checked ? 1.5 : (just2.checked ? 2 : 3),
                            airDodgeCount: air2.checked ? 2 : 3,
                            tumblerHealPercent: tumblerValue(),
                            customPatchesDir: tomlField.text,
                            helperMode: helperValue(),
                            helperIntervalSeconds: interval.value,
                            vanillaHelperBuild: vanillaHelperValue(),
                            lang: I18n.language
                        }
                    }
                    onClicked: {
                        var a = currentAnswers()
                        root.resultZip = ""
                        App.runBuilder(JSON.stringify(a), toFolderUrl(outField.text),
                                       instPaks.checked, instHelper.checked, root.gamePath)
                    }
                }

                // Mismo build, sin tocar la carpeta del juego: sirve para armar el
                // zip y pasarselo a otro (o probarlo antes de instalar).
                Button {
                    id: buildZipOnlyButton
                    Layout.fillWidth: true
                    text: I18n.s.builder_build_zip_only || "Compilar solo el ZIP"
                    enabled: buildButton.enabled
                    onClicked: {
                        root.resultZip = ""
                        App.runBuilder(JSON.stringify(buildButton.currentAnswers()),
                                       toFolderUrl(outField.text), false, false, root.gamePath)
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
                            Text { Layout.fillWidth: true; color: Theme.textDim; wrapMode: Text.WrapAnywhere
                                   text: "🧵 Helper (" + ((root.installed.helpers && root.installed.helpers.length > 0)
                                          ? root.installed.helpers.join(", ") : "StellarSoulsOutfitRestore") + ")" }
                            Button { text: I18n.s.builder_uninstall_helper || "Desinstalar helper"
                                     enabled: !App.busy; onClicked: uninstallDialog.ask("helper") }
                        }
                    }
                }

                // ---- Paks fantasma: cargables, pero no instalados por la tool ----
                Card {
                    visible: root.installed.shadowPaks && root.installed.shadowPaks.length > 0
                    Layout.fillWidth: true
                    color: Theme.panelAlt
                    implicitHeight: shadowCol.implicitHeight + 24
                    ColumnLayout {
                        id: shadowCol
                        anchors.fill: parent; anchors.margins: 12; spacing: 6
                        Text { text: "⚠ " + (I18n.s.builder_shadow_title || "Paks fantasma en ~mods")
                               color: Theme.warn; font.bold: true }
                        Text { text: I18n.s.builder_shadow_hint
                                     || "El juego carga ~mods recursivamente. Estos paks se cargan pero no los instalo esta herramienta (suelen quedar de un build compilado adentro de ~mods) y pueden pisar al mod instalado. Borralos a mano."
                               color: Theme.textDim; font.pixelSize: 11
                               wrapMode: Text.Wrap; Layout.fillWidth: true }
                        Text { text: root.installed.shadowPaks ? root.installed.shadowPaks.join("\n") : ""
                               color: Theme.textDim; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true }
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
                                         onClicked: root.applyHistoryTemplate(modelData.id) }
                                Button { text: I18n.s.builder_reexport || "Re-exportar ZIP"
                                         enabled: !App.busy && outField.text.length > 0
                                                  && !root.outDirInsideMods()
                                         onClicked: App.reexportBuild(modelData.id, toFolderUrl(outField.text)) }
                            }
                        }
                    }
                }
            }
        }
    }
}
