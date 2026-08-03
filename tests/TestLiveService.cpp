#include "core/LiveService.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace st;

class TestLiveService : public QObject {
    Q_OBJECT
private slots:
    void parsesStatus();
    void rejectsForeignStatus();
    void rejectsGarbage();
    void buildsRequestWithinLimits();
    void disabledFovSerializesAsZero();
    void clampsOutOfRange();
    void resolvesBridgePaths();
    void installsAndUninstalls();
    void reinstallOverwrites();
    void uninstallLeavesForeignFolderAlone();
    void publishAtomicLeavesNoTempFile();
};

void TestLiveService::parsesStatus() {
    const QByteArray body =
        "api=stellar-tool-live-v1\n"
        "beat=42\n"
        "ready=1\n"
        "seq=7\n"
        "fov_prop=ManualCameraFov\n"
        "fov_base=75.00\n"
        "fov_live=110.00\n"
        "speed_base=600.00\n"
        "jump_base=420.00\n"
        "message=applied:speed,jump,fov\n";
    const LiveService::Status status = LiveService::parseStatus(body);
    QVERIFY(status.valid);
    QCOMPARE(status.beat, quint64(42));
    QVERIFY(status.ready);
    QCOMPARE(status.seq, qint64(7));
    QCOMPARE(status.fovProperty, QStringLiteral("ManualCameraFov"));
    QCOMPARE(status.fovBase, 75.0);
    QCOMPARE(status.fovLive, 110.0);
    QCOMPARE(status.speedBase, 600.0);
    QCOMPARE(status.jumpBase, 420.0);
    QCOMPARE(status.message, QStringLiteral("applied:speed,jump,fov"));
}

void TestLiveService::rejectsForeignStatus() {
    // Un heartbeat de otra herramienta en la misma carpeta no debe leerse como
    // nuestro: sin el marcador de protocolo, invalido.
    const QByteArray body = "beat=1\nready=1\nspawnapi=native-live-add-beta-v1\n";
    QVERIFY(!LiveService::parseStatus(body).valid);
}

void TestLiveService::rejectsGarbage() {
    QVERIFY(!LiveService::parseStatus(QByteArray()).valid);
    QVERIFY(!LiveService::parseStatus(QByteArray("\0\0binario", 9)).valid);
}

void TestLiveService::buildsRequestWithinLimits() {
    const QByteArray body = LiveService::buildRequest(3, 90.0, 1.5, 2.0);
    QVERIFY(body.contains("api=stellar-tool-live-v1"));
    QVERIFY(body.contains("seq=3"));
    QVERIFY(body.contains("fov=90.00"));
    QVERIFY(body.contains("speed=1.500"));
    QVERIFY(body.contains("jump=2.000"));
    QVERIFY(body.endsWith('\n'));
}

void TestLiveService::disabledFovSerializesAsZero() {
    // fov<=0 es el "no tocar el FOV" que espera el bridge.
    QVERIFY(LiveService::buildRequest(1, 0.0, 1.0, 1.0).contains("fov=0.00"));
    QVERIFY(LiveService::buildRequest(1, -5.0, 1.0, 1.0).contains("fov=0.00"));
}

void TestLiveService::clampsOutOfRange() {
    QCOMPARE(LiveService::clampFov(500.0), LiveService::kFovMax);
    QCOMPARE(LiveService::clampFov(1.0), LiveService::kFovMin);
    QCOMPARE(LiveService::clampFov(0.0), 0.0);
    QCOMPARE(LiveService::clampMultiplier(0.0), LiveService::kMultMin);
    QCOMPARE(LiveService::clampMultiplier(9999.0), LiveService::kMultMax);
    // Un request armado con basura tampoco puede escaparse de los limites.
    const QByteArray body = LiveService::buildRequest(1, 9999.0, -3.0, 9999.0);
    QVERIFY(body.contains("fov=170.00"));
    QVERIFY(body.contains("speed=0.100"));
    QVERIFY(body.contains("jump=10.000"));
}

