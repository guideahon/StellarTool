#include <QtTest>
#include "core/MergeEngine.h"
#include "core/TableDiffEngine.h"
#include "Fixtures.h"

using namespace st;
using namespace fixtures;

class TestMergeEngine : public QObject {
    Q_OBJECT
private slots:
    void applySelected();
    void skipUnselected();
    void rowAddRemove();
    void roundTripDiffMergeVerify();
    void cleanStringEnumMerge();
    void newFNamesRegisteredInNameMap();
    void writesOverUAssetGuiFloatZero();
    void cleanEmptyNameArrayMerge();
    void cleanRowAddedNormalizesNestedEmptyFNames();
    void cleanModifiedClearedFNameBecomesNull();
    void cleanRowRemoved();
    void cleanRowRemovedMassLossGuard();
};

static QJsonObject baseTable() {
    return table({
        row(QStringLiteral("EVE"), {prop(QStringLiteral("MaxHP"), 100.0),
                                    prop(QStringLiteral("Speed"), 5.0)}),
        row(QStringLiteral("Drone"), {prop(QStringLiteral("MaxHP"), 50.0)}),
    });
}

static double leafValue(const QJsonObject &root, const QString &rowName, const QString &propName) {
    for (const QJsonValue &r : dataTableRows(root)) {
        const QJsonObject ro = r.toObject();
        if (ro.value(QLatin1String("Name")).toString() != rowName) continue;
        for (const QJsonValue &p : ro.value(QLatin1String("Value")).toArray())
            if (p.toObject().value(QLatin1String("Name")).toString() == propName)
                return p.toObject().value(QLatin1String("Value")).toDouble();
    }
    return -1;
}

void TestMergeEngine::applySelected() {
    const QJsonObject mod = table({
        row(QStringLiteral("EVE"), {prop(QStringLiteral("MaxHP"), 250.0),
                                    prop(QStringLiteral("Speed"), 9.0)}),
        row(QStringLiteral("Drone"), {prop(QStringLiteral("MaxHP"), 50.0)}),
    });
    auto items = TableDiffEngine::diffTable(baseTable(), mod, QStringLiteral("t.uasset"),
                                            QStringLiteral("m1"), QStringLiteral("Mod 1"));
    QJsonObject out = baseTable();
    const auto res = MergeEngine::applyToTable(out, items);
    QVERIFY(res.ok);
    QCOMPARE(res.applied, 2);
    QCOMPARE(leafValue(out, QStringLiteral("EVE"), QStringLiteral("MaxHP")), 250.0);
    QCOMPARE(leafValue(out, QStringLiteral("EVE"), QStringLiteral("Speed")), 9.0);
}

void TestMergeEngine::skipUnselected() {
    const QJsonObject mod = table({
        row(QStringLiteral("EVE"), {prop(QStringLiteral("MaxHP"), 250.0),
                                    prop(QStringLiteral("Speed"), 9.0)}),
        row(QStringLiteral("Drone"), {prop(QStringLiteral("MaxHP"), 50.0)}),
    });
    auto items = TableDiffEngine::diffTable(baseTable(), mod, QStringLiteral("t.uasset"),
                                            QStringLiteral("m1"), QStringLiteral("Mod 1"));
    for (auto &c : items)
        if (c.displayPath() == QLatin1String("Speed")) c.selected = false;
    QJsonObject out = baseTable();
    const auto res = MergeEngine::applyToTable(out, items);
    QVERIFY(res.ok);
    QCOMPARE(leafValue(out, QStringLiteral("EVE"), QStringLiteral("MaxHP")), 250.0);
    QCOMPARE(leafValue(out, QStringLiteral("EVE"), QStringLiteral("Speed")), 5.0);
}

