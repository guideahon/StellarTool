#include "AppController.h"

#include "core/PakService.h"
#include "core/UAssetService.h"
#include "core/Cue4Service.h"
#include "core/GamePaths.h"
#include "core/ModImporter.h"
#include "core/BaselineManager.h"
#include "core/ProjectStore.h"
#include "core/TableDiffEngine.h"
#include "core/MergeEngine.h"
#include "Translator.h"

#include <QtConcurrent>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QDateTime>
#include <QDesktopServices>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
// Enlaza src->dst con hardlink (instantáneo, mismo volumen); si falla
// (ej. distinto volumen) copia. Windows: QFile::link crearía un .lnk, no sirve.
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

AppController::AppController(Translator *i18n, QObject *parent)
    : QObject(parent),
      m_i18n(i18n),
      m_pak(new PakService(this)),
      m_uasset(new UAssetService(this)),
      m_cue4(new Cue4Service(this)),
      m_importer(new ModImporter(m_pak, m_uasset, m_cue4, i18n, this)),
      m_baseline(new BaselineManager(m_uasset, m_cue4, this)),
      m_store(new ProjectStore(this)) {
    connect(m_importer, &ModImporter::progress, this,
            [this](const QString &m) { setStatus(m); }, Qt::QueuedConnection);
    connect(m_baseline, &BaselineManager::progress, this,
            [this](const QString &m) { setStatus(m); }, Qt::QueuedConnection);
    m_changeModel.setItems(&m_items);
    m_conflictModel.setSource(&m_items, &m_groups);
}

QString AppController::t(const QString &key) const {
    return m_i18n ? m_i18n->t(key) : key;
}

AppController::~AppController() = default;

bool AppController::hasBaseline() const { return m_baseline->hasBaseline(); }
bool AppController::baselineStale() const {
    return GamePaths::hasGame() && m_baseline->isStale(GamePaths::paksDir());
}
bool AppController::toolsAvailable() const {
    return m_pak->available() && m_uasset->available();
}
QString AppController::toolsError() const {
    QStringList missing;
    if (!m_pak->available()) missing << QStringLiteral("repak.exe");
    if (!m_uasset->available()) missing << QStringLiteral("UAssetGUI.exe");
    if (missing.isEmpty()) return {};
    return t(QStringLiteral("tools_missing")).arg(missing.join(QStringLiteral(", ")));
}

void AppController::setBusy(bool b, const QString &status) {
    m_busy = b;
    if (!status.isEmpty() || !b) m_statusText = status;
    emit busyChanged();
    emit statusChanged();
}

void AppController::setStatus(const QString &s) {
    m_statusText = s;
    emit statusChanged();
}

QString AppController::workRoot() const {
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
           + QStringLiteral("/work");
}

void AppController::addMod(const QUrl &url) {
    if (m_busy) return;
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    for (const auto &m : m_mods) {
        if (QFileInfo(m.sourcePath) == QFileInfo(path)) {
            emit errorOccurred(t(QStringLiteral("err_mod_exists")));
            return;
        }
    }
    setBusy(true, t(QStringLiteral("core_importing")).arg(QFileInfo(path).fileName()));
    std::ignore = QtConcurrent::run([this, path] {
        QString error;
        ModPackage pkg = m_importer->import(path, workRoot(), &error);
        QMetaObject::invokeMethod(this, [this, pkg, error] {
            if (pkg.assets.isEmpty()) {
                emit errorOccurred(error.isEmpty() ? t(QStringLiteral("err_import_failed")) : error);
            } else {
                ModPackage p = pkg;
                p.loadOrder = m_mods.size();
                m_mods << p;
                m_modModel.setMods(m_mods);
                m_analyzed = false;
                emit analysisChanged();
            }
            setBusy(false);
        }, Qt::QueuedConnection);
    });
}

void AppController::removeMod(int row) {
    if (m_busy || row < 0 || row >= m_mods.size()) return;
    QDir(workRoot() + QLatin1Char('/') + m_mods.at(row).id).removeRecursively();
    m_mods.removeAt(row);
    for (int i = 0; i < m_mods.size(); ++i) m_mods[i].loadOrder = i;
    m_modModel.setMods(m_mods);
    m_items.clear();
    m_groups.clear();
    m_analyzed = false;
    m_changeModel.refresh();
    m_conflictModel.refresh();
    emit analysisChanged();
}

