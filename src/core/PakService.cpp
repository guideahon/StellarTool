#include "PakService.h"

#include "core/GamePaths.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
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

QString PakService::retocPath() {
    const QString env = QProcessEnvironment::systemEnvironment().value(QStringLiteral("ST_RETOC"));
    if (!env.isEmpty() && QFileInfo::exists(env)) return env;
    const QString bundled = QCoreApplication::applicationDirPath() + QStringLiteral("/tools/retoc.exe");
    if (QFileInfo::exists(bundled)) return bundled;
    const QString dev = QCoreApplication::applicationDirPath() + QStringLiteral("/../../tools/retoc.exe");
    if (QFileInfo::exists(dev)) return QFileInfo(dev).absoluteFilePath();
    return {};
}

QString PakService::cnsRetocPath() {
    const QString env = QProcessEnvironment::systemEnvironment()
                            .value(QStringLiteral("ST_CNS_RETOC"));
    if (!env.isEmpty() && QFileInfo::exists(env)) return env;
    const QStringList candidates{
        QCoreApplication::applicationDirPath() + QStringLiteral("/tools/CNSRepacker/retoc.exe"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/../../tools/CNSRepacker/retoc.exe"),
        GamePaths::gameRoot() + QStringLiteral("/CNSRepacker/tools/retoc/retoc.exe"),
        QStringLiteral("C:/Program Files (x86)/Steam/steamapps/common/StellarBlade/"
                       "CNSRepacker/tools/retoc/retoc.exe")
    };
    for (const QString &candidate : candidates)
        if (QFileInfo::exists(candidate)) return QFileInfo(candidate).absoluteFilePath();
    return {};
}

bool PakService::zenAvailable() const { return !retocPath().isEmpty(); }

QString PakService::oodleDir() {
    // Se llama en CADA corrida de retoc/cue4parse y el último recurso es un
    // barrido recursivo de la carpeta del juego (decenas de GB): sin cache eso
    // se pagaba una vez por pak. Cacheado por gameRoot (cambia si el usuario
    // reconfigura la ruta).
    static QMutex mutex;              // se llama desde worker threads
    static QString cachedRoot;
    static QString cachedDir;
    static bool cached = false;
    QMutexLocker lock(&mutex);
    const QString root = GamePaths::gameRoot();
    if (cached && cachedRoot == root) return cachedDir;

    const auto locate = [&root]() -> QString {
        const QString dll = QStringLiteral("oo2core_9_win64.dll");
        const QString env = QProcessEnvironment::systemEnvironment().value(QStringLiteral("STELLAR_OODLE_DIR"));
        if (!env.isEmpty() && QFileInfo::exists(env + QLatin1Char('/') + dll))
            return QFileInfo(env).absoluteFilePath();
        const QStringList local{
            QCoreApplication::applicationDirPath() + QStringLiteral("/tools"),
            QCoreApplication::applicationDirPath(),
            QCoreApplication::applicationDirPath() + QStringLiteral("/../../tools")};
        for (const QString &c : local)
            if (QFileInfo::exists(c + QLatin1Char('/') + dll))
                return QFileInfo(c).absoluteFilePath();
        if (root.isEmpty()) return {};
        const QStringList inGame{root + QStringLiteral("/SB/Binaries/Win64"),
                                 root + QStringLiteral("/CNSRepacker/tools/retoc")};
        for (const QString &c : inGame)
            if (QFileInfo::exists(c + QLatin1Char('/') + dll)) return c;
        QDirIterator it(root, {dll}, QDir::Files, QDirIterator::Subdirectories);
        if (it.hasNext()) {
            it.next();
            return it.fileInfo().absolutePath();
        }
        return {};
    };

    cachedDir = locate();
    cachedRoot = root;
    cached = true;
    return cachedDir;
}

bool PakService::packZen(const QString &contentDir, const QString &outUtoc, QString *error) {
    const QString retoc = retocPath();
    if (retoc.isEmpty()) {
        if (error) *error = QStringLiteral("retoc.exe no encontrado en tools/.");
        return false;
    }
    QDir().mkpath(QFileInfo(outUtoc).absolutePath());
    if (!runProcess(retoc, {QStringLiteral("to-zen"), contentDir, outUtoc,
                            QStringLiteral("--version"), QStringLiteral("UE4_26")}, error))
        return false;
    return runProcess(retoc, {QStringLiteral("verify"), outUtoc}, error);
}

bool PakService::runProcess(const QString &exe, const QStringList &args, QString *error, int timeoutMs) {
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    // retoc/cue4parse necesitan oo2core (Oodle). retoc la busca JUNTO A SU
    // PROPIO EXE (no usa PATH) y si no está intenta DESCARGARLA de GitHub,
    // lo que se cuelga sin red o con GitHub bloqueado (China). Se copia la DLL
    // del juego junto al exe en runtime (no se redistribuye: package.bat la
    // excluye del zip) y además se inyecta PATH + CWD como respaldo.
    const QString baseName = QFileInfo(exe).completeBaseName().toLower();
    if (baseName == QLatin1String("retoc") || baseName == QLatin1String("cue4parse")) {
        const QString oodle = oodleDir();
        if (!oodle.isEmpty()) {
            const QString dll = QStringLiteral("oo2core_9_win64.dll");
            const QString beside = QFileInfo(exe).absolutePath() + QLatin1Char('/') + dll;
            if (!QFileInfo::exists(beside))
                QFile::copy(oodle + QLatin1Char('/') + dll, beside); // best effort
            QProcessEnvironment envp = QProcessEnvironment::systemEnvironment();
            envp.insert(QStringLiteral("PATH"),
                        QDir::toNativeSeparators(oodle) + QLatin1Char(';')
                            + envp.value(QStringLiteral("PATH")));
            p.setProcessEnvironment(envp);
            p.setWorkingDirectory(oodle);
        }
    }
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

bool PakService::createZip(const QString &srcDir, const QString &outZip, QString *error) {
    QDir().mkpath(QFileInfo(outZip).absolutePath());
    QFile::remove(outZip);
    // bsdtar de Windows detecta formato zip por la extensión con -a. Se pasan
    // las entradas de primer nivel por nombre para evitar el prefijo "./".
    QStringList args{QStringLiteral("-a"), QStringLiteral("-c"),
                     QStringLiteral("-f"), QDir::toNativeSeparators(outZip),
                     QStringLiteral("-C"), QDir::toNativeSeparators(srcDir)};
    const QStringList entries = QDir(srcDir).entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    if (entries.isEmpty()) {
        if (error) *error = QStringLiteral("Nada que comprimir en %1").arg(srcDir);
        return false;
    }
    args << entries;
    return runProcess(QStringLiteral("tar"), args, error);
}

int PakService::toLegacy(const QString &utocPath, const QString &outDir, QString *error) {
    const QString retoc = retocPath();
    if (retoc.isEmpty()) {
        if (error) *error = QStringLiteral("retoc.exe no encontrado en tools/.");
        return 0;
    }
    QDir().mkpath(outDir);
    if (!runProcess(retoc, {QStringLiteral("to-legacy"),
                            QStringLiteral("--version"), QStringLiteral("UE4_26"),
                            utocPath, outDir}, error))
        return 0;
    // retoc no falla aunque no extraiga nada; contamos los .uasset producidos.
    int n = 0;
    QDirIterator it(outDir, {QStringLiteral("*.uasset")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) { it.next(); ++n; }
    return n;
}

int PakService::toLegacyMounted(const QString &modDir, const QString &outDir,
                                QString *error) {
    const QString retoc = cnsRetocPath();
    if (retoc.isEmpty()) {
        if (error) *error = QStringLiteral(
            "Falta tools/CNSRepacker/retoc.exe (fork con --mount-folder).");
        return 0;
    }
    if (!GamePaths::hasGame()) {
        if (error) *error = QStringLiteral("Falta configurar la carpeta del juego.");
        return 0;
    }
    QDir(outDir).removeRecursively();
    QDir().mkpath(outDir);
    if (!runProcess(retoc, {QStringLiteral("to-legacy"),
                            QStringLiteral("--mount-folder"),
                            QDir::toNativeSeparators(GamePaths::paksDir()),
                            QDir::toNativeSeparators(modDir),
                            QDir::toNativeSeparators(outDir),
                            QStringLiteral("--no-ver-check"),
                            QStringLiteral("--check-subfolders")}, error, 600000))
        return 0;
    int n = 0;
    QDirIterator it(outDir, {QStringLiteral("*.uasset")}, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) { it.next(); ++n; }
    return n;
}

int PakService::toLegacyFiltered(const QString &inputDir, const QString &filter,
                                 const QString &outDir, QString *error) {
    const QString retoc = retocPath();
    if (retoc.isEmpty()) {
        if (error) *error = QStringLiteral("retoc.exe no encontrado en tools/.");
        return 0;
    }
    QDir().mkpath(outDir);
    if (!runProcess(retoc, {QStringLiteral("to-legacy"),
                            QStringLiteral("-f"), filter,
                            QStringLiteral("--version"), QStringLiteral("UE4_26"),
                            inputDir, outDir}, error))
        return 0;
    int n = 0;
    QDirIterator it(outDir, {QStringLiteral("*.uasset")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) { it.next(); ++n; }
    return n;
}

QStringList PakService::listZenAssets(const QString &utocPath) {
    const QString retoc = retocPath();
    if (retoc.isEmpty()) return {};
    const QString tmp = QDir::tempPath() + QStringLiteral("/st_zenlist_")
        + QFileInfo(utocPath).completeBaseName();
    QDir(tmp).removeRecursively();
    QDir().mkpath(tmp);
    QStringList names;
    if (runProcess(retoc, {QStringLiteral("unpack"), utocPath, tmp}, nullptr, 120000)) {
        QDirIterator it(tmp, {QStringLiteral("*.uasset")}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            names << it.fileInfo().completeBaseName();
        }
    }
    QDir(tmp).removeRecursively();
    names.sort();
    return names;
}

QString PakService::compatGlobalDir(QString *error) {
    const QStringList globals = GamePaths::globalContainerFiles();
    if (globals.isEmpty()) {
        if (error) *error = QStringLiteral("No hay global.utoc del juego (falta la ruta del juego).");
        return {};
    }
    QString srcGlobal;
    for (const QString &g : globals)
        if (g.endsWith(QLatin1String(".utoc"), Qt::CaseInsensitive)) srcGlobal = g;
    if (srcGlobal.isEmpty()) {
        if (error) *error = QStringLiteral("No se encontró global.utoc.");
        return {};
    }

    const QString cache = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                          + QStringLiteral("/cache/zenglobal");
    const QString outDir = cache + QStringLiteral("/global");
    const QString stampPath = cache + QStringLiteral("/source.stamp");
    const QFileInfo si(srcGlobal);
    const QString stamp = QStringLiteral("%1|%2|%3")
                              .arg(srcGlobal)
                              .arg(si.size())
                              .arg(si.lastModified().toMSecsSinceEpoch());

    // Cache válida = mismo global de origen (el juego se pudo actualizar).
    if (QFileInfo::exists(outDir + QStringLiteral("/global.utoc"))) {
        QFile sf(stampPath);
        if (sf.open(QIODevice::ReadOnly)
            && QString::fromUtf8(sf.readAll()) == stamp)
            return outDir;
    }

    const QString retoc = retocPath();
    if (retoc.isEmpty()) {
        if (error) *error = QStringLiteral("retoc.exe no está disponible.");
        return {};
    }
    QDir(cache).removeRecursively();
    // unpack-raw crea el directorio y falla si ya existe ("os error 183"),
    // así que sólo se prepara el padre.
    const QString rawDir = cache + QStringLiteral("/raw");
    QDir().mkpath(cache);
    QDir().mkpath(outDir);

    PakService svc;   // sólo para reutilizar runProcess
    if (!svc.runProcess(retoc, {QStringLiteral("unpack-raw"),
                                QDir::toNativeSeparators(srcGlobal),
                                QDir::toNativeSeparators(rawDir)}, error))
        return {};

    // El volcado raw guarda la versión del contenedor en su manifiesto: se
    // cambia ahí y pack-raw emite el mismo global en la versión de los mods.
    const QString manifestPath = rawDir + QStringLiteral("/manifest.json");
    QFile mf(manifestPath);
    if (!mf.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("unpack-raw no dejó manifest.json en %1").arg(rawDir);
        return {};
    }
    QJsonObject manifest = QJsonDocument::fromJson(mf.readAll()).object();
    mf.close();
    manifest.insert(QStringLiteral("version"), QStringLiteral("DirectoryIndex"));
    if (!mf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = QStringLiteral("No se pudo reescribir %1").arg(manifestPath);
        return {};
    }
    mf.write(QJsonDocument(manifest).toJson(QJsonDocument::Indented));
    mf.close();

    if (!svc.runProcess(retoc, {QStringLiteral("pack-raw"),
                                QDir::toNativeSeparators(rawDir),
                                QDir::toNativeSeparators(outDir + QStringLiteral("/global.utoc"))},
                        error))
        return {};
    if (!QFileInfo::exists(outDir + QStringLiteral("/global.utoc"))) {
        if (error) *error = QStringLiteral("pack-raw no generó global.utoc");
        return {};
    }
    QDir(rawDir).removeRecursively();
    QFile sf(stampPath);
    if (sf.open(QIODevice::WriteOnly)) sf.write(stamp.toUtf8());
    return outDir;
}

int PakService::toLegacyWithGlobal(const QString &utocPath, const QString &outDir, QString *error) {
    const QString retoc = retocPath();
    if (retoc.isEmpty()) return 0;
    const QString global = compatGlobalDir(error);
    if (global.isEmpty()) return 0;

    // to-legacy convierte todos los contenedores del directorio, así que el
    // stage lleva el global compat + el mod (y nada más).
    const QFileInfo mi(utocPath);
    const QString stage = outDir + QStringLiteral("/../zenstage_") + mi.completeBaseName();
    QDir(stage).removeRecursively();
    if (!QDir().mkpath(stage)) return 0;
    for (const QString &ext : {QStringLiteral("utoc"), QStringLiteral("ucas")}) {
        const QString g = global + QStringLiteral("/global.") + ext;
        if (QFileInfo::exists(g))
            QFile::copy(g, stage + QStringLiteral("/global.") + ext);
    }
    for (const QString &ext : {QStringLiteral("utoc"), QStringLiteral("ucas"), QStringLiteral("pak")}) {
        const QString f = mi.absolutePath() + QLatin1Char('/') + mi.completeBaseName()
                          + QLatin1Char('.') + ext;
        if (QFileInfo::exists(f))
            QFile::copy(f, stage + QLatin1Char('/') + QFileInfo(f).fileName());
    }

    QDir().mkpath(outDir);
    const bool ok = runProcess(retoc, {QStringLiteral("to-legacy"),
                                       QStringLiteral("--version"), QStringLiteral("UE4_26"),
                                       QDir::toNativeSeparators(stage),
                                       QDir::toNativeSeparators(outDir)}, error);
    QDir(stage).removeRecursively();
    if (!ok) return 0;
    int n = 0;
    QDirIterator it(outDir, {QStringLiteral("*.uasset")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) { it.next(); ++n; }
    return n;
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
