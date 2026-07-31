#include "Cue4Service.h"

#include "core/PakService.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcCue4, "st.cue4")

namespace st {

Cue4Service::Cue4Service(QObject *parent) : QObject(parent) {}

QString Cue4Service::gameVersion() { return QStringLiteral("GAME_UE4_26"); }

QString Cue4Service::cue4Path() {
    // available() se consulta seguido (UI + cada import); la ubicación del exe
    // no cambia en runtime.
    static QMutex mutex;
    static QString cached;
    static bool resolved = false;
    QMutexLocker lock(&mutex);
    if (resolved) return cached;
    const auto locate = []() -> QString {
        const QString env = QProcessEnvironment::systemEnvironment().value(QStringLiteral("ST_CUE4PARSE"));
        if (!env.isEmpty() && QFileInfo::exists(env)) return env;
        const QString bundled = QCoreApplication::applicationDirPath() + QStringLiteral("/tools/cue4parse.exe");
        if (QFileInfo::exists(bundled)) return bundled;
        const QString dev = QCoreApplication::applicationDirPath() + QStringLiteral("/../../tools/cue4parse.exe");
        if (QFileInfo::exists(dev)) return QFileInfo(dev).absoluteFilePath();
        return {};
    };
    cached = locate();
    resolved = true;
    return cached;
}

bool Cue4Service::available() const { return !cue4Path().isEmpty(); }

bool Cue4Service::run(const QStringList &args, QString *error, int idleTimeoutMs, QString *output) {
    const QString exe = cue4Path();
    if (exe.isEmpty()) {
        if (error) *error = QStringLiteral("cue4parse.exe no encontrado en tools/.");
        return false;
    }
    // CUE4Parse busca oo2core_9_win64.dll (Oodle) en su directorio de trabajo;
    // si no está, intenta DESCARGARLA de internet, lo que se cuelga sin red o
    // con GitHub/CDN bloqueados (China). Nunca dejamos que descargue: se apunta
    // CWD y PATH a la copia del juego, o se falla rápido con instrucciones.
    const QString oodle = PakService::oodleDir();
    if (oodle.isEmpty()) {
        if (error)
            *error = QStringLiteral(
                "No se encontró oo2core_9_win64.dll (Oodle). CUE4Parse la necesita y "
                "se toma de la instalación de Stellar Blade (SB/Binaries/Win64). "
                "Elegí la carpeta del juego en Ajustes, o copiá la DLL a tools/ junto "
                "al exe, o definí STELLAR_OODLE_DIR con la carpeta que la contiene.");
        return false;
    }
    const QString dll = QStringLiteral("oo2core_9_win64.dll");
    const QString beside = QFileInfo(exe).absolutePath() + QLatin1Char('/') + dll;
    if (!QFileInfo::exists(beside))
        QFile::copy(oodle + QLatin1Char('/') + dll, beside); // best effort
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    QProcessEnvironment envp = QProcessEnvironment::systemEnvironment();
    envp.insert(QStringLiteral("PATH"),
                QDir::toNativeSeparators(oodle) + QLatin1Char(';')
                    + envp.value(QStringLiteral("PATH")));
    p.setProcessEnvironment(envp);
    p.setWorkingDirectory(oodle);
    qCInfo(lcCue4) << "run:" << args << "oodle:" << oodle;
    // Medir siempre: los reportes de "se cuelga" salen de corridas sin mappings
    // o con un stage viejo dentro de Paks, y sin este número no se distinguen.
    QElapsedTimer timer;
    timer.start();
    p.start(exe, args);
    if (!p.waitForStarted(10000)) {
        if (error) *error = QStringLiteral("No se pudo iniciar cue4parse.exe");
        return false;
    }
    // Leer incremental: cue4parse imprime "Exporting package N of M". Sin esto
    // la UI quedaba congelada en un solo mensaje durante toda la corrida y el
    // usuario lo reportaba como cuelgue.
    QString out;
    QElapsedTimer idle;
    idle.start();
    int lastReported = -1;
    while (p.state() != QProcess::NotRunning) {
        if (p.waitForReadyRead(200)) {
            const QString chunk = QString::fromLocal8Bit(p.readAll());
            if (!chunk.isEmpty()) {
                out += chunk;
                idle.restart();
                static const QRegularExpression re(
                    QStringLiteral("Exporting package (\\d+) of (\\d+)"));
                QRegularExpressionMatch last;
                auto mi = re.globalMatch(chunk);
                while (mi.hasNext()) last = mi.next();
                if (last.hasMatch()) {
                    const int done = last.captured(1).toInt();
                    const int total = last.captured(2).toInt();
                    if (done != lastReported) {
                        lastReported = done;
                        emit progress(tr("CUE4Parse: %1 de %2 paquetes...").arg(done).arg(total));
                    }
                }
            }
        } else if (idle.elapsed() > idleTimeoutMs) {
            p.kill();
            p.waitForFinished(2000);
            out += QString::fromLocal8Bit(p.readAll());
            qCWarning(lcCue4) << "cue4 idle timeout after" << timer.elapsed() << "ms";
            if (error) *error = QStringLiteral("cue4parse dejó de responder");
            if (output) *output = out;
            return false;
        }
    }
    out += QString::fromLocal8Bit(p.readAll());
    if (output) *output = out;
    // cue4parse puede terminar con excepción de escritura por race y aún así
    // haber producido los JSON; no tratamos exitCode!=0 como fatal si hay salida.
    if (out.contains(QLatin1String("Could not load standard asset"))) {
        if (error) *error = QStringLiteral("cue4parse no pudo resolver tipos (falta global del juego, versión o mappings).");
        return false;
    }
    qCInfo(lcCue4) << "cue4 exit" << p.exitCode() << "in" << timer.elapsed() << "ms"
                   << "mappings:" << (args.contains(QStringLiteral("-m")) ? "yes" : "NO");
    return true;
}

QStringList Cue4Service::listPackages(const QString &inputDir, const QString &mappings,
                                      const QString &pattern, QString *error) {
    return listPackages(inputDir, mappings, QStringList{pattern}, error);
}

QStringList Cue4Service::listPackages(const QString &inputDir, const QString &mappings,
                                      const QStringList &patterns, QString *error) {
    QStringList args{QStringLiteral("-i"), QDir::toNativeSeparators(inputDir),
                     QStringLiteral("-g"), gameVersion(),
                     QStringLiteral("-l")};
    for (const QString &pat : patterns)
        args << QStringLiteral("-p") << pat;
    if (!mappings.isEmpty())
        args << QStringLiteral("-m") << QDir::toNativeSeparators(mappings);
    QString out;
    run(args, error, 120000, &out);
    QStringList packages;
    const auto lines = out.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QString s = line.trimmed();
        if (s.endsWith(QLatin1String(".uasset"), Qt::CaseInsensitive) && !s.contains(QLatin1Char(' ')))
            packages << s;
    }
    packages.removeDuplicates();
    return packages;
}