void AppController::clearMods() {
    if (m_busy) return;
    for (const auto &m : m_mods)
        QDir(workRoot() + QLatin1Char('/') + m.id).removeRecursively();
    m_mods.clear();
    m_items.clear();
    m_groups.clear();
    m_analyzed = false;
    m_modModel.setMods(m_mods);
    m_changeModel.refresh();
    m_conflictModel.refresh();
    emit analysisChanged();
}

QString AppController::ensureVanillaStage() {
    if (!GamePaths::hasGame()) return {};
    const QString stage = workRoot() + QStringLiteral("/vanilla_stage");
    if (QFileInfo::exists(stage + QStringLiteral("/global.utoc"))) return stage;
    QDir().mkpath(stage);
    QDirIterator gi(GamePaths::paksDir(),
                    {QStringLiteral("global.*"), QStringLiteral("pakchunk*")}, QDir::Files);
    while (gi.hasNext()) {
        gi.next();
        linkOrCopy(gi.filePath(), stage + QLatin1Char('/') + gi.fileName());
    }
    return stage;
}

QString AppController::vanillaUAssetJsonPath(const QString &tableBase) {
    const QString cacheDir = m_baseline->baselineDir() + QStringLiteral("/uasset_json");
    const QString cached = cacheDir + QLatin1Char('/') + tableBase.toLower() + QStringLiteral(".json");
    if (QFileInfo::exists(cached)) return cached;
    const QString stage = ensureVanillaStage();
    if (stage.isEmpty()) return {};
    QDir().mkpath(cacheDir);
    const QString legDir = workRoot() + QStringLiteral("/vanilla_extract/") + tableBase;
    QDir(legDir).removeRecursively();
    QString err;
    if (m_pak->toLegacyFiltered(stage, tableBase, legDir, &err) <= 0) return {};
    QString uasset;
    QDirIterator li(legDir, {tableBase + QStringLiteral(".uasset")}, QDir::Files,
                    QDirIterator::Subdirectories);
    if (li.hasNext()) uasset = li.next();
    if (uasset.isEmpty()) return {};
    if (!m_uasset->toJson(uasset, cached, &err)) return {};
    return cached;
}

void AppController::runAnalysis() {
    QList<ChangeItem> items;
    for (const ModPackage &mod : m_mods) {
        for (const ModAsset &asset : mod.assets) {
            if (asset.kind == ModAsset::DataTable) {
                QFile f(asset.jsonPath);
                if (!f.open(QIODevice::ReadOnly)) continue;
                // Cada representación diffea contra SU baseline para evitar ruido:
                //  - mods Zen (CUE4Parse) vs baseline CUE4Parse (normalizados)
                //  - mods legacy (UAssetGUI) vs tabla vanilla real UAssetGUI (crudos)
                QJsonObject modRoot = QJsonDocument::fromJson(f.readAll()).object();
                QJsonObject baseRoot;
                if (asset.cleanJson) {
                    modRoot = normalizeDataTableDoc(modRoot);
                    baseRoot = m_baseline->tableFor(asset.gamePath);
                    if (!baseRoot.isEmpty()) baseRoot = normalizeDataTableDoc(baseRoot);
                } else {
                    const QString vp = vanillaUAssetJsonPath(
                        QFileInfo(asset.gamePath).completeBaseName());
                    if (!vp.isEmpty()) {
                        QFile vf(vp);
                        if (vf.open(QIODevice::ReadOnly))
                            baseRoot = QJsonDocument::fromJson(vf.readAll()).object();
                    }
                }
                auto tableItems = TableDiffEngine::diffTable(baseRoot, modRoot, asset.gamePath,
                                                            mod.id, mod.name);
                if (asset.cleanJson)
                    for (auto &c : tableItems) c.clean = true;
                items << tableItems;
            } else if (asset.kind != ModAsset::DataTable) {
                // Asset no tabular: check todo-o-nada por archivo.
                ChangeItem c;
                c.modId = mod.id;
                c.modName = mod.name;
                c.tablePath = asset.gamePath;
                c.type = ChangeItem::AssetReplaced;
                items << c;
            }
        }
    }
    QList<ConflictGroup> groups = TableDiffEngine::findConflicts(items);
    for (auto &c : items)
        c.summaryCache = c.summary();   // precalcular para listas grandes
    QMetaObject::invokeMethod(this, [this, items, groups] {
        m_items = items;
        m_groups = groups;
        m_analyzed = true;
        m_changeModel.refresh();
        m_conflictModel.refresh();
        emit analysisChanged();
        setBusy(false, t(QStringLiteral("core_summary")).arg(m_items.size()).arg(m_groups.size()));
    }, Qt::QueuedConnection);
}