void TestMergeEngine::rowAddRemove() {
    const QJsonObject mod = table({
        row(QStringLiteral("EVE"), {prop(QStringLiteral("MaxHP"), 100.0),
                                    prop(QStringLiteral("Speed"), 5.0)}),
        row(QStringLiteral("Boss"), {prop(QStringLiteral("MaxHP"), 999.0)}),
    });
    auto items = TableDiffEngine::diffTable(baseTable(), mod, QStringLiteral("t.uasset"),
                                            QStringLiteral("m1"), QStringLiteral("Mod 1"));
    QJsonObject out = baseTable();
    const auto res = MergeEngine::applyToTable(out, items);
    QVERIFY(res.ok);
    QCOMPARE(leafValue(out, QStringLiteral("Boss"), QStringLiteral("MaxHP")), 999.0);
    QCOMPARE(dataTableRows(out).size(), 2); // Drone eliminada, Boss agregada
}

void TestMergeEngine::roundTripDiffMergeVerify() {
    // Verificación estilo post-merge: diff(base, merged) == cambios aplicados.
    const QJsonObject mod = table({
        row(QStringLiteral("EVE"), {prop(QStringLiteral("MaxHP"), 300.0),
                                    prop(QStringLiteral("Speed"), 5.0)}),
        row(QStringLiteral("Drone"), {prop(QStringLiteral("MaxHP"), 25.0)}),
    });
    auto items = TableDiffEngine::diffTable(baseTable(), mod, QStringLiteral("t.uasset"),
                                            QStringLiteral("m1"), QStringLiteral("Mod 1"));
    QJsonObject merged = baseTable();
    QVERIFY(MergeEngine::applyToTable(merged, items).ok);
    auto verify = TableDiffEngine::diffTable(baseTable(), merged, QStringLiteral("t.uasset"),
                                             QStringLiteral("v"), QStringLiteral("v"));
    QCOMPARE(verify.size(), items.size());
    for (const auto &v : verify) {
        bool found = false;
        for (const auto &c : items)
            if (c.key() == v.key() && jsonValueEquals(c.newValue, v.newValue)) { found = true; break; }
        QVERIFY2(found, qPrintable(v.summary()));
    }
}

static QString leafString(const QJsonObject &root, const QString &rowName, const QString &propName) {
    for (const QJsonValue &r : dataTableRows(root)) {
        const QJsonObject ro = r.toObject();
        if (ro.value(QLatin1String("Name")).toString() != rowName) continue;
        for (const QJsonValue &p : ro.value(QLatin1String("Value")).toArray())
            if (p.toObject().value(QLatin1String("Name")).toString() == propName)
                return p.toObject().value(QLatin1String("Value")).toString();
    }
    return QStringLiteral("<none>");
}

// Fase 1: mods Zen (clean=true) escriben strings/enums, reconciliando contra el
// leaf real de UAssetGUI (namespace de enum, None -> nombre).
void TestMergeEngine::cleanStringEnumMerge() {
    // Base cruda UAssetGUI: enum con namespace, y un FName None (null).
    const QJsonObject base = table({
        row(QStringLiteral("Skill1"), {
            prop(QStringLiteral("NextStep"), QStringLiteral("ESBType::Old"), QStringLiteral("EnumPropertyData")),
            prop(QStringLiteral("Alias"), QJsonValue(QJsonValue::Null), QStringLiteral("NamePropertyData")),
        }),
    });
    // Mod normalizado (como lo entrega CUE4Parse tras normalizeDataTableDoc):
    // enum sin namespace, y el None ahora es un nombre real.
    const QJsonObject mod = table({
        row(QStringLiteral("Skill1"), {
            prop(QStringLiteral("NextStep"), QStringLiteral("New"), QStringLiteral("EnumPropertyData")),
            prop(QStringLiteral("Alias"), QStringLiteral("P_Eve_X"), QStringLiteral("NamePropertyData")),
        }),
    });
    auto items = TableDiffEngine::diffTable(base, mod, QStringLiteral("t.uasset"),
                                            QStringLiteral("m1"), QStringLiteral("Mod 1"));
    for (auto &c : items) c.clean = true; // simular lectura CUE4Parse
    QJsonObject out = base;
    const auto res = MergeEngine::applyToTable(out, items);
    QVERIFY(res.ok);
    QCOMPARE(res.applied, 2);
    QCOMPARE(res.skipped, 0);
    // Enum: se re-prefija el namespace de la base.
    QCOMPARE(leafString(out, QStringLiteral("Skill1"), QStringLiteral("NextStep")),
             QStringLiteral("ESBType::New"));
    // None -> nombre real.
    QCOMPARE(leafString(out, QStringLiteral("Skill1"), QStringLiteral("Alias")),
             QStringLiteral("P_Eve_X"));
}

