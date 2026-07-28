#include <QtTest>
#include "core/UpdateService.h"

using namespace st;

class TestUpdateService : public QObject {
    Q_OBJECT
private slots:
    void newerVersionWins();
    void equalVersions();
    void tagPrefixIgnored();
    void differentLengths();
    void suffixesIgnored();
    void doubleDigitComponents();
};

void TestUpdateService::newerVersionWins() {
    QCOMPARE(UpdateService::compareVersions(QStringLiteral("0.4.0"), QStringLiteral("0.3.13")), 1);
    QCOMPARE(UpdateService::compareVersions(QStringLiteral("0.3.13"), QStringLiteral("0.4.0")), -1);
    QCOMPARE(UpdateService::compareVersions(QStringLiteral("1.0.0"), QStringLiteral("0.99.99")), 1);
}

void TestUpdateService::equalVersions() {
    QCOMPARE(UpdateService::compareVersions(QStringLiteral("0.3.13"), QStringLiteral("0.3.13")), 0);
    // "0.3" y "0.3.0" son la misma versión: los componentes ausentes valen 0.
    QCOMPARE(UpdateService::compareVersions(QStringLiteral("0.3"), QStringLiteral("0.3.0")), 0);
}

void TestUpdateService::tagPrefixIgnored() {
    QCOMPARE(UpdateService::compareVersions(QStringLiteral("v0.3.14"), QStringLiteral("0.3.13")), 1);
    QCOMPARE(UpdateService::compareVersions(QStringLiteral("V0.3.13"), QStringLiteral("0.3.13")), 0);
    QCOMPARE(UpdateService::compareVersions(QStringLiteral(" v0.3.13 "), QStringLiteral("0.3.13")), 0);
}

void TestUpdateService::differentLengths() {
    QCOMPARE(UpdateService::compareVersions(QStringLiteral("0.3.13.1"), QStringLiteral("0.3.13")), 1);
    QCOMPARE(UpdateService::compareVersions(QStringLiteral("0.3.13"), QStringLiteral("0.3.13.1")), -1);
}

void TestUpdateService::suffixesIgnored() {
    // El sufijo se descarta: solo se comparan los números, así una tag
    // "0.4.0-beta" no se lee como más vieja que "0.4.0".
    QCOMPARE(UpdateService::compareVersions(QStringLiteral("0.4.0-beta"), QStringLiteral("0.4.0")), 0);
    QCOMPARE(UpdateService::compareVersions(QStringLiteral("0.4.0-beta"), QStringLiteral("0.3.13")), 1);
}

void TestUpdateService::doubleDigitComponents() {
    // Comparación numérica, no lexicográfica: 0.3.9 < 0.3.10.
    QCOMPARE(UpdateService::compareVersions(QStringLiteral("0.3.10"), QStringLiteral("0.3.9")), 1);
    QCOMPARE(UpdateService::compareVersions(QStringLiteral("0.10.0"), QStringLiteral("0.9.9")), 1);
}

#include "TestUpdateService.moc"

// Registro para el runner común.
void runTestUpdateService(int &failures, int argc, char **argv) {
    TestUpdateService t;
    failures += QTest::qExec(&t, argc, argv);
}