void AppController::analyze() {
    if (m_busy || m_mods.isEmpty()) return;
    setBusy(true, t(QStringLiteral("core_analyzing")));
    // Si hay juego configurado pero la baseline falta o quedó vieja (update del
    // juego), (re)construirla primero para que los diffs sean vanilla->mod reales.
    const bool needBaseline = GamePaths::hasGame()
        && (!m_baseline->hasBaseline() || m_baseline->isStale(GamePaths::paksDir()));
    const QString paks = GamePaths::paksDir();
    const QString usmap = m_uasset->usmapPath();
    std::ignore = QtConcurrent::run([this, needBaseline, paks, usmap] {
        if (needBaseline) {
            QString e; int n = 0;
            m_baseline->buildFromGame(paks, usmap, &e, &n);
            QMetaObject::invokeMethod(this, [this] { emit baselineChanged(); }, Qt::QueuedConnection);
        }
        runAnalysis();
    });
}

void AppController::resolveConflict(int groupId, const QString &modId) {
    for (auto &g : m_groups) {
        if (g.id != groupId) continue;
        g.resolvedModId = modId;
        for (int idx : g.itemIndexes)
            m_items[idx].selected = (m_items.at(idx).modId == modId);
    }
    m_changeModel.refresh();
    m_conflictModel.refresh();
}

void AppController::resolveAllWithMod(const QString &modId) {
    for (auto &g : m_groups) {
        bool hasMod = false;
        for (int idx : g.itemIndexes)
            if (m_items.at(idx).modId == modId) hasMod = true;
        if (!hasMod) continue;
        g.resolvedModId = modId;
        for (int idx : g.itemIndexes)
            m_items[idx].selected = (m_items.at(idx).modId == modId);
    }
    m_changeModel.refresh();
    m_conflictModel.refresh();
}

void AppController::resolveAllByPriority() {
    QHash<QString, int> orderById;
    for (const auto &m : m_mods) orderById.insert(m.id, m.loadOrder);
    for (auto &g : m_groups) {
        if (!g.resolvedModId.isEmpty()) continue;
        QString best;
        int bestOrder = INT_MAX;
        for (int idx : g.itemIndexes) {
            const int o = orderById.value(m_items.at(idx).modId, INT_MAX);
            if (o < bestOrder) { bestOrder = o; best = m_items.at(idx).modId; }
        }
        if (best.isEmpty()) continue;
        g.resolvedModId = best;
        for (int idx : g.itemIndexes)
            m_items[idx].selected = (m_items.at(idx).modId == best);
    }
    m_changeModel.refresh();
    m_conflictModel.refresh();
}

QStringList AppController::unresolvedConflictTitles() const {
    QStringList out;
    for (const auto &g : m_groups) {
        if (!g.resolvedModId.isEmpty()) continue;
        const ChangeItem &c = m_items.at(g.itemIndexes.first());
        out << c.summary();
    }
    return out;
}

