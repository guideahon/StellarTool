#include <QtTest>
#include <QDir>
#include <QSettings>
#include <QTemporaryDir>

#include "core/PakService.h"

using namespace st;

// Oodle (oo2core_9_win64.dll) no se distribuye: sale del juego del usuario.
// Los reportes "no se encontró la DLL" venían de copias con otra caja y de que
// no había forma de indicarla a mano, así que eso es lo que se cubre.
class TestOodle : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void userOverrideFile();
    void userOverrideDirCaseInsensitive();
    void clearedOverrideIsForgotten();
    void reportMentionsGameFolder();

private:
    QString m_savedOverride;
};

static QString makeDll(const QDir &dir, const QString &name) {
    QFile f(dir.filePath(name));
    if (!f.open(QIODevice::WriteOnly)) return {};
    f.write("not really oodle");
    f.close();
    return QFileInfo(f).absoluteFilePath();
}

void TestOodle::initTestCase() {
    QCoreApplication::setOrganizationName(QStringLiteral("StellarTool"));
    QCoreApplication::setApplicationName(QStringLiteral("StellarToolTests"));
    m_savedOverride = PakService::userOodlePath();
}

void TestOodle::cleanupTestCase() {
    PakService::setUserOodlePath(m_savedOverride);
}

void TestOodle::userOverrideFile() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString dll = makeDll(QDir(tmp.path()), QStringLiteral("oo2core_9_win64.dll"));
    QVERIFY(!dll.isEmpty());
    PakService::setUserOodlePath(dll);
    QCOMPARE(QFileInfo(PakService::oodleFilePath()).absoluteFilePath(), dll);
    QCOMPARE(QDir(PakService::oodleDir()).absolutePath(), QDir(tmp.path()).absolutePath());
}

void TestOodle::userOverrideDirCaseInsensitive() {
    // Copias hechas a mano y prefijos Wine sobre filesystems case-sensitive
    // dejan la DLL como .DLL; la búsqueda por nombre exacto no la veía.
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString dll = makeDll(QDir(tmp.path()), QStringLiteral("oo2core_9_win64.DLL"));
    QVERIFY(!dll.isEmpty());
    PakService::setUserOodlePath(tmp.path());
    QCOMPARE(QDir(PakService::oodleDir()).absolutePath(), QDir(tmp.path()).absolutePath());
}

void TestOodle::clearedOverrideIsForgotten() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    PakService::setUserOodlePath(makeDll(QDir(tmp.path()), QStringLiteral("oo2core_9_win64.dll")));
    QVERIFY(!PakService::userOodlePath().isEmpty());
    PakService::setUserOodlePath(QString());
    QVERIFY(PakService::userOodlePath().isEmpty());
    // El cache se invalida al cambiar el override: no puede seguir devolviendo
    // el archivo del temporal ya borrado.
    const QString after = PakService::oodleFilePath();
    QVERIFY(after.isEmpty() || !after.startsWith(QDir(tmp.path()).absolutePath()));
}

void TestOodle::reportMentionsGameFolder() {
    const QString report = PakService::oodleSearchReport();
    QVERIFY(report.contains(QStringLiteral("game folder")));
    QVERIFY(report.contains(QStringLiteral("tools")));
}

#include "TestOodle.moc"

void runTestOodle(int &failures, int argc, char **argv) {
    TestOodle test;
    failures += QTest::qExec(&test, argc, argv);
}
