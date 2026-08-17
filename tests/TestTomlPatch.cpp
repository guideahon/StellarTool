#include <QtTest>
#include "core/TomlPatch.h"

class TestTomlPatch : public QObject {
    Q_OBJECT
private slots:
    void parsesLiteralAndOperations();
    void parsesRegexRows();
    void rejectsScriptsAndBadOperations();
    void appliesOperations();
};

void TestTomlPatch::parsesLiteralAndOperations() {
    const auto d = st::TomlPatch::parseDocument(
        "[meta]\n table = \"CharacterTable\"\n\n[Player]\nAttackSpeed = 1.5\nMaxHP = { op = \"multiply\", value = 2 }\n");
    QVERIFY2(d.errors.isEmpty(), qPrintable(d.errors.join("; ")));
    QCOMPARE(d.table, QStringLiteral("CharacterTable"));
    QCOMPARE(d.rules.size(), 2);
    QCOMPARE(d.rules.at(1).operation, st::TomlPatch::Operation::Multiply);
}

void TestTomlPatch::parsesRegexRows() {
    const auto d = st::TomlPatch::parseDocument("[row_regex:^Enemy_.*]\nMaxHP = { op = \"multiply\", value = 2 }\n");
    QVERIFY(d.errors.isEmpty());
    QCOMPARE(d.rules.size(), 1);
    QCOMPARE(d.rules.first().rowRegex, QStringLiteral("^Enemy_.*"));
}

void TestTomlPatch::rejectsScriptsAndBadOperations() {
    const auto d = st::TomlPatch::parseDocument("[Player]\nX = { op = \"run\", value = 1 }\n");
    QVERIFY(!d.errors.isEmpty());
    const auto script = st::TomlPatch::parseDocument("[Player]\nX = '''=> code'''\n");
    QVERIFY(!script.errors.isEmpty());
}

void TestTomlPatch::appliesOperations() {
    QJsonValue result; QString error;
    QVERIFY(st::TomlPatch::applyOperation(st::TomlPatch::Operation::Add, 10, 2, {}, {}, &result, &error));
    QCOMPARE(result.toDouble(), 12.0);
    QVERIFY(st::TomlPatch::applyOperation(st::TomlPatch::Operation::Multiply, 10, 2, {}, {}, &result, &error));
    QCOMPARE(result.toDouble(), 20.0);
    QVERIFY(st::TomlPatch::applyOperation(st::TomlPatch::Operation::Clamp, 10, {}, 0, 5, &result, &error));
    QCOMPARE(result.toDouble(), 5.0);
    QVERIFY(st::TomlPatch::applyOperation(st::TomlPatch::Operation::Toggle, true, {}, {}, {}, &result, &error));
    QVERIFY(!result.toBool());
}

#include "TestTomlPatch.moc"

void runTestTomlPatch(int &failures, int argc, char **argv) {
    TestTomlPatch test;
    failures += QTest::qExec(&test, argc, argv);
}