// Copia un asset con sus compañeros (.uexp/.ubulk) al árbol destino.
static bool copyAssetWithCompanions(const QString &srcRoot, const QString &gamePath,
                                    const QString &dstRoot) {
    const QString src = srcRoot + QLatin1Char('/') + gamePath;
    const QString dst = dstRoot + QLatin1Char('/') + gamePath;
    QDir().mkpath(QFileInfo(dst).absolutePath());
    QFile::remove(dst);
    if (!QFile::copy(src, dst)) return false;
    const QFileInfo fi(src);
    for (const QString &ext : {QStringLiteral("uexp"), QStringLiteral("ubulk")}) {
        const QString compSrc = fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName()
                                + QLatin1Char('.') + ext;
        if (QFileInfo::exists(compSrc)) {
            const QFileInfo di(dst);
            const QString compDst = di.absolutePath() + QLatin1Char('/') + di.completeBaseName()
                                    + QLatin1Char('.') + ext;
            QFile::remove(compDst);
            QFile::copy(compSrc, compDst);
        }
    }
    return true;
}

QString AppController::runMerge(const QString &outDir) {
    m_lastSkipped = 0;
    // Reporte legible del merge (se escribe junto al pak y dentro del zip).
    QStringList report;
    report << QStringLiteral("Stellar Tool merge report — %1")
                  .arg(QDateTime::currentDateTime().toString(Qt::ISODate));
    report << QString();
    report << QStringLiteral("Mods (priority order, first wins):");
    for (const auto &m : m_mods)
        report << QStringLiteral("  %1. %2  [%3]").arg(m.loadOrder + 1).arg(m.name, m.sourcePath);
    if (!m_groups.isEmpty()) {
        report << QString() << QStringLiteral("Conflicts (%1):").arg(m_groups.size());
        for (const auto &g : m_groups) {
            QString winner, line;
            for (int idx : g.itemIndexes)
                if (m_items.at(idx).modId == g.resolvedModId) winner = m_items.at(idx).modName;
            const auto &first = m_items.at(g.itemIndexes.first());
            line = first.summaryCache.isEmpty() ? first.summary() : first.summaryCache;
            report << QStringLiteral("  %1 -> %2").arg(line, winner);
        }
    }
    report << QString() << QStringLiteral("Tables:");

    const QString mergeRoot = workRoot() + QStringLiteral("/merged");
    QDir(mergeRoot).removeRecursively();
    const QString contentDir = mergeRoot + QStringLiteral("/content");
    const QString jsonDir = mergeRoot + QStringLiteral("/json");
    QDir().mkpath(contentDir);
    QDir().mkpath(jsonDir);

    // 1) Assets no tabulares: prioridad = orden de la lista (primero gana),
    //    así que se copian en orden inverso para que el de mayor prioridad pise.
    for (int i = m_mods.size() - 1; i >= 0; --i) {
        const ModPackage &mod = m_mods.at(i);
        for (const ModAsset &asset : mod.assets) {
            if (asset.kind == ModAsset::DataTable) continue;
            // Respetar selección/resolución del AssetReplaced correspondiente.
            bool selected = true;
            for (const ChangeItem &c : m_items)
                if (c.type == ChangeItem::AssetReplaced && c.modId == mod.id
                    && c.tablePath.toLower() == asset.pathKey())
                    selected = c.selected;
            if (!selected) continue;
            copyAssetWithCompanions(mod.extractDir, asset.gamePath, contentDir);
        }
    }

    // 2) Tablas: agrupar items seleccionados por tabla.
    QMap<QString, QList<ChangeItem>> byTable; // pathKey -> items
    QMap<QString, QString> tableGamePath;     // pathKey -> gamePath real
    for (const ChangeItem &c : m_items) {
        if (c.type == ChangeItem::AssetReplaced || !c.selected) continue;
        byTable[c.tablePath.toLower()] << c;
        tableGamePath[c.tablePath.toLower()] = c.tablePath;
    }

    // Base de escritura: JSON UAssetGUI real de la tabla vanilla (cacheado).
    auto realVanilla = [&](const QString &tableBase, QString *) -> QJsonObject {
        const QString vp = vanillaUAssetJsonPath(tableBase);
        if (vp.isEmpty()) return {};
        QFile f(vp);
        if (!f.open(QIODevice::ReadOnly)) return {};
        return QJsonDocument::fromJson(f.readAll()).object();
    };

    for (auto it = byTable.begin(); it != byTable.end(); ++it) {
        const QString gamePath = tableGamePath.value(it.key());
        const QString tableBase = QFileInfo(gamePath).completeBaseName();

        // Base de ESCRITURA: UAssetGUI real (round-trippable). Prioridad:
        //  a) tabla vanilla real del juego (retoc to-legacy + tojson)
        //  b) JSON real de un mod legacy que traiga la tabla (no CUE4Parse)
        //  c) baseline en cache (puede ser CUE4Parse -> solo si es fromjson-able)
        QString verr;
        QJsonObject base = realVanilla(tableBase, &verr);
        if (base.isEmpty()) {
            for (const ModPackage &mod : m_mods) {
                for (const ModAsset &asset : mod.assets) {
                    if (asset.kind == ModAsset::DataTable && asset.pathKey() == it.key()
                        && !asset.localPath.isEmpty()) { // legacy: uasset real presente
                        QFile f(asset.jsonPath);
                        if (f.open(QIODevice::ReadOnly))
                            base = QJsonDocument::fromJson(f.readAll()).object();
                        break;
                    }
                }
                if (!base.isEmpty()) break;
            }
        }
        if (base.isEmpty())
            return tr("No hay base de escritura para %1. Configurá la carpeta del juego "
                      "para poder mergear mods Zen.").arg(gamePath);

        const auto res = MergeEngine::applyToTable(base, it.value());
        if (!res.ok) return res.error;
        m_lastSkipped += res.skipped;
        report << QStringLiteral("  %1: %2 applied, %3 skipped")
                      .arg(tableBase).arg(res.applied).arg(res.skipped);

        const QString mergedJson = jsonDir + QLatin1Char('/')
            + QString(gamePath).replace(QLatin1Char('/'), QLatin1Char('_')) + QStringLiteral(".json");
        QFile jf(mergedJson);
        if (!jf.open(QIODevice::WriteOnly))
            return tr("No se pudo escribir %1").arg(mergedJson);
        jf.write(QJsonDocument(base).toJson(QJsonDocument::Indented));
        jf.close();

        const QString outUasset = contentDir + QLatin1Char('/') + gamePath;
        QString err;
        QMetaObject::invokeMethod(this, [this, gamePath] {
            setStatus(t(QStringLiteral("core_generating")).arg(QFileInfo(gamePath).fileName()));
        }, Qt::QueuedConnection);
        if (!m_uasset->fromJson(mergedJson, outUasset, &err))
            return err;

        // Verificación: reconvertir a JSON y comparar filas contra lo planificado.
        const QString verifyJson = mergedJson + QStringLiteral(".verify.json");
        if (!m_uasset->toJson(outUasset, verifyJson, &err))
            return tr("Verificación falló en %1: %2").arg(gamePath, err);
        QFile vf(verifyJson);
        if (!vf.open(QIODevice::ReadOnly))
            return tr("Verificación: no se pudo leer %1").arg(verifyJson);
        const QJsonObject verifyRoot = QJsonDocument::fromJson(vf.readAll()).object();
        // Comparar por VALORES (normalizado): UAssetGUI puede reordenar/reformatear
        // metadata de serialización sin cambiar el contenido real de la tabla.
        if (!jsonValueEquals(dataTableRows(normalizeDataTableDoc(base)),
                             dataTableRows(normalizeDataTableDoc(verifyRoot))))
            return tr("Verificación falló: %1 no round-tripea fiel. Tabla excluida del merge; "
                      "reportar el caso.").arg(gamePath);
    }

    if (QDir(contentDir).isEmpty())
        return tr("No hay nada seleccionado para mergear.");

    // 3) Empaquetar. Con retoc disponible se genera contenedor Zen/IoStore
    //    (formato nativo de Stellar Blade); si no, pak legacy.
    QString err;
    QMetaObject::invokeMethod(this, [this] { setStatus(t(QStringLiteral("core_packing"))); }, Qt::QueuedConnection);
    const QString baseName = QStringLiteral("zzz_StellarTool_Merged_P");
    if (m_pak->zenAvailable()) {
        if (!m_pak->packZen(contentDir, outDir + QLatin1Char('/') + baseName + QStringLiteral(".utoc"), &err))
            return err;
    } else {
        if (!m_pak->pack(contentDir, outDir + QLatin1Char('/') + baseName + QStringLiteral(".pak"), &err))
            return err;
    }

    // Reporte legible junto al pak.
    report << QString()
           << QStringLiteral("Skipped = non-numeric changes from Zen-read mods (text/enums/arrays)")
           << QStringLiteral("that don't round-trip reliably into a Zen container.");
    {
        QFile rf(outDir + QStringLiteral("/merge_report.txt"));
        if (rf.open(QIODevice::WriteOnly))
            rf.write(report.join(QLatin1Char('\n')).toUtf8());
    }

    // 4) Zip instalable para mod managers (Vortex, etc.): Paks/<archivos> + readme.
    if (m_exportZip) {
        QMetaObject::invokeMethod(this, [this] { setStatus(t(QStringLiteral("core_making_zip"))); }, Qt::QueuedConnection);
        const QString zipStage = mergeRoot + QStringLiteral("/zipstage");
        QDir().mkpath(zipStage + QStringLiteral("/Paks"));
        for (const QString &ext : {QStringLiteral("pak"), QStringLiteral("ucas"), QStringLiteral("utoc")}) {
            const QString src = outDir + QLatin1Char('/') + baseName + QLatin1Char('.') + ext;
            if (QFileInfo::exists(src))
                QFile::copy(src, zipStage + QStringLiteral("/Paks/") + baseName + QLatin1Char('.') + ext);
        }
        QStringList modNames;
        for (const auto &m : m_mods) modNames << QStringLiteral("- %1").arg(m.name);
        int selectedCount = 0;
        for (const auto &c : m_items) if (c.selected) ++selectedCount;
        QFile::copy(outDir + QStringLiteral("/merge_report.txt"),
                    zipStage + QStringLiteral("/merge_report.txt"));
        QFile readme(zipStage + QStringLiteral("/README.txt"));
        if (readme.open(QIODevice::WriteOnly)) {
            readme.write(tr("Merge generado por Stellar Tool\n"
                            "https://github.com/guideahon/StellarTool\n\n"
                            "Fecha: %1\nMods de origen:\n%2\n\nCambios aplicados: %3\n\n"
                            "Instalacion manual: copiar el contenido de Paks\\ a\n"
                            "  steamapps\\common\\StellarBlade\\SB\\Content\\Paks\\~mods\n"
                            "O instalar este zip directamente con tu mod manager (Vortex, etc.).\n"
                            "Desactiva los mods de origen para que no pisen el merge.\n")
                             .arg(QDateTime::currentDateTime().toString(Qt::ISODate),
                                  modNames.join(QLatin1Char('\n')))
                             .arg(selectedCount)
                             .toUtf8());
            readme.close();
        }
        if (!m_pak->createZip(zipStage, outDir + QStringLiteral("/zzz_StellarTool_Merged.zip"), &err))
            return tr("El pak se generó pero falló el zip: %1").arg(err);
    }
    return {};
}

