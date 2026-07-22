#include "ModImporter.h"
#include "PakService.h"
#include "UAssetService.h"
#include "Cue4Service.h"
#include "GamePaths.h"
#include "../Translator.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLoggingCategory>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

Q_LOGGING_CATEGORY(lcImport, "st.import")

namespace {
// Hardlink src->dst (instantáneo, sin ocupar disco extra; requiere mismo
// volumen). Si falla (distinto volumen), copia. Evita duplicar el global.ucas
// del juego (varios GB) en la unidad del AppData al leer mods Zen.
bool linkOrCopy(const QString &src, const QString &dst) {
    if (QFileInfo::exists(dst)) return true;
#ifdef Q_OS_WIN
    if (CreateHardLinkW(reinterpret_cast<const wchar_t *>(QDir::toNativeSeparators(dst).utf16()),
                        reinterpret_cast<const wchar_t *>(QDir::toNativeSeparators(src).utf16()),
                        nullptr))
        return true;
#endif
    return QFile::copy(src, dst);
}
}

namespace st {

ModImporter::ModImporter(PakService *pak, UAssetService *uasset, Cue4Service *cue4,
                         Translator *i18n, QObject *parent)
    : QObject(parent), m_pak(pak), m_uasset(uasset), m_cue4(cue4), m_i18n(i18n) {}

QString ModImporter::t(const QString &key) const {
    return m_i18n ? m_i18n->t(key) : key;
}

static QStringList findPaks(const QString &dir) {
    QStringList out;
    QDirIterator it(dir, {QStringLiteral("*.pak")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) out << it.next();
    return out;
}

// Desempaqueta un .pak en extractDir. Detecta contenedores Zen/IoStore
// (sibling .utoc): intenta revertir a legacy con retoc; si no se puede
// (limitación conocida en mods ya empaquetados), da un error accionable.
// Reúne los contenedores global del juego + el mod en un stage temporal para
// que CUE4Parse resuelva los tipos. Devuelve el dir de stage, o vacío.
// El stage se ubica en el volumen del juego (junto a los Paks) para poder
// hardlinkear el global (varios GB): no ocupa disco extra. Si esa carpeta no
// es escribible, cae al workDir del mod (con hardlink si es el mismo volumen,
// o copia si no). Los contenedores del mod son chicos: se copian siempre.
static QString stageForCue4(const QString &modUtoc, const QString &modWorkDir) {
    const QStringList globals = GamePaths::globalContainerFiles();
    if (globals.isEmpty()) return {};

    // Preferir un stage en la unidad del juego para que el global se hardlinkee.
    QString stage = GamePaths::paksDir() + QStringLiteral("/~st_cue4stage");
    QDir(stage).removeRecursively();
    if (!QDir().mkpath(stage)) {
        stage = modWorkDir + QStringLiteral("/cue4stage");
        QDir(stage).removeRecursively();
        if (!QDir().mkpath(stage)) return {};
    }

    for (const QString &g : globals)
        linkOrCopy(g, stage + QLatin1Char('/') + QFileInfo(g).fileName());
    const QFileInfo mi(modUtoc);
    for (const QString &ext : {QStringLiteral("utoc"), QStringLiteral("ucas"), QStringLiteral("pak")}) {
        const QString f = mi.absolutePath() + QLatin1Char('/') + mi.completeBaseName() + QLatin1Char('.') + ext;
        if (QFileInfo::exists(f))
            QFile::copy(f, stage + QLatin1Char('/') + QFileInfo(f).fileName());
    }
    return stage;
}

int ModImporter::importZenTables(const QString &utoc, const QString &modWorkDir,
                                 ModPackage &pkg, QString *error) {
    if (!m_cue4 || !m_cue4->available()) {
        if (error) *error = t(QStringLiteral("err_cue4_missing"));
        return 0;
    }
    if (!GamePaths::hasGame()) {
        if (error) *error = t(QStringLiteral("err_need_game_path"));
        return 0;
    }
    const QString stage = stageForCue4(utoc, modWorkDir);
    if (stage.isEmpty()) {
        if (error) *error = t(QStringLiteral("err_need_game_path"));
        return 0;
    }
    emit progress(t(QStringLiteral("core_reading_zen")).arg(QFileInfo(utoc).completeBaseName()));
    const QString jsonDir = modWorkDir + QStringLiteral("/cue4json");
    const auto tables = m_cue4->exportTables(stage, jsonDir, m_uasset->usmapPath(),
                                             QStringLiteral("*Table*"), error);
    QDir(stage).removeRecursively();
    if (tables.isEmpty())
        return 0;

    int n = 0;
    const QString convDir = modWorkDir + QStringLiteral("/cue4conv");
    QDir().mkpath(convDir);
    for (auto it = tables.begin(); it != tables.end(); ++it) {
        QFile f(it.value());
        if (!f.open(QIODevice::ReadOnly)) continue;
        const QJsonObject doc = cue4ToDataTableDoc(f.readAll());
        f.close();
        if (!isDataTableJson(doc)) continue;
        const QString convPath = convDir + QLatin1Char('/') + it.key() + QStringLiteral(".json");
        QFile o(convPath);
        if (!o.open(QIODevice::WriteOnly)) continue;
        o.write(QJsonDocument(doc).toJson(QJsonDocument::Compact));
        o.close();

        ModAsset asset;
        // Ruta de juego canónica de las DataTables de SB.
        asset.gamePath = QStringLiteral("SB/Content/Local/Data/") + it.key() + QStringLiteral(".uasset");
        asset.jsonPath = convPath;
        asset.kind = ModAsset::DataTable;
        asset.cleanJson = true;   // leído con CUE4Parse
        pkg.assets << asset;
        ++n;
    }
    return n;
}

bool ModImporter::unpackPakInto(const QString &pak, const QString &extractDir,
                                const QString &modWorkDir, ModPackage &pkg, QString *error) {
    const QFileInfo fi(pak);
    const QString utoc = fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName() + QStringLiteral(".utoc");
    if (QFileInfo::exists(utoc)) {
        // Mod Zen/IoStore: el .pak es cáscara; los datos están en .ucas/.utoc.
        // 1) retoc to-legacy (rápido) si funciona.
        if (m_pak->zenAvailable()) {
            emit progress(t(QStringLiteral("core_converting_zen")).arg(fi.fileName()));
            QString convErr;
            if (m_pak->toLegacy(utoc, extractDir, &convErr) > 0)
                return true;
        }
        // 2) CUE4Parse (lee Zen que retoc no puede revertir).
        QString zenErr;
        if (importZenTables(utoc, modWorkDir, pkg, &zenErr) > 0)
            return true;
        if (error) {
            if (!zenErr.isEmpty()) { *error = zenErr; return false; }
            const QStringList tables = m_pak->zenAvailable() ? m_pak->listZenAssets(utoc) : QStringList();
            *error = tables.isEmpty()
                ? t(QStringLiteral("err_zen_mod")).arg(fi.fileName())
                : t(QStringLiteral("err_zen_mod_tables")).arg(fi.fileName(), tables.join(QStringLiteral(", ")));
        }
        return false;
    }
    emit progress(t(QStringLiteral("core_unpacking")).arg(fi.fileName()));
    return m_pak->unpack(pak, extractDir, error);
}

bool ModImporter::stageSource(const QString &sourcePath, const QString &modWorkDir,
                              ModPackage &pkg, QString *contentDir, QString *error) {
    const QFileInfo fi(sourcePath);
    const QString extractDir = modWorkDir + QStringLiteral("/content");
    QDir().mkpath(extractDir);

    if (fi.isDir()) {
        // Carpeta: puede contener paks o assets sueltos.
        const QStringList paks = findPaks(sourcePath);
        if (!paks.isEmpty()) {
            for (const QString &pak : paks)
                if (!unpackPakInto(pak, extractDir, modWorkDir, pkg, error)) return false;
        } else {
            emit progress(t(QStringLiteral("core_copying_folder")));
            QDirIterator it(sourcePath, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                const QString src = it.next();
                const QString rel = QDir(sourcePath).relativeFilePath(src);
                const QString dst = extractDir + QLatin1Char('/') + rel;
                QDir().mkpath(QFileInfo(dst).absolutePath());
                if (!QFile::copy(src, dst)) {
                    if (error) *error = t(QStringLiteral("err_copy_failed")).arg(rel);
                    return false;
                }
            }
        }
    } else if (fi.suffix().compare(QLatin1String("pak"), Qt::CaseInsensitive) == 0) {
        if (!unpackPakInto(sourcePath, extractDir, modWorkDir, pkg, error)) return false;
    } else if (fi.suffix().compare(QLatin1String("utoc"), Qt::CaseInsensitive) == 0) {
        // Contenedor Zen directo: retoc, luego CUE4Parse.
        if (m_pak->zenAvailable() && m_pak->toLegacy(sourcePath, extractDir, nullptr) > 0) {
            // ok
        } else if (importZenTables(sourcePath, modWorkDir, pkg, error) > 0) {
            // ok
        } else {
            if (error && error->isEmpty()) *error = t(QStringLiteral("err_zen_container")).arg(fi.fileName());
            return false;
        }
    } else if (fi.suffix().compare(QLatin1String("zip"), Qt::CaseInsensitive) == 0) {
        const QString zipDir = modWorkDir + QStringLiteral("/zip");
        emit progress(t(QStringLiteral("core_extracting_zip")));
        if (!m_pak->extractZip(sourcePath, zipDir, error)) return false;
        const QStringList paks = findPaks(zipDir);
        if (!paks.isEmpty()) {
            for (const QString &pak : paks)
                if (!unpackPakInto(pak, extractDir, modWorkDir, pkg, error)) return false;
        } else {
            // Zip sin paks: puede traer .uasset sueltos (ej. layout package/SB/...).
            emit progress(t(QStringLiteral("core_copying_zip")));
            bool anyAsset = false;
            QDirIterator it(zipDir, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                const QString src = it.next();
                const QString rel = QDir(zipDir).relativeFilePath(src);
                const QString dst = extractDir + QLatin1Char('/') + rel;
                QDir().mkpath(QFileInfo(dst).absolutePath());
                QFile::copy(src, dst);
                if (src.endsWith(QLatin1String(".uasset"), Qt::CaseInsensitive)) anyAsset = true;
            }
            if (!anyAsset) {
                if (error) *error = t(QStringLiteral("err_zip_empty"));
                return false;
            }
        }
    } else {
        if (error) *error = t(QStringLiteral("err_unsupported")).arg(fi.suffix());
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
            emit progress(t(QStringLiteral("core_analyzing_asset")).arg(fi.fileName()));
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
    if (!stageSource(sourcePath, modWorkDir, pkg, &contentDir, error))
        return {};
    pkg.extractDir = contentDir;
    scanAssets(pkg);   // agrega assets de archivos extraídos (además de los Zen ya cargados)
    if (pkg.assets.isEmpty()) {
        if (error) *error = t(QStringLiteral("err_no_assets"));
        return {};
    }
    return pkg;
}

} // namespace st
