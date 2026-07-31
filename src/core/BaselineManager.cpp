#include "BaselineManager.h"
#include "UAssetService.h"
#include "Cue4Service.h"
#include "GamePaths.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QStandardPaths>

namespace st {

BaselineManager::BaselineManager(UAssetService *uasset, Cue4Service *cue4, QObject *parent)
    : QObject(parent), m_uasset(uasset), m_cue4(cue4) {}

static QString gameStamp(const QString &gamePaksDir) {
    const QFileInfo fi(gamePaksDir + QStringLiteral("/pakchunk0-WindowsNoEditor.utoc"));
    if (!fi.exists()) return {};
    return QStringLiteral("%1|%2").arg(fi.size()).arg(fi.lastModified().toSecsSinceEpoch());
}

bool BaselineManager::isStale(const QString &gamePaksDir) const {
    if (!hasBaseline()) return false;
    QFile f(baselineDir() + QStringLiteral("/stamp.txt"));
    if (!f.open(QIODevice::ReadOnly)) return false; // baseline importada a mano: no juzgar
    const QString saved = QString::fromUtf8(f.readAll()).trimmed();
    const QString current = gameStamp(gamePaksDir);
    return !current.isEmpty() && !saved.isEmpty() && saved != current;
}

bool BaselineManager::buildFromGame(const QString &gamePaksDir, const QString &mappings,
                                    QString *error, int *imported) {
    if (imported) *imported = 0;
    if (!m_cue4 || !m_cue4->available()) {
        if (error) *error = tr("cue4parse.exe no disponible");
        return false;
    }
    if (mappings.isEmpty()) {
        if (error) *error = tr("Faltan los mappings (.usmap): sin ellos CUE4Parse no resuelve "
                               "tipos y leer las tablas vanilla tarda decenas de minutos. "
                               "Conseguilos en Ajustes → Mappings.");
        return false;
    }
    // Un stage de un import anterior dentro de Paks haría que CUE4Parse relea
    // el juego entero una segunda vez (el stage tiene el global hardlinkeado).
    GamePaths::cleanCue4Stages();
    QDir().mkpath(baselineDir());
    // Invalida el cache de tablas UAssetGUI (rep. legacy) al reconstruir.
    QDir(baselineDir() + QStringLiteral("/uasset_json")).removeRecursively();
    // Reconstruir = desde cero: si no, ensureTables saltearía todo lo cacheado
    // y una baseline vieja/parcial nunca se corregiría.
    const QDir bdir(baselineDir());
    for (const QString &f : bdir.entryList({QStringLiteral("*.json")}, QDir::Files))
        QFile::remove(bdir.filePath(f));
    QFile::remove(bdir.filePath(QStringLiteral("stamp.txt")));
    QFile::remove(bdir.filePath(QStringLiteral("absent.txt")));
    // Listar primero (barato: monta y no escribe) y delegar en ensureTables,
    // que reintenta lo que quede afuera cuando cue4parse aborta por colisión
    // de escritura. Una sola pasada dejaba la baseline a medias sin avisar.
    emit progress(tr("Listando tablas vanilla del juego (CUE4Parse)..."));
    const QStringList packages = m_cue4->listPackages(gamePaksDir, mappings,
                                                      QStringLiteral("*Table*"), error);
    QStringList names;
    for (const QString &p : packages) names << QFileInfo(p).completeBaseName();
    names.removeDuplicates();
    int count = 0;
    if (!names.isEmpty())
        ensureTables(gamePaksDir, mappings, names, error, &count);
    if (count > 0) writeStamp(gamePaksDir);
    if (imported) *imported = count;
    if (count == 0 && error && error->isEmpty())
        *error = tr("CUE4Parse no exportó tablas del juego");
    return count > 0;
}

int BaselineManager::ingestExported(const QMap<QString, QString> &tables) const {
    int count = 0;
    for (auto it = tables.begin(); it != tables.end(); ++it) {
        QFile f(it.value());
        if (!f.open(QIODevice::ReadOnly)) continue;
        const QJsonObject doc = cue4ToDataTableDoc(f.readAll());
        f.close();
        if (!isDataTableJson(doc)) continue;
        const QString dst = baselineDir() + QLatin1Char('/') + it.key().toLower() + QStringLiteral(".json");
        QFile o(dst);
        if (!o.open(QIODevice::WriteOnly)) continue;
        o.write(QJsonDocument(doc).toJson(QJsonDocument::Compact));
        o.close();
        ++count;
    }
    return count;
}

QSet<QString> BaselineManager::readAbsent() const {
    QSet<QString> out;
    QFile f(baselineDir() + QStringLiteral("/absent.txt"));
    if (!f.open(QIODevice::ReadOnly)) return out;
    const auto lines = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'));
    for (const QString &l : lines) {
        const QString s = l.trimmed().toLower();
        if (!s.isEmpty()) out.insert(s);
    }
    return out;
}

void BaselineManager::writeAbsent(const QSet<QString> &absent) const {
    QFile f(baselineDir() + QStringLiteral("/absent.txt"));
    if (!f.open(QIODevice::WriteOnly)) return;
    QStringList names(absent.begin(), absent.end());
    names.sort();
    f.write(names.join(QLatin1Char('\n')).toUtf8());
}

void BaselineManager::writeStamp(const QString &gamePaksDir) const {
    QFile sf(baselineDir() + QStringLiteral("/stamp.txt"));
    if (sf.open(QIODevice::WriteOnly))
        sf.write(gameStamp(gamePaksDir).toUtf8());
}

bool BaselineManager::ensureTables(const QString &gamePaksDir, const QString &mappings,
                                   const QStringList &tableNames, QString *error, int *imported) {
    if (imported) *imported = 0;
    if (tableNames.isEmpty()) return true;
    if (!m_cue4 || !m_cue4->available()) {
        if (error) *error = tr("cue4parse.exe no disponible");
        return false;
    }
    if (mappings.isEmpty()) {
        if (error) *error = tr("Faltan los mappings (.usmap): sin ellos CUE4Parse no resuelve "
                               "tipos y leer las tablas vanilla tarda decenas de minutos. "
                               "Conseguilos en Ajustes → Mappings.");
        return false;
    }
    // Juego actualizado: lo cacheado ya no es vanilla de ESTA versión.
    if (isStale(gamePaksDir)) {
        const QDir d(baselineDir());
        for (const QString &f : d.entryList({QStringLiteral("*.json")}, QDir::Files))
            QFile::remove(d.filePath(f));
        // Sin borrar el stamp viejo la baseline quedaría "stale" para siempre
        // y se limpiaría en cada análisis.
        QFile::remove(d.filePath(QStringLiteral("stamp.txt")));
        // Un parche puede agregar tablas: lo "inexistente" caduca con el juego.
        QFile::remove(d.filePath(QStringLiteral("absent.txt")));
        QDir(baselineDir() + QStringLiteral("/uasset_json")).removeRecursively();
    }
    QDir().mkpath(baselineDir());

    const auto stillMissing = [this](const QStringList &names) {
        QStringList out;
        for (const QString &name : names) {
            if (name.isEmpty() || out.contains(name)) continue;
            const QString cached = baselineDir() + QLatin1Char('/') + name.toLower()
                                   + QStringLiteral(".json");
            if (!QFileInfo::exists(cached)) out << name;
        }
        return out;
    };
    QStringList missing = stillMissing(tableNames);
    // Tablas que ya se comprobó que NO existen en vanilla (tablas nuevas del
    // mod). Sin este registro cada análisis volvía a barrer el juego buscando
    // algo que nunca va a aparecer.
    QSet<QString> absent = readAbsent();
    for (int i = missing.size() - 1; i >= 0; --i)
        if (absent.contains(missing.at(i).toLower())) missing.removeAt(i);
    if (missing.isEmpty()) return true;

    GamePaths::cleanCue4Stages();

    // Un listado (-l) monta el juego una vez y no escribe: descartar acá lo que
    // no existe evita que el bucle de reintento gaste pasadas completas de
    // exportación persiguiendo tablas inexistentes (eran ~30 min de espera).
    {
        emit progress(tr("Buscando %1 tabla(s) vanilla en el juego (CUE4Parse)...").arg(missing.size()));
        QStringList patterns;
        for (const QString &name : missing) patterns << Cue4Service::patternForTable(name);
        QString listErr;
        const QStringList found = m_cue4->listPackages(gamePaksDir, mappings, patterns, &listErr);
        if (!found.isEmpty()) {
            QSet<QString> present;
            for (const QString &p : found) present.insert(QFileInfo(p).completeBaseName().toLower());
            QStringList existing;
            for (const QString &name : missing) {
                if (present.contains(name.toLower())) existing << name;
                else absent.insert(name.toLower());
            }
            writeAbsent(absent);
            missing = existing;
            if (missing.isEmpty()) return true;
        }
        // Si el listado falló (found vacío) se sigue con el camino viejo: mejor
        // exportar de más que no exportar nada por un -l roto.
    }

    const QString tmp = baselineDir() + QStringLiteral("/_cue4_raw");
    int count = 0;
    // cue4parse exporta en paralelo y aborta el proceso entero si dos hilos
    // escriben el mismo JSON (un paquete que existe en dos contenedores, p. ej.
    // CharacterTable). Lo ya exportado queda en disco, así que se reintenta con
    // lo que falte: cada pasada avanza hasta que no gana nada nuevo.
    for (int pass = 0; pass < 5 && !missing.isEmpty(); ++pass) {
        emit progress(tr("Leyendo %1 tabla(s) vanilla del juego (CUE4Parse)...").arg(missing.size()));
        QStringList patterns;
        for (const QString &name : missing) patterns << Cue4Service::patternForTable(name);
        QString passErr;
        const auto tables = m_cue4->exportPackages(gamePaksDir, tmp, mappings, patterns, &passErr);
        const int got = ingestExported(tables);
        count += got;
        QDir(tmp).removeRecursively();
        if (got == 0) {                      // ninguna pasada más va a avanzar
            if (error && count == 0) *error = passErr;
            break;
        }
        missing = stillMissing(missing);
    }
    if (count > 0 && !QFileInfo::exists(baselineDir() + QStringLiteral("/stamp.txt")))
        writeStamp(gamePaksDir);
    if (imported) *imported = count;
    // Que una tabla del mod no exista en vanilla (tabla nueva) es normal: no
    // es error mientras CUE4Parse haya corrido bien.
    return true;
}

QString BaselineManager::baselineDir() const {
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
           + QStringLiteral("/baseline");
}

bool BaselineManager::hasBaseline() const {
    const QDir d(baselineDir());
    return d.exists() && !d.entryList({QStringLiteral("*.json")}, QDir::Files).isEmpty();
}

QString BaselineManager::keyFor(const QString &gamePath) const {
    // Identifica la tabla por nombre de archivo (las rutas pueden variar entre dumps).
    return QFileInfo(gamePath).completeBaseName().toLower() + QStringLiteral(".json");
}

QJsonObject BaselineManager::tableFor(const QString &gamePath) const {
    QFile f(baselineDir() + QLatin1Char('/') + keyFor(gamePath));
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

bool BaselineManager::importFromDir(const QString &dir, QString *error, int *imported) {
    if (imported) *imported = 0;
    QDir().mkpath(baselineDir());
    int count = 0;
    QDirIterator it(dir, {QStringLiteral("*.json"), QStringLiteral("*.uasset")},
                    QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        QString jsonSrc = path;
        if (path.endsWith(QLatin1String(".uasset"), Qt::CaseInsensitive)) {
            if (!m_uasset || !m_uasset->available()) continue;
            const QString tmp = baselineDir() + QStringLiteral("/_convert_tmp.json");
            QString err;
            if (!m_uasset->toJson(path, tmp, &err)) continue;
            jsonSrc = tmp;
        }
        QFile f(jsonSrc);
        if (!f.open(QIODevice::ReadOnly)) continue;
        const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
        f.close();
        if (!isDataTableJson(root)) continue;
        const QString dst = baselineDir() + QLatin1Char('/')
            + QFileInfo(path).completeBaseName().toLower() + QStringLiteral(".json");
        QFile::remove(dst);
        if (QFile::copy(jsonSrc, dst)) ++count;
    }
    QFile::remove(baselineDir() + QStringLiteral("/_convert_tmp.json"));
    if (imported) *imported = count;
    if (count == 0 && error)
        *error = tr("No se encontraron JSONs de DataTable en %1").arg(dir);
    return count > 0;
}

} // namespace st