void AppController::merge(const QUrl &outDirUrl) {
    if (m_busy || !m_analyzed) return;
    const QStringList pending = unresolvedConflictTitles();
    if (!pending.isEmpty()) {
        emit errorOccurred(t(QStringLiteral("err_unresolved"))
                               .arg(pending.size())
                               .arg(pending.mid(0, 8).join(QLatin1Char('\n'))));
        return;
    }
    const QString outDir = outDirUrl.isLocalFile() ? outDirUrl.toLocalFile() : outDirUrl.toString();
    if (outDir.isEmpty()) {
        emit errorOccurred(t(QStringLiteral("err_choose_out")));
        return;
    }
    setBusy(true, t(QStringLiteral("core_merging")));
    std::ignore = QtConcurrent::run([this, outDir] {
        const QString error = runMerge(outDir);
        QMetaObject::invokeMethod(this, [this, error, outDir] {
            if (error.isEmpty()) {
                m_lastMergeOk = true;
                m_lastMergeResult = (m_exportZip ? t(QStringLiteral("merge_ok_zip"))
                                                 : t(QStringLiteral("merge_ok"))).arg(outDir);
                if (m_lastSkipped > 0)
                    m_lastMergeResult += QStringLiteral(" ") + t(QStringLiteral("merge_skipped_note")).arg(m_lastSkipped);
            } else {
                m_lastMergeOk = false;
                m_lastMergeResult = t(QStringLiteral("merge_err")).arg(error);
                emit errorOccurred(error);
            }
            emit mergeFinished();
            setBusy(false);
        }, Qt::QueuedConnection);
    });
}