// Un FName nuevo (no presente en el NameMap del asset) debe quedar registrado:
// si no, UAssetAPI lo trata como dummy y al escribir tira
// DummyFNameSerializationException (UAssetGUI muere sin generar el uasset).
void TestMergeEngine::newFNamesRegisteredInNameMap() {
    QJsonObject base = table({
        row(QStringLiteral("Skill1"), {
            prop(QStringLiteral("Alias"), QStringLiteral("OldName"),
                 QStringLiteral("NamePropertyData")),
        }),
    });
    base.insert(QStringLiteral("NameMap"),
                QJsonArray{QStringLiteral("Skill1"), QStringLiteral("Alias"),
                           QStringLiteral("OldName")});
    const QJsonObject mod = table({
        row(QStringLiteral("Skill1"), {
            prop(QStringLiteral("Alias"), QStringLiteral("BrandNewName"),
                 QStringLiteral("NamePropertyData")),
        }),
    });
    auto items = TableDiffEngine::diffTable(base, mod, QStringLiteral("t.uasset"),
                                            QStringLiteral("m1"), QStringLiteral("Mod 1"));
    for (auto &c : items) c.clean = true;
    QJsonObject out = base;
    QVERIFY(MergeEngine::applyToTable(out, items).ok);

    QStringList names;
    for (const QJsonValue &n : out.value(QLatin1String("NameMap")).toArray())
        names << n.toString();
    QVERIFY2(names.contains(QStringLiteral("BrandNewName")),
             qPrintable(names.join(QLatin1Char(','))));
    QVERIFY(names.contains(QStringLiteral("OldName"))); // no se pierden los previos
}

// UAssetGUI serializa el float cero como el string "+0": escribir un numero
// encima debe funcionar (es el caso comun de "activar algo que vale 0").
void TestMergeEngine::writesOverUAssetGuiFloatZero() {
    const QJsonObject base = table({
        row(QStringLiteral("Player"), {prop(QStringLiteral("DrainHp"), QStringLiteral("+0"))}),
    });
    const QJsonObject mod = table({
        row(QStringLiteral("Player"), {prop(QStringLiteral("DrainHp"), 600.0)}),
    });
    auto items = TableDiffEngine::diffTable(base, mod, QStringLiteral("t.uasset"),
                                            QStringLiteral("m1"), QStringLiteral("Mod 1"));
    for (auto &c : items) c.clean = true;   // simular lectura CUE4Parse
    QJsonObject out = base;
    const auto res = MergeEngine::applyToTable(out, items);
    QVERIFY(res.ok);
    QCOMPARE(res.applied, 1);
    QCOMPARE(res.skipped, 0);
    QCOMPARE(leafValue(out, QStringLiteral("Player"), QStringLiteral("DrainHp")), 600.0);
}

