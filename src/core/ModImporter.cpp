#include "ModImporter.h"
#include "PakService.h"
#include "UAssetService.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcImport, "st.import")

namespace st {

ModImporter::ModImporter(PakService *pak, UAssetService *uasset, QObject *parent)
    : QObject(parent), m_pak(pak), m_uasset(uasset) {}

static QStringList findPaks(const QString &dir) {
    QStringList out;
    QDirIterator it(dir, {QStringLiteral("*.pak")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) out << it.next();
    return out;
}

bool ModImporter::stageSource(const QString &sourcePath, const QString &modWorkDir,
                              QString *contentDir, QString *error) {
    const QFileInfo fi(sourcePath);
    const QString extractDir = modWorkDir + QStringLiteral("/content");
    QDir().mkpath(extractDir);

    if (fi.isDir()) {
        // Carpeta: puede contener paks o assets sueltos.
        const QStringList paks = findPaks(sourcePath);
        if (!paks.isEmpty()) {
            for (const QString &pak : paks) {
                emit progress(tr("Desempaquetando %1...").arg(QFileInfo(pak).fileName()));
                if (!m_pak->unpack(pak, extractDir, error)) return false;
            }
        } else {
            emit progress(tr("Copiando carpeta..."));
            QDirIterator it(sourcePath, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                const QString src = it.next();
                const QString rel = QDir(sourcePath).relativeFilePath(src);
                const QString dst = extractDir + QLatin1Char('/') + rel;
                QDir().mkpath(QFileInfo(dst).absolutePath());
                if (!QFile::copy(src, dst)) {
                    if (error) *error = tr("No se pudo copiar %1").arg(rel);
                    return false;
                }
            }
        }
    } else if (fi.suffix().compare(QLatin1String("pak"), Qt::CaseInsensitive) == 0) {
        emit progress(tr("Desempaquetando %1...").arg(fi.fileName()));
        if (!m_pak->unpack(sourcePath, extractDir, error)) return false;
    } else if (fi.suffix().compare(QLatin1String("zip"), Qt::CaseInsensitive) == 0) {
        const QString zipDir = modWorkDir + QStringLiteral("/zip");
        emit progress(tr("Extrayendo zip..."));
        if (!m_pak->extractZip(sourcePath, zipDir, error)) return false;
        const QStringList paks = findPaks(zipDir);
        if (paks.isEmpty()) {
            if (error) *error = tr("El zip no contiene ningún .pak");
            return false;
        }
        for (const QString &pak : paks) {
            emit progress(tr("Desempaquetando %1...").arg(QFileInfo(pak).fileName()));
            if (!m_pak->unpack(pak, extractDir, error)) return false;
        }
    } else {
        if (error) *error = tr("Formato no soportado: %1 (se acepta .pak, .zip o carpeta)").arg(fi.suffix());
        return false;
    }
    *contentDir = extractDir;
    return true;
}

void ModImporter::scanAssets(ModPackage &pkg) {
    const QString jsonDir = pkg.extractDir + QStringLiteral("/../json");
    QDir().mkpath(jsonDir);
    QDirIterator it(pkg.extractDir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QFileInfo fi(path);
        const QString suffix = fi.suffix().toLower();
        // .uexp/.ubulk son compañeros del .uasset; se manejan junto a él.
        if (suffix == QLatin1String("uexp") || suffix == QLatin1String("ubulk"))
            continue;
        ModAsset asset;
        asset.gamePath = QDir(pkg.extractDir).relativeFilePath(path);
        asset.localPath = path;
        if (suffix == QLatin1String("uasset")) {
            const QString jsonPath = jsonDir + QLatin1Char('/')
                + QString(asset.gamePath).replace(QLatin1Char('/'), QLatin1Char('_')) + QStringLiteral(".json");
            QString err;
            emit progress(tr("Analizando %1...").arg(fi.fileName()));
            if (m_uasset->toJson(path, jsonPath, &err)) {
                QFile f(jsonPath);
                QJsonObject root;
                if (f.open(QIODevice::ReadOnly))
                    root = QJsonDocument::fromJson(f.readAll()).object();
                if (isDataTableJson(root)) {
                    asset.kind = ModAsset::DataTable;
                    asset.jsonPath = jsonPath;
                } else {
                    asset.kind = ModAsset::Other; // uasset válido pero no DataTable
                }
            } else {
                asset.kind = ModAsset::Unreadable;
                asset.error = err;
                qCWarning(lcImport) << "asset no analizable:" << asset.gamePath << err;
            }
        } else {
            asset.kind = ModAsset::Other;
        }
        pkg.assets << asset;
    }
}

ModPackage ModImporter::import(const QString &sourcePath, const QString &workRoot, QString *error) {
    ModPackage pkg;
    pkg.sourcePath = sourcePath;
    pkg.name = QFileInfo(sourcePath).completeBaseName();
    if (pkg.name.isEmpty()) pkg.name = QFileInfo(sourcePath).fileName();
    pkg.id = shortHash(QFileInfo(sourcePath).absoluteFilePath());

    const QString modWorkDir = workRoot + QLatin1Char('/') + pkg.id;
    QDir(modWorkDir).removeRecursively();
    QDir().mkpath(modWorkDir);

    QString contentDir;
    if (!stageSource(sourcePath, modWorkDir, &contentDir, error))
        return {};
    pkg.extractDir = contentDir;
    scanAssets(pkg);
    if (pkg.assets.isEmpty()) {
        if (error) *error = tr("El mod no contiene assets");
        return {};
    }
    return pkg;
}

} // namespace st
