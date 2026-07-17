#include "UAssetService.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcUasset, "st.uasset")

namespace st {

UAssetService::UAssetService(QObject *parent) : QObject(parent) {}

QString UAssetService::engineVersion() { return QStringLiteral("VER_UE4_26"); }

QString UAssetService::uassetGuiPath() {
    const QString env = QProcessEnvironment::systemEnvironment().value(QStringLiteral("ST_UASSETGUI"));
    if (!env.isEmpty() && QFileInfo::exists(env)) return env;
    const QString bundled = QCoreApplication::applicationDirPath() + QStringLiteral("/tools/UAssetGUI.exe");
    if (QFileInfo::exists(bundled)) return bundled;
    const QString dev = QCoreApplication::applicationDirPath() + QStringLiteral("/../../tools/UAssetGUI.exe");
    if (QFileInfo::exists(dev)) return QFileInfo(dev).absoluteFilePath();
    return {};
}

bool UAssetService::available() const { return !uassetGuiPath().isEmpty(); }

QString UAssetService::mappingName() { return QStringLiteral("StellarBlade"); }

QString UAssetService::usmapPath() {
    const QString env = QProcessEnvironment::systemEnvironment().value(QStringLiteral("ST_USMAP"));
    if (!env.isEmpty() && QFileInfo::exists(env)) return env;
    const QString bundled = QCoreApplication::applicationDirPath() + QStringLiteral("/tools/StellarBlade.usmap");
    if (QFileInfo::exists(bundled)) return bundled;
    const QString dev = QCoreApplication::applicationDirPath() + QStringLiteral("/../../tools/StellarBlade.usmap");
    if (QFileInfo::exists(dev)) return QFileInfo(dev).absoluteFilePath();
    return {};
}

void UAssetService::ensureMappingInstalled() {
    const QString usmap = usmapPath();
    if (usmap.isEmpty()) return;
    const QString dir = QDir::homePath() + QStringLiteral("/AppData/Roaming/UAssetGUI/Mappings");
    const QString dst = dir + QLatin1Char('/') + mappingName() + QStringLiteral(".usmap");
    if (QFileInfo::exists(dst)) return;
    QDir().mkpath(dir);
    QFile::copy(usmap, dst);
}

bool UAssetService::run(const QStringList &args, QString *error) {
    const QString exe = uassetGuiPath();
    if (exe.isEmpty()) {
        if (error) *error = QStringLiteral("UAssetGUI.exe no encontrado en tools/. Ver tools/VERSIONS.md.");
        return false;
    }
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    qCInfo(lcUasset) << "run:" << args;
    p.start(exe, args);
    if (!p.waitForStarted(10000)) {
        if (error) *error = QStringLiteral("No se pudo iniciar UAssetGUI.exe");
        return false;
    }
    if (!p.waitForFinished(60000)) {
        p.kill();
        if (error) *error = QStringLiteral("Timeout en UAssetGUI (%1)").arg(args.value(0));
        return false;
    }
    const QString out = QString::fromLocal8Bit(p.readAll());
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        qCWarning(lcUasset) << "fail:" << p.exitCode() << out;
        if (error) *error = QStringLiteral("UAssetGUI falló (código %1): %2").arg(p.exitCode()).arg(out.right(500));
        return false;
    }
    return true;
}

bool UAssetService::toJson(const QString &uassetPath, const QString &jsonPath, QString *error) {
    QDir().mkpath(QFileInfo(jsonPath).absolutePath());
    QStringList args{QStringLiteral("tojson"), QDir::toNativeSeparators(uassetPath),
                     QDir::toNativeSeparators(jsonPath), engineVersion()};
    if (!usmapPath().isEmpty())
        args << QDir::toNativeSeparators(usmapPath());
    if (!run(args, error))
        return false;
    if (!QFileInfo::exists(jsonPath)) {
        if (error) *error = QStringLiteral("UAssetGUI no produjo el JSON esperado");
        return false;
    }
    return true;
}

bool UAssetService::fromJson(const QString &jsonPath, const QString &uassetPath, QString *error) {
    QDir().mkpath(QFileInfo(uassetPath).absolutePath());
    QStringList args{QStringLiteral("fromjson"), QDir::toNativeSeparators(jsonPath),
                     QDir::toNativeSeparators(uassetPath)};
    if (!usmapPath().isEmpty()) {
        ensureMappingInstalled();
        args << mappingName();
    }
    if (!run(args, error))
        return false;
    if (!QFileInfo::exists(uassetPath)) {
        if (error) *error = QStringLiteral("UAssetGUI no produjo el uasset esperado");
        return false;
    }
    return true;
}

} // namespace st
