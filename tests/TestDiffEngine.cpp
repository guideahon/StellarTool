#include <QtTest>
#include "core/TableDiffEngine.h"
#include "Fixtures.h"

using namespace st;
using namespace fixtures;

class TestDiffEngine : public QObject {
    Q_OBJECT
private slots:
    void modifiedLeaf();
    void rowAddedRemoved();
    void nestedStructAndArray();
    void floatEpsilonIgnored();
    void conflictDetected();
    void identicalChangesNoConflict();
    void noBaselineRowsAsAdded();
};

static QJsonObject baseTable() {
    return table({
        row(QStringLiteral("EVE"), {prop(QStringLiteral("MaxHP"), 100.0),
                                    prop(QStringLiteral("Speed"), 5.0)}),
        row(QStringLiteral("Drone"), {prop(QStringLiteral("MaxHP"), 50.0)}),
    });
}

void TestDiffEngine::modifiedLeaf() {
    const QJsonObject mod = table({
        row(QStringLiteral("EVE"), {prop(QStringLiteral("MaxHP"), 250.0),
                                    prop(QStringLiteral("Speed"), 5.0)}),
        row(QStringLiteral("Drone"), {prop(QStringLiteral("MaxHP"), 50.0)}),
    });
    const auto items = TableDiffEngine::diffTable(baseTable(), mod,
        QStringLiteral("SB/Content/Data/CharacterTable.uasset"), QStringLiteral("m1"), QStringLiteral("Mod 1"));
    QCOMPARE(items.size(), 1);
    QCOMPARE(items[0].type, ChangeItem::Modified);
    QCOMPARE(items[0].rowName, QStringLiteral("EVE"));
    QCOMPARE(items[0].baseValue.toDouble(), 100.0);
    QCOMPARE(items[0].newValue.toDouble(), 250.0);
    QCOMPARE(items[0].displayPath(), QStringLiteral("MaxHP"));
    QVERIFY(items[0].summary().contains(QStringLiteral("100 → 250")));
    QVERIFY(items[0].summary().contains(QStringLiteral("+150%")));
}

void TestDiffEngine::rowAddedRemoved() {
    const QJsonObject mod = table({
        row(QStringLiteral("EVE"), {prop(QStringLiteral("MaxHP"), 100.0),
                                    prop(QStringLiteral("Speed"), 5.0)}),
        row(QStringLiteral("Boss"), {prop(QStringLiteral("MaxHP"), 999.0)}),
    });
    auto items = TableDiffEngine::diffTable(baseTable(), mod,
        QStringLiteral("t.uasset"), QStringLiteral("m1"), QStringLiteral("Mod 1"));
    QCOMPARE(items.size(), 2);
    bool added = false, removed = false;
    for (const auto &c : items) {
        if (c.type == ChangeItem::RowAdded)   { QCOMPARE(c.rowName, QStringLiteral("Boss"));  added = true; }
        if (c.type == ChangeItem::RowRemoved) { QCOMPARE(c.rowName, QStringLiteral("Drone")); removed = true; }
    }
    QVERIFY(added && removed);
}

void TestDiffEngine::nestedStructAndArray() {
    auto mkBase = [] {
        return table({row(QStringLiteral("EVE"), {
            prop(QStringLiteral("Stats"),
                 QJsonArray{prop(QStringLiteral("Atk"), 10.0), prop(QStringLiteral("Def"), 3.0)},
                 QStringLiteral("StructPropertyData")),
            prop(QStringLiteral("Costs"), QJsonArray{1.0, 2.0, 3.0}, QStringLiteral("ArrayPropertyData")),
        })});
    };
    QJsonObject mod = table({row(QStringLiteral("EVE"), {
        prop(QStringLiteral("Stats"),
             QJsonArray{prop(QStringLiteral("Atk"), 10.0), prop(QStringLiteral("Def"), 7.0)},
             QStringLiteral("StructPropertyData")),
        prop(QStringLiteral("Costs"), QJsonArray{1.0, 9.0, 3.0}, QStringLiteral("ArrayPropertyData")),
    })});
    const auto items = TableDiffEngine::diffTable(mkBase(), mod,
        QStringLiteral("t.uasset"), QStringLiteral("m1"), QStringLiteral("Mod 1"));
    QCOMPARE(items.size(), 2);
    QStringList paths;
    for (const auto &c : items) paths << c.displayPath();
    QVERIFY(paths.contains(QStringLiteral("Stats.Def")));
    QVERIFY(paths.contains(QStringLiteral("Costs[1]")));
}

