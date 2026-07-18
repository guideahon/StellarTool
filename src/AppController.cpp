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
#include <QStandardPaths>
#include <QUrl>

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

void AppController::runAnalysis() {
    QList<ChangeItem> items;
    for (const ModPackage &mod : m_mods) {
        for (const ModAsset &asset : mod.assets) {
            if (asset.kind == ModAsset::DataTable) {
                QFile f(asset.jsonPath);
                if (!f.open(QIODevice::ReadOnly)) continue;
                const QJsonObject modRoot = normalizeDataTableDoc(
                    QJsonDocument::fromJson(f.readAll()).object());
                QJsonObject baseRoot = m_baseline->tableFor(asset.gamePath);
                if (!baseRoot.isEmpty()) baseRoot = normalizeDataTableDoc(baseRoot);
                items << TableDiffEngine::diffTable(baseRoot, modRoot, asset.gamePath,
                                                    mod.id, mod.name);
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
    std::ignore = QtConcurrent::run([this] { runAnalysis(); });
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

    for (auto it = byTable.begin(); it != byTable.end(); ++it) {
        const QString gamePath = tableGamePath.value(it.key());
        // Base: baseline si existe; si no, tabla del mod de mayor prioridad que la tenga.
        QJsonObject base = m_baseline->tableFor(gamePath);
        if (base.isEmpty()) {
            for (const ModPackage &mod : m_mods) {
                for (const ModAsset &asset : mod.assets) {
                    if (asset.kind == ModAsset::DataTable && asset.pathKey() == it.key()) {
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
            return tr("No hay JSON base para %1").arg(gamePath);

        const auto res = MergeEngine::applyToTable(base, it.value());
        if (!res.ok) return res.error;

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
        if (!jsonValueEquals(dataTableRows(base), dataTableRows(verifyRoot)))
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
