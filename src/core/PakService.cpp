#include "PakService.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcPak, "st.pak")

namespace st {

PakService::PakService(QObject *parent) : QObject(parent) {}

QString PakService::repakPath() {
    const QString env = QProcessEnvironment::systemEnvironment().value(QStringLiteral("ST_REPAK"));
    if (!env.isEmpty() && QFileInfo::exists(env)) return env;
    const QString bundled = QCoreApplication::applicationDirPath() + QStringLiteral("/tools/repak.exe");
    if (QFileInfo::exists(bundled)) return bundled;
    // Durante desarrollo: tools/ en la raíz del repo (dos niveles arriba de build/<cfg>/)
    const QString dev = QCoreApplication::applicationDirPath() + QStringLiteral("/../../tools/repak.exe");
    if (QFileInfo::exists(dev)) return QFileInfo(dev).absoluteFilePath();
    return {};
}

bool PakService::available() const { return !repakPath().isEmpty(); }

bool PakService::runProcess(const QString &exe, const QStringList &args, QString *error, int timeoutMs) {
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    qCInfo(lcPak) << "run:" << exe << args;
    p.start(exe, args);
    if (!p.waitForStarted(10000)) {
        if (error) *error = QStringLiteral("No se pudo iniciar %1").arg(exe);
        return false;
    }
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        if (error) *error = QStringLiteral("Timeout ejecutando %1").arg(QFileInfo(exe).fileName());
        return false;
    }
    const QString out = QString::fromLocal8Bit(p.readAll());
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        qCWarning(lcPak) << "fail:" << exe << p.exitCode() << out;
        if (error) *error = QStringLiteral("%1 falló (código %2): %3")
                                .arg(QFileInfo(exe).fileName()).arg(p.exitCode()).arg(out.right(500));
        return false;
    }
    return true;
}

bool PakService::unpack(const QString &pakPath, const QString &outDir, QString *error) {
    const QString repak = repakPath();
    if (repak.isEmpty()) {
        if (error) *error = QStringLiteral("repak.exe no encontrado en tools/. Ver tools/VERSIONS.md.");
        return false;
    }
    QDir().mkpath(outDir);
    return runProcess(repak, {QStringLiteral("unpack"), QStringLiteral("--output"), outDir, pakPath}, error);
}

bool PakService::pack(const QString &contentDir, const QString &outPak, QString *error) {
    const QString repak = repakPath();
    if (repak.isEmpty()) {
        if (error) *error = QStringLiteral("repak.exe no encontrado en tools/.");
        return false;
    }
    QDir().mkpath(QFileInfo(outPak).absolutePath());
    // V11 = UE4.26 (Stellar Blade). Mount point default de repak coincide con mods legacy.
    return runProcess(repak, {QStringLiteral("pack"),
                              QStringLiteral("--version"), QStringLiteral("V11"),
                              contentDir, outPak}, error);
}

bool PakService::extractZip(const QString &zipPath, const QString &outDir, QString *error) {
    QDir().mkpath(outDir);
    // tar.exe (bsdtar) viene con Windows 10+ y soporta zip.
    return runProcess(QStringLiteral("tar"),
                      {QStringLiteral("-xf"), QDir::toNativeSeparators(zipPath),
                       QStringLiteral("-C"), QDir::toNativeSeparators(outDir)},
                      error);
}

} // namespace st
