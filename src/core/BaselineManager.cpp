#include "BaselineManager.h"
#include "UAssetService.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QStandardPaths>

namespace st {

BaselineManager::BaselineManager(UAssetService *uasset, QObject *parent)
    : QObject(parent), m_uasset(uasset) {}

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