QString Cue4Service::patternForTable(const QString &tableName) {
    return QStringLiteral("*/") + tableName + QStringLiteral(".uasset");
}

QMap<QString, QString> Cue4Service::exportTables(const QString &inputDir,
                                                 const QString &outDir,
                                                 const QString &mappings,
                                                 const QString &packageWildcard,
                                                 QString *error) {
    return exportPackages(inputDir, outDir, mappings, {packageWildcard}, error);
}

QMap<QString, QString> Cue4Service::exportPackages(const QString &inputDir,
                                                   const QString &outDir,
                                                   const QString &mappings,
                                                   const QStringList &patterns,
                                                   QString *error) {
    QDir(outDir).removeRecursively();
    QDir().mkpath(outDir);

    QStringList args{QStringLiteral("-i"), QDir::toNativeSeparators(inputDir),
                     QStringLiteral("-g"), gameVersion(),
                     QStringLiteral("-f"), QStringLiteral("json"),
                     QStringLiteral("-o"), QDir::toNativeSeparators(outDir),
                     QStringLiteral("-y")};
    for (const QString &pat : patterns)
        args << QStringLiteral("-p") << pat;   // -p es repetible
    if (!mappings.isEmpty())
        args << QStringLiteral("-m") << QDir::toNativeSeparators(mappings);

    emit progress(tr("Leyendo tablas con CUE4Parse..."));
    QString runErr, runOut;
    run(args, &runErr, 120000, &runOut); // no fatal salvo "could not load"

    QMap<QString, QString> result;
    QDirIterator it(outDir, {QStringLiteral("*.json")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        result.insert(it.fileInfo().completeBaseName(), it.filePath());
    }
    if (result.isEmpty() && error) {
        if (!runErr.isEmpty())
            *error = runErr;
        else
            *error = tr("CUE4Parse no exportó ninguna tabla (el mod no contiene DataTables "
                        "'%1', o el mapping/versión están desactualizados tras un parche del juego). %2")
                         .arg(patterns.join(QStringLiteral(", ")), runOut.trimmed().right(500));
    }
    return result;
}

} // namespace st