QString AppController::gamePath() const { return GamePaths::gameRoot(); }
bool AppController::hasGamePath() const { return GamePaths::hasGame(); }
QString AppController::defaultOutDir() const { return GamePaths::modsDir(); }

// ¿El origen del mod vive dentro de ~mods del juego?
static bool sourceInMods(const QString &sourcePath) {
    const QString mods = GamePaths::modsDir();
    if (mods.isEmpty()) return false;
    const QString src = QDir::cleanPath(QFileInfo(sourcePath).absoluteFilePath()).toLower();
    const QString base = QDir::cleanPath(mods).toLower() + QLatin1Char('/');
    return src.startsWith(base) && !src.contains(QLatin1String("/disabled/"));
}

int AppController::disableableSourceCount() const {
    int n = 0;
    for (const auto &m : m_mods)
        if (sourceInMods(m.sourcePath)) ++n;
    return n;
}

int AppController::disableSourceMods() {
    const QString disabledDir = GamePaths::modsDir() + QStringLiteral("/disabled");
    QDir().mkpath(disabledDir);
    int moved = 0;
    for (const auto &m : m_mods) {
        if (!sourceInMods(m.sourcePath)) continue;
        const QFileInfo fi(m.sourcePath);
        // Mover el archivo/carpeta y sus compañeros de contenedor (.ucas/.utoc/.sig).
        QStringList toMove;
        if (fi.isDir()) {
            toMove << fi.absoluteFilePath();
        } else {
            toMove << fi.absoluteFilePath();
            for (const QString &ext : {QStringLiteral("ucas"), QStringLiteral("utoc"), QStringLiteral("sig")}) {
                const QString comp = fi.absolutePath() + QLatin1Char('/')
                    + fi.completeBaseName() + QLatin1Char('.') + ext;
                if (QFileInfo::exists(comp)) toMove << comp;
            }
        }
        bool ok = true;
        for (const QString &p : toMove) {
            const QString dst = disabledDir + QLatin1Char('/') + QFileInfo(p).fileName();
            QFile::remove(dst);
            QDir dstDir(dst);
            if (QFileInfo(p).isDir()) {
                if (dstDir.exists()) dstDir.removeRecursively();
                ok = QDir().rename(p, dst) && ok;
            } else {
                ok = QFile::rename(p, dst) && ok;
            }
        }
        if (ok) ++moved;
    }
    return moved;
}