void TestDiffEngine::floatEpsilonIgnored() {
    const QJsonObject mod = table({
        row(QStringLiteral("EVE"), {prop(QStringLiteral("MaxHP"), 100.0000001),
                                    prop(QStringLiteral("Speed"), 5.0)}),
        row(QStringLiteral("Drone"), {prop(QStringLiteral("MaxHP"), 50.0)}),
    });
    const auto items = TableDiffEngine::diffTable(baseTable(), mod,
        QStringLiteral("t.uasset"), QStringLiteral("m1"), QStringLiteral("Mod 1"));
    QCOMPARE(items.size(), 0);
}

void TestDiffEngine::conflictDetected() {
    const QJsonObject modA = table({row(QStringLiteral("EVE"),
        {prop(QStringLiteral("MaxHP"), 250.0), prop(QStringLiteral("Speed"), 5.0)}),
        row(QStringLiteral("Drone"), {prop(QStringLiteral("MaxHP"), 50.0)})});
    const QJsonObject modB = table({row(QStringLiteral("EVE"),
        {prop(QStringLiteral("MaxHP"), 300.0), prop(QStringLiteral("Speed"), 8.0)}),
        row(QStringLiteral("Drone"), {prop(QStringLiteral("MaxHP"), 50.0)})});

    QList<ChangeItem> all;
    all << TableDiffEngine::diffTable(baseTable(), modA, QStringLiteral("t.uasset"),
                                      QStringLiteral("mA"), QStringLiteral("Mod A"));
    all << TableDiffEngine::diffTable(baseTable(), modB, QStringLiteral("t.uasset"),
                                      QStringLiteral("mB"), QStringLiteral("Mod B"));
    const auto groups = TableDiffEngine::findConflicts(all);
    // MaxHP difiere (250 vs 300) => conflicto. Speed solo la toca modB => sin conflicto.
    QCOMPARE(groups.size(), 1);
    QCOMPARE(groups[0].itemIndexes.size(), 2);
    int conflicted = 0, free_ = 0;
    for (const auto &c : all) (c.conflictGroup >= 0 ? conflicted : free_)++;
    QCOMPARE(conflicted, 2);
    QCOMPARE(free_, 1);
}

void TestDiffEngine::identicalChangesNoConflict() {
    const QJsonObject modded = table({row(QStringLiteral("EVE"),
        {prop(QStringLiteral("MaxHP"), 250.0), prop(QStringLiteral("Speed"), 5.0)}),
        row(QStringLiteral("Drone"), {prop(QStringLiteral("MaxHP"), 50.0)})});
    QList<ChangeItem> all;
    all << TableDiffEngine::diffTable(baseTable(), modded, QStringLiteral("t.uasset"),
                                      QStringLiteral("mA"), QStringLiteral("Mod A"));
    all << TableDiffEngine::diffTable(baseTable(), modded, QStringLiteral("t.uasset"),
                                      QStringLiteral("mB"), QStringLiteral("Mod B"));
    const auto groups = TableDiffEngine::findConflicts(all);
    QCOMPARE(groups.size(), 0);
}

void TestDiffEngine::noBaselineRowsAsAdded() {
    const QJsonObject mod = table({row(QStringLiteral("EVE"),
        {prop(QStringLiteral("MaxHP"), 250.0)})});
    const auto items = TableDiffEngine::diffTable(QJsonObject(), mod,
        QStringLiteral("t.uasset"), QStringLiteral("m1"), QStringLiteral("Mod 1"));
    QCOMPARE(items.size(), 1);
    QCOMPARE(items[0].type, ChangeItem::RowAdded);
    QVERIFY(items[0].baseValue.isUndefined());
}

#include "TestDiffEngine.moc"

// Registro para el runner común.
void runTestDiffEngine(int &failures, int argc, char **argv) {
    TestDiffEngine t;
    failures += QTest::qExec(&t, argc, argv);
}