void TestMergeEngine::cleanEmptyNameArrayMerge() {
    QJsonObject arrayProp = prop(QStringLiteral("ChainEffectAliasArray"), QJsonArray{},
                                 QStringLiteral("ArrayPropertyData"));
    arrayProp.insert(QStringLiteral("ArrayType"), QStringLiteral("NameProperty"));
    const QJsonObject base = table({row(QStringLiteral("Skill1"), {arrayProp})});
    QJsonObject modArrayProp = prop(
        QStringLiteral("ChainEffectAliasArray"),
        QJsonArray{QStringLiteral("P_Eve_Gun_Missile_TimeScaleEnd")},
        QStringLiteral("ArrayPropertyData"));
    modArrayProp.insert(QStringLiteral("ArrayType"), QStringLiteral("NameProperty"));
    const QJsonObject mod = table({row(QStringLiteral("Skill1"), {modArrayProp})});
    auto items = TableDiffEngine::diffTable(base, mod, QStringLiteral("t.uasset"),
                                            QStringLiteral("m1"), QStringLiteral("Mod 1"));
    for (auto &c : items) c.clean = true;
    QJsonObject out = base;
    const auto res = MergeEngine::applyToTable(out, items);
    QVERIFY(res.ok);
    QCOMPARE(res.applied, 1);
    QCOMPARE(res.skipped, 0);

    const QJsonObject written = dataTableRows(out).first().toObject()
        .value(QStringLiteral("Value")).toArray().first().toObject()
        .value(QStringLiteral("Value")).toArray().first().toObject();
    QVERIFY(written.value(QStringLiteral("$type")).toString()
            .contains(QStringLiteral("NamePropertyData")));
    QCOMPARE(written.value(QStringLiteral("ArrayIndex")).toInt(), 0);
    QCOMPARE(written.value(QStringLiteral("Value")).toString(),
             QStringLiteral("P_Eve_Gun_Missile_TimeScaleEnd"));
}

void TestMergeEngine::cleanRowAddedNormalizesNestedEmptyFNames() {
    QJsonObject nestedName = prop(QStringLiteral("NestedAlias"), QJsonValue(QJsonValue::Null),
                                  QStringLiteral("NamePropertyData"));
    const QJsonObject base = table({row(QStringLiteral("Existing"), {
        prop(QStringLiteral("Alias"), QStringLiteral("Old"), QStringLiteral("NamePropertyData")),
        prop(QStringLiteral("Nested"), QJsonArray{nestedName},
             QStringLiteral("StructPropertyData"))
    })});
    QJsonObject mod = table({row(QStringLiteral("Existing"), {
        prop(QStringLiteral("Alias"), QStringLiteral("Old"), QStringLiteral("NamePropertyData")),
        prop(QStringLiteral("Nested"), QJsonArray{nestedName},
             QStringLiteral("StructPropertyData"))
    }), row(QStringLiteral("Added"), {
        prop(QStringLiteral("Alias"), QStringLiteral("New"), QStringLiteral("NamePropertyData")),
        prop(QStringLiteral("Nested"),
             QJsonArray{prop(QStringLiteral("NestedAlias"), QStringLiteral(""),
                             QStringLiteral("NamePropertyData"))},
             QStringLiteral("StructPropertyData"))
    })});
    auto items = TableDiffEngine::diffTable(base, mod, QStringLiteral("t.uasset"),
                                            QStringLiteral("m1"), QStringLiteral("Mod 1"));
    for (auto &c : items) c.clean = true;
    QJsonObject out = base;
    const auto res = MergeEngine::applyToTable(out, items);
    QVERIFY(res.ok);
    QCOMPARE(res.applied, 1);
    QCOMPARE(res.skipped, 0);

    const QJsonObject added = dataTableRows(out).last().toObject();
    QCOMPARE(added.value(QStringLiteral("Name")).toString(), QStringLiteral("Added"));
    const QJsonArray props = added.value(QStringLiteral("Value")).toArray();
    const QJsonArray nested = props.at(1).toObject().value(QStringLiteral("Value")).toArray();
    // "None" y no null: null se escribe pero vuelve como " " al releer.
    QCOMPARE(nested.first().toObject().value(QStringLiteral("Value")).toString(),
             QStringLiteral("None"));
    QStringList names;
    for (const QJsonValue &name : out.value(QStringLiteral("NameMap")).toArray())
        names << name.toString();
    QVERIFY(names.contains(QStringLiteral("Added")));
    QVERIFY(names.contains(QStringLiteral("New")));
}

