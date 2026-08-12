#include "core/GamePaths.h"
#include "core/ReShadePresetService.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QTemporaryDir>
#include <QtTest>

using namespace st;

class TestReShadePresetService : public QObject {
    Q_OBJECT
private slots:
    void savesRestoresAndBacksUp();
    void reportsMissingShaders();
};

static bool writeFile(const QString &path, const QByteArray &body) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    file.write(body); return true;
}

void TestReShadePresetService::savesRestoresAndBacksUp() {
    QTemporaryDir game;
    QVERIFY(game.isValid());
    const QString win64 = game.path() + "/SB/Binaries/Win64";
    QVERIFY(writeFile(win64 + "/ReShade.ini", "[GENERAL]\nPresetPath=original.ini\n"));
    QVERIFY(writeFile(win64 + "/original.ini", "[GENERAL]\nTechniques=LumaSharpen\n"));
    const QString oldRoot = GamePaths::gameRoot();
    GamePaths::setGameRoot(game.path());

    ReShadePresetService service;
    QVERIFY(service.available());
    QVERIFY(service.saveCurrentPreset(QStringLiteral("Test ReShade")));
    QVERIFY(service.presetsJson().contains(QStringLiteral("Test ReShade")));
    QVERIFY(service.restorePreset(QStringLiteral("Test ReShade")));
    QVERIFY(!service.lastBackupPath().isEmpty());
    QVERIFY(QFileInfo::exists(service.lastBackupPath() + "/ReShade.ini"));
    QVERIFY(QFileInfo::exists(win64 + "/StellarTool_ReShade"));
    QFile config(win64 + "/ReShade.ini");
    QVERIFY(config.open(QIODevice::ReadOnly));
    QVERIFY(config.readAll().contains("PresetPath=StellarTool_ReShade/"));
    QVERIFY(service.deletePreset(QStringLiteral("Test ReShade")));
    GamePaths::setGameRoot(oldRoot);
}

void TestReShadePresetService::reportsMissingShaders() {
    QTemporaryDir game;
    QVERIFY(game.isValid());
    const QString win64 = game.path() + "/SB/Binaries/Win64";
    QVERIFY(writeFile(win64 + "/ReShade.ini", "[GENERAL]\nPresetPath=missing.ini\n"));
    QVERIFY(writeFile(win64 + "/missing.ini", "[GENERAL]\nTechniques=NotInstalled.fx\n"));
    const QString oldRoot = GamePaths::gameRoot();
    GamePaths::setGameRoot(game.path());
    ReShadePresetService service;
    QVERIFY(service.saveCurrentPreset(QStringLiteral("Missing Shader Test")));
    QVERIFY(service.presetsJson().contains(QStringLiteral("Faltan shaders")));
    QVERIFY(service.deletePreset(QStringLiteral("Missing Shader Test")));
    GamePaths::setGameRoot(oldRoot);
}

void runTestReShadePresetService(int &failures, int argc, char **argv) {
    TestReShadePresetService test;
    failures += QTest::qExec(&test, argc, argv);
}

#include "TestReShadePresetService.moc"
