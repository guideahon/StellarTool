#include "Cue4Service.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcCue4, "st.cue4")

namespace st {

Cue4Service::Cue4Service(QObject *parent) : QObject(parent) {}

QString Cue4Service::gameVersion() { return QStringLiteral("GAME_UE4_26"); }

QString Cue4Service::cue4Path() {
    const QString env = QProcessEnvironment::systemEnvironment().value(QStringLiteral("ST_CUE4PARSE"));
    if (!env.isEmpty() && QFileInfo::exists(env)) return env;
    const QString bundled = QCoreApplication::applicationDirPath() + QStringLiteral("/tools/cue4parse.exe");
    if (QFileInfo::exists(bundled)) return bundled;
    const QString dev = QCoreApplication::applicationDirPath() + QStringLiteral("/../../tools/cue4parse.exe");
    if (QFileInfo::exists(dev)) return QFileInfo(dev).absoluteFilePath();
    return {};
}

bool Cue4Service::available() const { return !cue4Path().isEmpty(); }

bool Cue4Service::run(const QStringList &args, QString *error, int timeoutMs) {
    const QString exe = cue4Path();
    if (exe.isEmpty()) {
        if (error) *error = QStringLiteral("cue4parse.exe no encontrado en tools/.");
        return false;
    }
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    qCInfo(lcCue4) << "run:" << args;
    p.start(exe, args);
    if (!p.waitForStarted(10000)) {
        if (error) *error = QStringLiteral("No se pudo iniciar cue4parse.exe");
        return false;
    }
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        if (error) *error = QStringLiteral("Timeout en cue4parse");
        return false;
    }
    const QString out = QString::fromLocal8Bit(p.readAll());
    // cue4parse puede terminar con excepción de escritura por race y aún así
    // haber producido los JSON; no tratamos exitCode!=0 como fatal si hay salida.
    if (out.contains(QLatin1String("Could not load standard asset"))) {
        if (error) *error = QStringLiteral("cue4parse no pudo resolver tipos (falta global del juego, versión o mappings).");
        return false;
    }
    qCInfo(lcCue4) << "cue4 exit" << p.exitCode();
    return true;
}

QMap<QString, QString> Cue4Service::exportTables(const QString &inputDir,
                                                 const QString &outDir,
                                                 const QString &mappings,
                                                 const QString &packageWildcard,
                                                 QString *error) {
    QDir(outDir).removeRecursively();
    QDir().mkpath(outDir);

    QStringList args{QStringLiteral("-i"), QDir::toNativeSeparators(inputDir),
                     QStringLiteral("-g"), gameVersion(),
                     QStringLiteral("-p"), packageWildcard,
                     QStringLiteral("-f"), QStringLiteral("json"),
                     QStringLiteral("-o"), QDir::toNativeSeparators(outDir),
                     QStringLiteral("-y")};
    if (!mappings.isEmpty())
        args << QStringLiteral("-m") << QDir::toNativeSeparators(mappings);

    emit progress(tr("Leyendo tablas con CUE4Parse..."));
    QString runErr;
    run(args, &runErr, 600000); // no fatal salvo "could not load"

    QMap<QString, QString> result;
    QDirIterator it(outDir, {QStringLiteral("*.json")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        result.insert(it.fileInfo().completeBaseName(), it.filePath());
    }
    if (result.isEmpty() && error)
        *error = runErr.isEmpty() ? tr("CUE4Parse no exportó ninguna tabla") : runErr;
    return result;
}

} // namespace st
