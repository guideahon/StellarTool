#include "UAssetService.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>
#include <QStandardPaths>
#include <QDateTime>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcUasset, "st.uasset")

namespace {
// Escribe un log de diagnóstico de una corrida de UAssetGUI (exe, args, código
// de salida y salida de consola) y devuelve su ruta. UAssetGUI es una app GUI:
// a veces sale con código 0 sin escribir el archivo ni imprimir nada, así que
// dejamos rastro en disco para poder diagnosticar reportes de usuarios.
QString writeUassetDiag(const QStringList &args, int exitCode, const QString &out) {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                        + QStringLiteral("/logs");
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/uassetgui_")
        + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz"))
        + QStringLiteral(".log");
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return {};
    QTextStream ts(&f);
    ts << "UAssetGUI diagnostic\n"
       << "when: " << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n"
       << "args: " << args.join(QLatin1Char(' ')) << "\n"
       << "exitCode: " << exitCode << "\n"
       << "----- output -----\n"
       << (out.isEmpty() ? QStringLiteral("(no console output captured)") : out) << "\n";
    f.close();
    return path;
}
}

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
    // Override del usuario (UI): usmap actualizado tras un parche del juego,
    // sin necesidad de reemplazar el bundled ni una nueva release.
    const QString custom = customUsmapPath();
    if (!custom.isEmpty() && QFileInfo::exists(custom)) return custom;
    const QString bundled = QCoreApplication::applicationDirPath() + QStringLiteral("/tools/StellarBlade.usmap");
    if (QFileInfo::exists(bundled)) return bundled;
    const QString dev = QCoreApplication::applicationDirPath() + QStringLiteral("/../../tools/StellarBlade.usmap");
    if (QFileInfo::exists(dev)) return QFileInfo(dev).absoluteFilePath();
    return {};
}

QString UAssetService::customUsmapPath() {
    QSettings s;
    return s.value(QStringLiteral("usmapOverride")).toString();
}

void UAssetService::setCustomUsmapPath(const QString &path) {
    QSettings s;
    if (path.isEmpty())
        s.remove(QStringLiteral("usmapOverride"));
    else
        s.setValue(QStringLiteral("usmapOverride"), path);
    // Invalidar el mapping ya copiado a %APPDATA% para que se regenere
    // desde el nuevo usmap en el próximo fromjson.
    const QString dst = QDir::homePath()
        + QStringLiteral("/AppData/Roaming/UAssetGUI/Mappings/")
        + mappingName() + QStringLiteral(".usmap");
    QFile::remove(dst);
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

bool UAssetService::run(const QStringList &args, QString *error, QString *output) {
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
    if (output) *output = out;
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        qCWarning(lcUasset) << "fail:" << p.exitCode() << out;
        const QString log = writeUassetDiag(args, p.exitCode(), out);
        if (error) *error = QStringLiteral("UAssetGUI falló (código %1): %2\nLog: %3")
                                .arg(p.exitCode()).arg(out.right(500), log);
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
    QString out;
    if (!run(args, error, &out))
        return false;
    if (!QFileInfo::exists(jsonPath)) {
        const QString log = writeUassetDiag(args, 0, out);
        if (error) *error = QStringLiteral("UAssetGUI no produjo el JSON esperado "
                                           "(mapping/versión desactualizados tras parche del juego). %1\nLog: %2")
                                .arg(out.trimmed().right(500), log);
        return false;
    }
    return true;
}

bool UAssetService::fromJson(const QString &jsonPath, const QString &uassetPath, QString *error) {
    QDir().mkpath(QFileInfo(uassetPath).absolutePath());
    QStringList args{QStringLiteral("fromjson"), QDir::toNativeSeparators(jsonPath),
                     QDir::toNativeSeparators(uassetPath)};
    // Pasar la RUTA del usmap, no el nombre. UAssetGUI resuelve el nombre
    // escaneando %LOCALAPPDATA%/UAssetGUI/Mappings; si el usmap no está ahí
    // (o está en otra carpeta), el nombre no matchea, las mappings quedan en
    // null, la DataTable no se puede reserializar y no se escribe el .uasset
    // ("no produjo el uasset esperado"). Con la ruta, UAssetGUI la carga
    // directo, sin depender de esa carpeta. (tojson ya usa la ruta.)
    if (!usmapPath().isEmpty())
        args << QDir::toNativeSeparators(usmapPath());
    QString out;
    if (!run(args, error, &out))
        return false;
    if (!QFileInfo::exists(uassetPath)) {
        const QString log = writeUassetDiag(args, 0, out);
        // Preservar el JSON de entrada (temporal, se limpia luego) junto al log
        // para poder inspeccionar la conversión Zen→UAssetGUI que falló.
        if (!log.isEmpty())
            QFile::copy(jsonPath, log + QStringLiteral(".input.json"));
        if (error) *error = QStringLiteral("UAssetGUI no produjo el uasset esperado "
                                           "(mapping/versión desactualizados tras parche del juego). %1\nLog: %2")
                                .arg(out.trimmed().right(500), log);
        return false;
    }
    return true;
}

} // namespace st