void TestLiveService::resolvesBridgePaths() {
    const QString root = QStringLiteral("C:/Games/StellarBlade");
    QCOMPARE(LiveService::ue4ssDirFor(root),
             QStringLiteral("C:/Games/StellarBlade/SB/Binaries/Win64/ue4ss"));
    QCOMPARE(LiveService::bridgeDirFor(root),
             QStringLiteral("C:/Games/StellarBlade/SB/Binaries/Win64/ue4ss/Mods/StellarToolLive"));
    QVERIFY(LiveService::bridgeDirFor(QString()).isEmpty());
    QVERIFY(LiveService::ue4ssDirFor(QString()).isEmpty());
}

void TestLiveService::installsAndUninstalls() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString bridge = LiveService::bridgeDirFor(tmp.path());
    QVERIFY(!LiveService::isInstalledAt(bridge));

    QString error;
    QVERIFY2(LiveService::installTo(bridge, &error), qPrintable(error));
    QVERIFY(LiveService::isInstalledAt(bridge));
    QVERIFY(QFile::exists(bridge + "/Scripts/main.lua"));
    QVERIFY(QFile::exists(bridge + "/enabled.txt"));
    QVERIFY(QFile::exists(bridge + "/README.txt"));

    // El script instalado tiene que ser el bridge real, no un archivo vacio.
    QFile lua(bridge + "/Scripts/main.lua");
    QVERIFY(lua.open(QIODevice::ReadOnly));
    const QByteArray script = lua.readAll();
    lua.close();   // Windows no borra la carpeta con el archivo abierto.
    QVERIFY(script.contains("stellar-tool-live-v1"));
    QVERIFY(script.contains("LoopAsync"));
    // Fase 1 no toca inventario ni save: si eso aparece, el scope se rompio.
    QVERIFY(!script.contains("RegisterHook"));
    QVERIFY(!script.contains("RegisterKeyBind"));

    QVERIFY2(LiveService::uninstallFrom(bridge, &error), qPrintable(error));
    QVERIFY(!QDir(bridge).exists());
    // Desinstalar dos veces no es un error.
    QVERIFY(LiveService::uninstallFrom(bridge, &error));
}

void TestLiveService::reinstallOverwrites() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString bridge = LiveService::bridgeDirFor(tmp.path());
    QString error;
    QVERIFY(LiveService::installTo(bridge, &error));

    // Simula un bridge viejo y reinstala encima.
    QVERIFY(LiveService::publishAtomic(bridge + "/Scripts/main.lua", "-- viejo\n", &error));
    QVERIFY2(LiveService::installTo(bridge, &error), qPrintable(error));

    QFile lua(bridge + "/Scripts/main.lua");
    QVERIFY(lua.open(QIODevice::ReadOnly));
    QVERIFY(lua.readAll().contains("stellar-tool-live-v1"));
}

void TestLiveService::uninstallLeavesForeignFolderAlone() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString bridge = LiveService::bridgeDirFor(tmp.path());
    QVERIFY(QDir().mkpath(bridge));
    QString error;
    QVERIFY(LiveService::publishAtomic(bridge + "/otro_mod.txt", "no es nuestro\n", &error));

    // Sin nuestro script adentro, no borramos nada.
    QVERIFY(!LiveService::uninstallFrom(bridge, &error));
    QVERIFY(QFile::exists(bridge + "/otro_mod.txt"));
}

void TestLiveService::publishAtomicLeavesNoTempFile() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.path() + "/sub/live_request.txt";
    QString error;
    QVERIFY2(LiveService::publishAtomic(path, "seq=1\n", &error), qPrintable(error));
    QVERIFY(QFile::exists(path));
    QVERIFY(!QFile::exists(path + ".tmp"));

    // Reescribir pisa el contenido y tampoco deja el .tmp.
    QVERIFY(LiveService::publishAtomic(path, "seq=2\n", &error));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray("seq=2\n"));
    QVERIFY(!QFile::exists(path + ".tmp"));
}

void runTestLiveService(int &failures, int argc, char **argv) {
    TestLiveService test;
    failures += QTest::qExec(&test, argc, argv);
}

#include "TestLiveService.moc"
