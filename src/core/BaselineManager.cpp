#include "BaselineManager.h"
#include "UAssetService.h"
#include "Cue4Service.h"

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
    QDir().mkpath(baselineDir());
    // Invalida el cache de tablas UAssetGUI (rep. legacy) al reconstruir.
    QDir(baselineDir() + QStringLiteral("/uasset_json")).removeRecursively();
    const QString tmp = baselineDir() + QStringLiteral("/_cue4_raw");
    emit progress(tr("Leyendo tablas vanilla del juego (CUE4Parse)..."));
    const auto tables = m_cue4->exportTables(gamePaksDir, tmp, mappings,
                                             QStringLiteral("*Table*"), error);
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
    QDir(tmp).removeRecursively();
    if (count > 0) {
        QFile sf(baselineDir() + QStringLiteral("/stamp.txt"));
        if (sf.open(QIODevice::WriteOnly))
            sf.write(gameStamp(gamePaksDir).toUtf8());
    }
    if (imported) *imported = count;
    if (count == 0 && error && error->isEmpty())
        *error = tr("CUE4Parse no exportó tablas del juego");
    return count > 0;
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