void AppController::openDir(const QString &path) {
    if (!path.isEmpty() && QFileInfo::exists(path))
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

bool AppController::advancedMode() const {
    QSettings s;
    return s.value(QStringLiteral("advancedMode"), false).toBool();
}

void AppController::setAdvancedMode(bool v) {
    QSettings s;
    if (s.value(QStringLiteral("advancedMode"), false).toBool() == v) return;
    s.setValue(QStringLiteral("advancedMode"), v);
    emit advancedModeChanged();
}

void AppController::setGamePath(const QUrl &dirUrl) {
    const QString dir = dirUrl.isLocalFile() ? dirUrl.toLocalFile() : dirUrl.toString();
    GamePaths::setGameRoot(dir);
    emit gamePathChanged();
    if (!GamePaths::hasGame())
        emit errorOccurred(t(QStringLiteral("err_game_not_found")));
}

void AppController::buildBaselineFromGame() {
    if (m_busy) return;
    if (!GamePaths::hasGame()) {
        emit errorOccurred(t(QStringLiteral("err_need_game_path")));
        return;
    }
    const QString paks = GamePaths::paksDir();
    const QString usmap = m_uasset->usmapPath();
    setBusy(true, t(QStringLiteral("core_importing_baseline")));
    std::ignore = QtConcurrent::run([this, paks, usmap] {
        QString error;
        int imported = 0;
        const bool ok = m_baseline->buildFromGame(paks, usmap, &error, &imported);
        QMetaObject::invokeMethod(this, [this, ok, error, imported] {
            if (ok) {
                setStatus(t(QStringLiteral("core_baseline_done")).arg(imported));
                emit baselineChanged();
                m_analyzed = false;
                emit analysisChanged();
            } else {
                emit errorOccurred(error);
            }
            setBusy(false);
        }, Qt::QueuedConnection);
    });
}

void AppController::importBaseline(const QUrl &dirUrl) {
    if (m_busy) return;
    const QString dir = dirUrl.isLocalFile() ? dirUrl.toLocalFile() : dirUrl.toString();
    setBusy(true, t(QStringLiteral("core_importing_baseline")));
    std::ignore = QtConcurrent::run([this, dir] {
        QString error;
        int imported = 0;
        const bool ok = m_baseline->importFromDir(dir, &error, &imported);
        QMetaObject::invokeMethod(this, [this, ok, error, imported] {
            if (ok) {
                setStatus(t(QStringLiteral("core_baseline_done")).arg(imported));
                emit baselineChanged();
                m_analyzed = false;
                emit analysisChanged();
            } else {
                emit errorOccurred(error);
            }
            setBusy(false);
        }, Qt::QueuedConnection);
    });
}

void AppController::saveProject(const QUrl &fileUrl) {
    ProjectStore::ProjectState state;
    for (const auto &m : m_mods) state.modSources << m.sourcePath;
    for (const auto &c : m_items)
        state.selections.insert(c.key() + QLatin1Char('|') + c.modId, c.selected);
    for (const auto &g : m_groups)
        if (!g.resolvedModId.isEmpty()) state.resolutions.insert(g.key, g.resolvedModId);
    QString error;
    const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    if (!m_store->save(path, state, &error))
        emit errorOccurred(error);
    else
        setStatus(t(QStringLiteral("core_project_saved")));
}

void AppController::loadProject(const QUrl &fileUrl) {
    if (m_busy) return;
    ProjectStore::ProjectState state;
    QString error;
    const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    if (!m_store->load(path, &state, &error)) {
        emit errorOccurred(error);
        return;
    }
    m_mods.clear();
    m_items.clear();
    m_groups.clear();
    m_modModel.setMods(m_mods);
    m_changeModel.refresh();
    m_conflictModel.refresh();

    setBusy(true, t(QStringLiteral("core_loading_project")));
    std::ignore = QtConcurrent::run([this, state] {
        QList<ModPackage> mods;
        QString error;
        for (const QString &src : state.modSources) {
            ModPackage pkg = m_importer->import(src, workRoot(), &error);
            if (!pkg.assets.isEmpty()) {
                pkg.loadOrder = mods.size();
                mods << pkg;
            }
        }
        QMetaObject::invokeMethod(this, [this, mods, state] {
            m_mods = mods;
            m_modModel.setMods(m_mods);
            setBusy(true, t(QStringLiteral("core_analyzing")));
            std::ignore = QtConcurrent::run([this, state] {
                runAnalysis();
                QMetaObject::invokeMethod(this, [this, state] {
                    // Restaurar selecciones y resoluciones sobre el análisis fresco.
                    for (int i = 0; i < m_items.size(); ++i) {
                        const QString key = m_items.at(i).key() + QLatin1Char('|') + m_items.at(i).modId;
                        if (state.selections.contains(key))
                            m_items[i].selected = state.selections.value(key);
                    }
                    for (auto &g : m_groups) {
                        if (state.resolutions.contains(g.key))
                            g.resolvedModId = state.resolutions.value(g.key);
                    }
                    m_changeModel.refresh();
                    m_conflictModel.refresh();
                }, Qt::QueuedConnection);
            });
        }, Qt::QueuedConnection);
    });
}

} // namespace st
