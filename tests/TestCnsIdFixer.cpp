#include "core/CnsIdFixerService.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>

using namespace st;

namespace {
void putU32(QByteArray &bytes, int offset, quint32 value) {
    qToLittleEndian<quint32>(value, reinterpret_cast<uchar *>(bytes.data() + offset));
}
void putU64(QByteArray &bytes, int offset, quint64 value) {
    qToLittleEndian<quint64>(value, reinterpret_cast<uchar *>(bytes.data() + offset));
}
quint64 getU64(const QByteArray &bytes, int offset) {
    return qFromLittleEndian<quint64>(
        reinterpret_cast<const uchar *>(bytes.constData() + offset));
}
void writeUtoc(const QString &path, quint64 containerId,
               std::initializer_list<quint64> packageIds) {
    const int count = int(packageIds.size()) + 1;
    QByteArray bytes(0x90 + count * 12, '\0');
    bytes.replace(0, 16, QByteArray("-==--==--==--==-", 16));
    bytes[16] = 2; // DirectoryIndex / UE4 chunk types
    putU32(bytes, 20, 0x90);
    putU32(bytes, 24, count);
    putU64(bytes, 52, containerId);
    int offset = 0x90;
    putU64(bytes, offset, containerId);
    bytes[offset + 11] = 10; // ContainerHeader
    offset += 12;
    for (quint64 packageId : packageIds) {
        putU64(bytes, offset, packageId);
        bytes[offset + 11] = 2; // ExportBundleData
        offset += 12;
    }
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(bytes), bytes.size());
}
}

class TestCnsIdFixer : public QObject {
    Q_OBJECT
private slots:
    void scanReportsContainerAndPackageConflicts() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        writeUtoc(dir.filePath("a.utoc"), 0x1234, {0xAA, 0xBB});
        writeUtoc(dir.filePath("b.utoc"), 0x1234, {0xAA, 0xCC});

        CnsIdFixerService service;
        const auto result = service.run(dir.path(), false);
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(result.containers, 2);
        QCOMPARE(result.duplicateContainers, 1);
        QCOMPARE(result.packageConflicts, 1);
        QVERIFY(result.report.contains(QStringLiteral("a.utoc")));
        QVERIFY(result.report.contains(QStringLiteral("b.utoc")));
        QVERIFY(!QFileInfo::exists(dir.filePath("b.utoc.cnsidfixer.bak")));
    }

    void fixesDuplicateAndCreatesBackup() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        writeUtoc(dir.filePath("a.utoc"), 0x1234, {0xAA});
        writeUtoc(dir.filePath("b.utoc"), 0x1234, {0xBB});

        CnsIdFixerService service;
        const auto result = service.run(dir.path(), true);
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(result.fixedContainers, 1);

        QFile patched(dir.filePath("b.utoc"));
        QVERIFY(patched.open(QIODevice::ReadOnly));
        const QByteArray bytes = patched.readAll();
        const quint64 newId = getU64(bytes, 52);
        QVERIFY(newId != quint64(0x1234));
        QCOMPARE(getU64(bytes, 0x90), newId);
        QVERIFY(QFileInfo::exists(dir.filePath("b.utoc.cnsidfixer.bak")));

        const auto after = service.run(dir.path(), false);
        QCOMPARE(after.duplicateContainers, 0);
    }

    void rejectsInvalidFolder() {
        CnsIdFixerService service;
        const auto result = service.run(QStringLiteral("Z:/definitely/missing"), false);
        QVERIFY(!result.ok);
        QVERIFY(!result.error.isEmpty());
    }
};

void runTestCnsIdFixer(int &failures, int argc, char **argv) {
    TestCnsIdFixer test;
    failures += QTest::qExec(&test, argc, argv);
}

#include "TestCnsIdFixer.moc"