// Un mod que VACÍA un FName que en vanilla tenía valor: el diff lo canoniza a
// "" y escribirlo tal cual mata a UAssetAPI al generar el uasset.
void TestMergeEngine::cleanModifiedClearedFNameBecomesNull() {
    const QJsonObject base = table({row(QStringLiteral("Hit1"), {
        prop(QStringLiteral("Alias"), QStringLiteral("P_Eve_Target"),
             QStringLiteral("NamePropertyData"))})});
    const QJsonObject mod = table({row(QStringLiteral("Hit1"), {
        prop(QStringLiteral("Alias"), QStringLiteral(""),
             QStringLiteral("NamePropertyData"))})});
    auto items = TableDiffEngine::diffTable(base, mod, QStringLiteral("t.uasset"),
                                            QStringLiteral("m1"), QStringLiteral("Mod 1"));
    for (auto &c : items) c.clean = true;
    QJsonObject out = base;
    const auto res = MergeEngine::applyToTable(out, items);
    QVERIFY(res.ok);
    QCOMPARE(res.applied, 1);
    const QJsonObject changed = dataTableRows(out).first().toObject();
    QCOMPARE(changed.value(QStringLiteral("Value")).toArray().first().toObject()
                 .value(QStringLiteral("Value")).toString(),
             QStringLiteral("None"));
}

void TestMergeEngine::cleanRowRemoved() {
    QJsonArray baseRows;
    for (int i = 0; i < 8; ++i)
        baseRows.append(row(QStringLiteral("Row%1").arg(i),
                            {prop(QStringLiteral("Value"), double(i))}));
    QJsonArray modRows = baseRows;
    modRows.removeAt(3); // 1/8: borrado puntual, no export truncado.

    const QJsonObject base = table(baseRows);
    const QJsonObject mod = table(modRows);
    auto items = TableDiffEngine::diffTable(base, mod, QStringLiteral("t.uasset"),
                                            QStringLiteral("m1"), QStringLiteral("Mod 1"));
    for (auto &c : items) c.clean = true;
    QJsonObject out = base;
    const auto res = MergeEngine::applyToTable(out, items);
    QVERIFY(res.ok);
    QCOMPARE(res.applied, 1);
    QCOMPARE(res.skipped, 0);
    QCOMPARE(dataTableRows(out).size(), 7);
    for (const QJsonValue &r : dataTableRows(out))
        QVERIFY(r.toObject().value(QStringLiteral("Name")).toString()
                != QStringLiteral("Row3"));
}

void TestMergeEngine::cleanRowRemovedMassLossGuard() {
    QJsonArray baseRows;
    for (int i = 0; i < 8; ++i)
        baseRows.append(row(QStringLiteral("Row%1").arg(i),
                            {prop(QStringLiteral("Value"), double(i))}));
    QJsonArray truncatedRows;
    for (int i = 0; i < 5; ++i)
        truncatedRows.append(baseRows.at(i)); // faltan 3/8 (>25%).

    const QJsonObject base = table(baseRows);
    const QJsonObject mod = table(truncatedRows);
    auto items = TableDiffEngine::diffTable(base, mod, QStringLiteral("t.uasset"),
                                            QStringLiteral("m1"), QStringLiteral("Mod 1"));
    for (auto &c : items) c.clean = true;
    QJsonObject out = base;
    const auto res = MergeEngine::applyToTable(out, items);
    QVERIFY(res.ok);
    QCOMPARE(res.applied, 0);
    QCOMPARE(res.skipped, 3);
    QCOMPARE(dataTableRows(out).size(), 8);
}

#include "TestMergeEngine.moc"

void runTestMergeEngine(int &failures, int argc, char **argv) {
    TestMergeEngine t;
    failures += QTest::qExec(&t, argc, argv);
}
