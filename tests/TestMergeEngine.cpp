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

#include "TestMergeEngine.moc"

void runTestMergeEngine(int &failures, int argc, char **argv) {
    TestMergeEngine t;
    failures += QTest::qExec(&t, argc, argv);
}
