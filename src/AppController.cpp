#include "AppController.h"

#include "core/PakService.h"
#include "core/UAssetService.h"
#include "core/Cue4Service.h"
#include "core/CnsConverterService.h"
#include "core/CnsIdFixerService.h"
#include "core/SaveConverterService.h"
#include "core/GamePaths.h"
#include "core/UsmapService.h"
#include "core/ModImporter.h"
#include "core/BaselineManager.h"
#include "core/ProjectStore.h"
#include "core/TableDiffEngine.h"
#include "core/MergeEngine.h"
#include "core/TomlPatch.h"
#include "Translator.h"

#include <QtConcurrent>
#include <QProcess>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTextStream>
#include <QDateTime>
#include <QDesktopServices>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <QRegularExpression>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
// Devuelve el primer punto divergente sin volcar una EffectTable completa.
// El detalle queda en merge_report.txt para que una exclusión sea accionable.
QString firstJsonDifference(const QJsonValue &a, const QJsonValue &b,
                            const QString &path = QStringLiteral("root")) {
    if (a.isDouble() && b.isDouble()) {
        const double x = a.toDouble(), y = b.toDouble();
        const double scale = (std::abs(x) > std::abs(y)) ? std::abs(x) : std::abs(y);
        if (x == y || std::abs(x - y) <= scale * 1e-6) return {};
    } else if (a.isArray() && b.isArray()) {
        const QJsonArray aa = a.toArray(), ba = b.toArray();
        if (aa.size() != ba.size())
            return QStringLiteral("%1 length %2 vs %3").arg(path).arg(aa.size()).arg(ba.size());
        for (int i = 0; i < aa.size(); ++i) {
            const QString diff = firstJsonDifference(aa.at(i), ba.at(i),
                                                     QStringLiteral("%1[%2]").arg(path).arg(i));
            if (!diff.isEmpty()) return diff;
        }
        return {};
    } else if (a.isObject() && b.isObject()) {
        const QJsonObject ao = a.toObject(), bo = b.toObject();
        if (ao.size() != bo.size())
            return QStringLiteral("%1 object size %2 vs %3").arg(path).arg(ao.size()).arg(bo.size());
        for (auto it = ao.begin(); it != ao.end(); ++it) {
            if (!bo.contains(it.key())) return QStringLiteral("%1 missing key %2").arg(path, it.key());
            const QString diff = firstJsonDifference(it.value(), bo.value(it.key()),
                                                     QStringLiteral("%1.%2").arg(path, it.key()));
            if (!diff.isEmpty()) return diff;
        }
        return {};
    } else if (a == b) {
        return {};
    }
    return QStringLiteral("%1: type %2=%3 vs type %4=%5")
        .arg(path).arg(int(a.type())).arg(a.toString()).arg(int(b.type())).arg(b.toString());
}

// Enlaza src->dst con hardlink (instantáneo, mismo volumen). NUNCA copia: los
// contenedores del juego pesan ~100 GB y una copia llenaría el disco (o
// quedaría a medias) en vez de fallar rápido. Windows: QFile::link crearía un
// .lnk, no sirve.
bool hardLink(const QString &src, const QString &dst) {
    if (QFileInfo::exists(dst)) return true;
#ifdef Q_OS_WIN
    return CreateHardLinkW(reinterpret_cast<const wchar_t *>(QDir::toNativeSeparators(dst).utf16()),
                           reinterpret_cast<const wchar_t *>(QDir::toNativeSeparators(src).utf16()),
                           nullptr);
#else
    return QFile::link(src, dst);
#endif
}
}

namespace st {

static QString builderScript();
static QString pythonExe();
static void applyBuilderEnv(QProcess &proc, const QString &gameDir = QString());

AppController::AppController(Translator *i18n, QObject *parent)
    : QObject(parent),
      m_i18n(i18n),
      m_pak(new PakService(this)),
      m_uasset(new UAssetService(this)),
      m_usmap(new UsmapService(this)),
      m_cue4(new Cue4Service(this)),
      m_cns(new CnsConverterService(m_pak, m_uasset, m_cue4, this)),
      m_cnsIdFixer(new CnsIdFixerService(this)),
      m_saveConverter(new SaveConverterService(this)),
      m_importer(new ModImporter(m_pak, m_uasset, m_cue4, i18n, this)),
      m_baseline(new BaselineManager(m_uasset, m_cue4, this)),
      m_store(new ProjectStore(this)) {
    // Un stage de CUE4Parse de una corrida anterior (app cerrada a mitad de un
    // import) deja el global del juego duplicado dentro de Paks y hace que cada
    // export posterior relea el juego entero de más.
    GamePaths::cleanCue4Stages();
    connect(m_importer, &ModImporter::progress, this,
            [this](const QString &m) { setStatus(m); }, Qt::QueuedConnection);
    connect(m_baseline, &BaselineManager::progress, this,
            [this](const QString &m) { setStatus(m); }, Qt::QueuedConnection);
    // Estaba desconectada: el avance real de cue4parse (N de M paquetes) no
    // llegaba a la UI y una corrida larga parecía un cuelgue.
    connect(m_cue4, &Cue4Service::progress, this,
            [this](const QString &m) { setStatus(m); }, Qt::QueuedConnection);
    connect(m_usmap, &UsmapService::progress, this,
            [this](const QString &m) { setStatus(m); }, Qt::QueuedConnection);
    connect(m_usmap, &UsmapService::finished, this,
            [this](bool ok, const QString &msg, const QString &path) {
        m_downloadingUsmap = false;
        emit usmapDownloadChanged();
        if (ok && !path.isEmpty()) {
            UAssetService::setCustomUsmapPath(path);
            emit usmapChanged();
            setStatus(msg);
        }
        emit usmapDownloadDone(ok, msg);
    }, Qt::QueuedConnection);
    connect(m_cns, &CnsConverterService::progress, this,
            [this](const QString &m) { setStatus(m); }, Qt::QueuedConnection);
    m_conflictModel.setTranslator(i18n);
    m_changeModel.setItems(&m_items);
    m_conflictModel.setSource(&m_items, &m_groups);
}

QStringList AppController::cnsReplacementNames() const {
    QString error;
    const QStringList names = m_cns->replacementNames(&error);
    return names;
}

void AppController::runCnsIdFixer(const QUrl &directoryUrl, bool applyFixes) {
    if (m_busy) return;
    const QString directory = directoryUrl.isLocalFile()
        ? directoryUrl.toLocalFile() : directoryUrl.toString();
    setBusy(true, applyFixes ? QStringLiteral("Corrigiendo Container_Id…")
                            : QStringLiteral("Escaneando IDs IoStore…"));
    std::ignore = QtConcurrent::run([this, directory, applyFixes] {
        const auto result = m_cnsIdFixer->run(directory, applyFixes);
        QMetaObject::invokeMethod(this, [this, result, applyFixes] {
            setBusy(false, result.ok
                ? (applyFixes ? QStringLiteral("Corrección de IDs terminada.")
                              : QStringLiteral("Escaneo de IDs terminado."))
                : QStringLiteral("Falló CNS ID Fixer."));
            m_cnsIdFixerReport = result.ok ? result.report : result.error;
            if (!result.ok) emit errorOccurred(result.error);
            emit cnsIdFixerFinished(result.ok);
        }, Qt::QueuedConnection);
    });
}

void AppController::convertCns(const QUrl &inputUrl, const QUrl &outDirUrl,
                               const QString &modName, const QString &mode,
                               const QString &replacementName,
                               const QString &selection) {
    convertCnsBatch({inputUrl}, outDirUrl, modName, mode, replacementName, selection);
}

void AppController::convertCnsBatch(const QList<QUrl> &inputUrls, const QUrl &outDirUrl,
                                    const QString &modName, const QString &mode,
                                    const QString &replacementName,
                                    const QString &selection) {
    if (m_busy || inputUrls.isEmpty()) return;
    const QString outputDir = outDirUrl.isLocalFile() ? outDirUrl.toLocalFile()
                                                      : outDirUrl.toString();
    const auto convertMode = mode.compare(QStringLiteral("replacer"), Qt::CaseInsensitive) == 0
        ? CnsConverterService::Mode::ToReplacer : CnsConverterService::Mode::ToCns;
    QList<CnsConverterService::Request> requests;
    for (const QUrl &url : inputUrls) {
        CnsConverterService::Request request;
        request.inputPath = url.isLocalFile() ? url.toLocalFile() : url.toString();
        request.outputDir = outputDir;
        // Con varias entradas cada mod toma el nombre de su propio archivo; un
        // modName compartido sobrescribiría el mismo ZIP una y otra vez.
        request.modName = inputUrls.size() == 1 ? modName : QString();
        request.mode = convertMode;
        request.replacementName = replacementName;
        request.selection = selection;
        requests << request;
    }
    setBusy(true, QStringLiteral("Preparando conversión CNS…"));
    std::ignore = QtConcurrent::run([this, requests] {
        QList<CnsConverterService::Result> results;
        for (int i = 0; i < requests.size(); ++i) {
            if (requests.size() > 1)
                QMetaObject::invokeMethod(this, [this, i, total = requests.size()] {
                    setBusy(true, QStringLiteral("Convirtiendo mod %1 de %2…")
                                      .arg(i + 1).arg(total));
                }, Qt::QueuedConnection);
            // Un mod que falla no cancela el lote: se reporta al final.
            results << m_cns->convert(requests[i]);
        }
        QMetaObject::invokeMethod(this, [this, results, requests] {
            int okCount = 0;
            for (const auto &result : results) if (result.ok) ++okCount;
            const bool allOk = okCount == results.size();
            setBusy(false, allOk ? QStringLiteral("Conversión CNS terminada.")
                                 : QStringLiteral("Falló la conversión CNS."));

            QSettings settings;
            QJsonArray history;
            const auto stored = QJsonDocument::fromJson(
                settings.value(QStringLiteral("cns/history")).toByteArray());
            if (stored.isArray()) history = stored.array();
            QStringList lines;
            QStringList errors;
            QString lastZip;
            for (int i = 0; i < results.size(); ++i) {
                const auto &result = results[i];
                const QString label = QFileInfo(requests[i].inputPath).completeBaseName();
                if (!result.ok) {
                    errors << QStringLiteral("%1: %2").arg(label, result.error);
                    lines << QStringLiteral("✕ %1: %2").arg(label, result.error);
                    continue;
                }
                lastZip = result.zipPath;
                lines << QStringLiteral("✓ %1 — %2 assets — %3")
                             .arg(label).arg(result.assetsWritten).arg(result.zipPath);
                for (const QString &warning : result.warnings)
                    lines << QStringLiteral("⚠ %1").arg(warning);
                history.prepend(QJsonObject{
                    {QStringLiteral("name"), QFileInfo(result.zipPath).completeBaseName()},
                    {QStringLiteral("zip"), result.zipPath},
                    {QStringLiteral("timestamp"),
                     QDateTime::currentDateTime().toString(Qt::ISODate)},
                    {QStringLiteral("direction"),
                     requests[i].mode == CnsConverterService::Mode::ToCns
                         ? QStringLiteral("Replacer → CNS")
                         : QStringLiteral("CNS → replacer")},
                    {QStringLiteral("assets"), result.assetsWritten}
                });
            }
            m_lastCnsResult = results.size() == 1
                ? (results.first().ok
                       ? QStringLiteral("%1 assets convertidos.\nZIP para Vortex: %2%3")
                             .arg(results.first().assetsWritten).arg(results.first().zipPath,
                                  results.first().warnings.isEmpty()
                                      ? QString()
                                      : QStringLiteral("\n\n⚠ ")
                                            + results.first().warnings.join(QStringLiteral("\n⚠ ")))
                       : results.first().error)
                : QStringLiteral("%1 de %2 mods convertidos.\n%3")
                      .arg(okCount).arg(results.size()).arg(lines.join(QLatin1Char('\n')));
            if (okCount > 0) {
                while (history.size() > 100) history.removeLast();
                settings.setValue(QStringLiteral("cns/history"),
                                  QJsonDocument(history).toJson(QJsonDocument::Compact));
                settings.setValue(QStringLiteral("cns/outputDir"),
                                  QFileInfo(lastZip).absolutePath());
                emit cnsHistoryChanged();
            }
            if (!errors.isEmpty()) emit errorOccurred(errors.join(QLatin1Char('\n')));
            emit cnsConversionFinished(allOk, lastZip);
        }, Qt::QueuedConnection);
    });
}

QString AppController::cnsHistory() const {
    const auto doc = QJsonDocument::fromJson(
        QSettings().value(QStringLiteral("cns/history")).toByteArray());
    return QString::fromUtf8(doc.isArray()
        ? doc.toJson(QJsonDocument::Compact)
        : QByteArrayLiteral("[]"));
}

void AppController::openCnsOutputDir() {
    QSettings settings;
    QString dir = settings.value(QStringLiteral("cns/outputDir")).toString();
    if (dir.isEmpty()) dir = defaultBuildOutDir();
    if (!dir.isEmpty()) openDir(dir);
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
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    // Se encola: arrastrar/elegir varios mods llama addMod en ráfaga y una
    // importación tarda, así que descartar los que llegan mientras hay otra en
    // curso perdía mods en silencio (solo entraba el primero).
    for (const auto &m : m_mods) {
        if (QFileInfo(m.sourcePath) == QFileInfo(path)) {
            emit errorOccurred(t(QStringLiteral("err_mod_exists")));
            return;
        }
    }
    for (const QString &q : m_pendingMods)
        if (QFileInfo(q) == QFileInfo(path)) return;   // ya está en la cola
    m_pendingMods << path;
    if (!m_busy) importNextMod();
}

// Importa el siguiente mod de la cola; se re-llama al terminar cada uno. Mantiene
// busy hasta drenar la cola, para que la UI no parezca lista a medio camino.
void AppController::importNextMod() {
    if (m_pendingMods.isEmpty()) {
        setBusy(false);
        return;
    }
    const QString path = m_pendingMods.takeFirst();
    const int queued = m_pendingMods.size();
    QString status = t(QStringLiteral("core_importing")).arg(QFileInfo(path).fileName());
    if (queued > 0) status += QStringLiteral(" (+%1)").arg(queued);
    setBusy(true, status);
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
            importNextMod();   // seguir con la cola (o marcar no-busy si terminó)
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

QString AppController::ensureVanillaStage(QString *error) {
    if (!GamePaths::hasGame()) {
        if (error) *error = t(QStringLiteral("err_need_game_path"));
        return {};
    }
    const QString paks = GamePaths::paksDir();
    QStringList names;   // contenedores vanilla (sin ~mods/, que es subcarpeta)
    QDirIterator gi(paks, {QStringLiteral("global.*"), QStringLiteral("pakchunk*")}, QDir::Files);
    while (gi.hasNext()) {
        gi.next();
        names << gi.fileName();
    }
    if (names.isEmpty()) {
        if (error) *error = t(QStringLiteral("err_game_not_found"));
        return {};
    }

    // Un stage previo sólo sirve si está COMPLETO. Antes alcanzaba con que
    // existiera global.utoc: una corrida cortada a la mitad (disco lleno, otro
    // volumen) dejaba el stage roto y el fallo se volvía permanente, porque
    // ninguna corrida posterior lo rehacía.
    const QString stage = workRoot() + QStringLiteral("/vanilla_stage");
    bool complete = true;
    for (const QString &n : names) {
        const QFileInfo src(paks + QLatin1Char('/') + n);
        const QFileInfo dst(stage + QLatin1Char('/') + n);
        if (!dst.exists() || dst.size() != src.size()) { complete = false; break; }
    }
    if (complete) return stage;

    QDir(stage).removeRecursively();
    QDir().mkpath(stage);
    for (const QString &n : names) {
        if (hardLink(paks + QLatin1Char('/') + n, stage + QLatin1Char('/') + n)) continue;
        // Sin hardlink (juego en otra unidad que %LOCALAPPDATA%, o carpeta de
        // trabajo sin permisos) se usa la carpeta Paks del juego tal cual:
        // retoc no recorre subcarpetas, así que ~mods/ y LogicMods/ quedan
        // afuera igual que en el stage. Copiar no es opción: son ~100 GB.
        QDir(stage).removeRecursively();
        return paks;
    }
    return stage;
}

QString AppController::vanillaUAssetJsonPath(const QString &tableBase, QString *error) {
    const QString cacheDir = m_baseline->baselineDir() + QStringLiteral("/uasset_json");
    const QString cached = cacheDir + QLatin1Char('/') + tableBase.toLower() + QStringLiteral(".json");
    if (QFileInfo::exists(cached)) return cached;
    const QString stage = ensureVanillaStage(error);
    if (stage.isEmpty()) return {};
    QDir().mkpath(cacheDir);
    const QString legDir = workRoot() + QStringLiteral("/vanilla_extract/") + tableBase;
    QDir(legDir).removeRecursively();
    QString err;
    if (m_pak->toLegacyFiltered(stage, tableBase, legDir, &err) <= 0) {
        if (error)
            *error = err.isEmpty() ? t(QStringLiteral("err_retoc_no_table")).arg(tableBase) : err;
        return {};
    }
    QString uasset;
    QDirIterator li(legDir, {tableBase + QStringLiteral(".uasset")}, QDir::Files,
                    QDirIterator::Subdirectories);
    if (li.hasNext()) uasset = li.next();
    if (uasset.isEmpty()) {
        if (error) *error = t(QStringLiteral("err_table_not_in_game")).arg(tableBase);
        return {};
    }
    if (!m_uasset->toJson(uasset, cached, &err)) {
        QFile::remove(cached);   // no dejar un JSON a medias cacheado
        if (error) *error = err;
        return {};
    }
    return cached;
}

const QHash<QString, QStringList> &AppController::usmapEnums() {
    if (!m_usmapEnumsLoaded) {
        m_usmapEnums = UsmapService::loadEnums(UAssetService::usmapPath());
        m_usmapEnumsLoaded = true;
    }
    return m_usmapEnums;
}

AppController::AnalysisChoices AppController::captureChoices() const {
    AnalysisChoices c;
    for (const ChangeItem &item : m_items) {
        const QString key = item.key() + QLatin1Char('|') + item.modId;
        if (!item.selected) c.selections.insert(key, false);
        if (item.edited) c.edits.insert(key, item.newValue);
    }
    for (const ConflictGroup &g : m_groups)
        if (!g.resolvedModId.isEmpty()) c.resolutions.insert(g.key, g.resolvedModId);
    return c;
}

void AppController::runAnalysis(const AnalysisChoices &choices) {
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
                        QFileInfo(asset.gamePath).completeBaseName(), nullptr);
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
    // Reponer lo elegido antes de buscar conflictos: un valor editado a mano
    // cambia si el cambio choca con otro mod, así que tiene que estar puesto
    // cuando se arman los grupos.
    if (!choices.isEmpty()) {
        for (ChangeItem &item : items) {
            const QString key = item.key() + QLatin1Char('|') + item.modId;
            const auto sel = choices.selections.constFind(key);
            if (sel != choices.selections.constEnd()) item.selected = sel.value();
            const auto edit = choices.edits.constFind(key);
            if (edit != choices.edits.constEnd()) {
                item.newValue = edit.value();
                item.edited = true;
            }
        }
    }

    QList<ConflictGroup> groups = TableDiffEngine::findConflicts(items);
    for (ConflictGroup &g : groups) {
        const auto res = choices.resolutions.constFind(g.key);
        if (res == choices.resolutions.constEnd()) continue;
        // Solo si el mod ganador sigue participando del conflicto: si se quitó,
        // el grupo vuelve a quedar sin resolver en vez de apuntar a la nada.
        for (int idx : g.itemIndexes)
            if (items.at(idx).modId == res.value()) { g.resolvedModId = res.value(); break; }
    }
    for (auto &c : items)
        c.summaryCache = c.summary(m_i18n);   // precalcular para listas grandes
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
    // Baseline bajo demanda: solo las tablas que tocan los mods cargados. Antes
    // se barría el juego entero (~300 paquetes) en el primer import, que es de
    // donde salían los reportes de "tarda media hora".
    QStringList needed;
    for (const ModPackage &mod : m_mods)
        for (const ModAsset &asset : mod.assets)
            if (asset.kind == ModAsset::DataTable && asset.cleanJson)
                needed << QFileInfo(asset.gamePath).completeBaseName();
    needed.removeDuplicates();
    const bool needBaseline = GamePaths::hasGame() && !needed.isEmpty();
    const QString paks = GamePaths::paksDir();
    const QString usmap = m_uasset->usmapPath();
    // Agregar o quitar un mod obliga a re-analizar; sin esto el usuario perdía
    // cada casilla destildada, cada valor editado y cada conflicto resuelto.
    const AnalysisChoices choices = captureChoices();
    std::ignore = QtConcurrent::run([this, needBaseline, paks, usmap, choices, needed] {
        if (needBaseline) {
            QString e; int n = 0;
            const bool ok = m_baseline->ensureTables(paks, usmap, needed, &e, &n);
            QMetaObject::invokeMethod(this, [this, ok, e] {
                emit baselineChanged();
                // Sin esto un fallo (p. ej. mappings faltantes) quedaba mudo y
                // el análisis seguía contra una baseline vacía.
                if (!ok && !e.isEmpty()) emit errorOccurred(e);
            }, Qt::QueuedConnection);
        }
        runAnalysis(choices);
    });
}

void AppController::resolveConflict(int groupId, const QString &modId) {
    for (auto &g : m_groups) {
        if (g.id != groupId) continue;
        g.resolvedModId = modId;
        for (int idx : g.itemIndexes)
            m_items[idx].selected = (m_items.at(idx).modId == modId);
    }
    m_changeModel.refreshSelections();
    m_conflictModel.refreshResolutions();
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
    m_changeModel.refreshSelections();
    m_conflictModel.refreshResolutions();
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
    m_changeModel.refreshSelections();
    m_conflictModel.refreshResolutions();
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
    // Cuánto aporta cada mod: un mod con 0 cambios legibles (pak Zen que no se
    // pudo leer, o idéntico a vanilla) es la causa mas comun de "falta una tabla".
    QHash<QString, int> itemsByMod, selectedByMod;
    for (const ChangeItem &c : m_items) {
        ++itemsByMod[c.modId];
        if (c.selected) ++selectedByMod[c.modId];
    }
    for (const auto &m : m_mods) {
        const int total = itemsByMod.value(m.id);
        const int sel = selectedByMod.value(m.id);
        QString note;
        if (total == 0)
            note = QStringLiteral("  <- NO readable changes (unreadable Zen pak, "
                                  "or identical to vanilla)");
        else if (sel == 0)
            note = QStringLiteral("  <- nothing selected: this mod contributes NOTHING "
                                  "to the merge");
        report << QStringLiteral("  %1. %2  [%3]").arg(m.loadOrder + 1).arg(m.name, m.sourcePath)
               << QStringLiteral("     %1 changes, %2 selected%3").arg(total).arg(sel).arg(note);
    }
    // Un mod Zen puede traer animaciones/AnimBP/mallas además de tablas. Esos
    // assets no se pueden reescribir, así que el pak mergeado lleva SOLO sus
    // tablas: si el usuario desinstala el mod original (como dice el readme),
    // las tablas quedan pero el contenido que referencian desaparece.
    {
        QStringList lines;
        for (const auto &m : m_mods) {
            if (m.zenAssetsNotMerged.isEmpty()) continue;
            QStringList shown = m.zenAssetsNotMerged.mid(0, 8);
            if (m.zenAssetsNotMerged.size() > shown.size())
                shown << QStringLiteral("... (+%1)").arg(m.zenAssetsNotMerged.size() - shown.size());
            lines << QStringLiteral("  %1: %2 non-table assets (%3)")
                         .arg(m.name)
                         .arg(m.zenAssetsNotMerged.size())
                         .arg(shown.join(QStringLiteral(", ")));
        }
        if (!lines.isEmpty()) {
            report << QString()
                   << QStringLiteral("Assets NOT merged (Zen mods: animations, blueprints, meshes):")
                   << lines
                   << QStringLiteral("  Only DataTables can be rewritten, so the merged pak carries "
                                     "the tables of these mods but not the rest.")
                   << QStringLiteral("  Their original paks are copied UNCHANGED into the installable "
                                     "zip, next to the merged one: install both.")
                   << QStringLiteral("  The merged pak loads last (zzz_) and still wins on the tables.");
        }
    }
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
    // Tablas que tienen cambios pero ninguno seleccionado: no llegan al bucle de
    // abajo, asi que sin esta seccion desaparecen del reporte sin explicacion
    // (es lo que se veia como "la tabla del mod no entro en el merge").
    {
        struct TableTally { int total = 0, selected = 0, dups = 0, lostConflict = 0; QString path; };
        QMap<QString, TableTally> tally;
        QHash<int, QString> resolvedBy;
        for (const ConflictGroup &g : m_groups)
            if (!g.resolvedModId.isEmpty()) resolvedBy.insert(g.id, g.resolvedModId);
        for (const ChangeItem &c : m_items) {
            if (c.type == ChangeItem::AssetReplaced) continue;
            TableTally &t = tally[c.tablePath.toLower()];
            t.path = c.tablePath;
            ++t.total;
            if (c.selected) { ++t.selected; continue; }
            if (c.dup) ++t.dups;
            else if (c.conflictGroup >= 0 && resolvedBy.contains(c.conflictGroup)
                     && resolvedBy.value(c.conflictGroup) != c.modId)
                ++t.lostConflict;
        }
        QStringList lines;
        for (const TableTally &t : tally) {
            if (t.selected > 0 || t.total == 0) continue;
            const int unticked = t.total - t.dups - t.lostConflict;
            lines << QStringLiteral("  %1: %2 changes, none selected "
                                    "(%3 lost a conflict, %4 duplicate of another mod, %5 unticked)")
                         .arg(QFileInfo(t.path).completeBaseName())
                         .arg(t.total).arg(t.lostConflict).arg(t.dups).arg(unticked);
        }
        if (!lines.isEmpty()) {
            report << QString()
                   << QStringLiteral("Tables left out (changes exist but none selected):")
                   << lines
                   << QStringLiteral("  These tables are NOT in the merged pak. Keep the mod that "
                                     "owns them enabled, or tick their changes.");
        }
    }

    report << QString() << QStringLiteral("Tables:");
    // Tablas cuyos cambios se saltearon por completo: no se emiten (ver abajo).
    m_lastDroppedTables.clear();
    m_lastFailedTables.clear();

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
    auto realVanilla = [&](const QString &tableBase, QString *why) -> QJsonObject {
        const QString vp = vanillaUAssetJsonPath(tableBase, why);
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
        if (base.isEmpty()) {
            if (verr.isEmpty()) verr = t(QStringLiteral("err_need_game_path"));
            return t(QStringLiteral("err_no_write_base")).arg(gamePath, verr);
        }

        // Antes de tocar nada: sanear los enums con FName numerado que vienen
        // así de vanilla. Si quedan como están, UAssetGUI no puede reescribir
        // la tabla y se pierde entera (pasa con CharacterStanceTable).
        MergeEngine::rewriteNumberedEnums(base, usmapEnums());

        const QString mergedJson = jsonDir + QLatin1Char('/')
            + QString(gamePath).replace(QLatin1Char('/'), QLatin1Char('_')) + QStringLiteral(".json");
        const QString outUasset = contentDir + QLatin1Char('/') + gamePath;
        const QJsonObject vanilla = base;   // base prístina para el reintento
        QString verifyDiff;

        QMetaObject::invokeMethod(this, [this, gamePath] {
            setStatus(t(QStringLiteral("core_generating")).arg(QFileInfo(gamePath).fileName()));
        }, Qt::QueuedConnection);

        // Aplica, escribe el JSON, genera el uasset y verifica el round-trip.
        // Devuelve vacío si salió bien, o el motivo del fallo.
        auto tryBuild = [&](MergeEngine::Result &res) -> QString {
            base = vanilla;
            res = MergeEngine::applyToTable(base, it.value());
            if (!res.ok) return res.error;
            if (res.applied == 0) return {};   // no se escribe nada (ver abajo)
            // RowAdded clean se reconstruye después del saneamiento inicial
            // de la tabla vanilla; canonicalizar también las filas nuevas
            // antes de serializar evita que sus enums vuelvan como null.
            MergeEngine::rewriteNumberedEnums(base, usmapEnums());
            QFile jf(mergedJson);
            if (!jf.open(QIODevice::WriteOnly))
                return tr("No se pudo escribir %1").arg(mergedJson);
            jf.write(QJsonDocument(base).toJson(QJsonDocument::Indented));
            jf.close();
            QString err;
            if (!m_uasset->fromJson(mergedJson, outUasset, &err)) return err;
            const QString verifyJson = mergedJson + QStringLiteral(".verify.json");
            if (!m_uasset->toJson(outUasset, verifyJson, &err))
                return tr("Verificación falló en %1: %2").arg(gamePath, err);
            QFile vf(verifyJson);
            if (!vf.open(QIODevice::ReadOnly))
                return tr("Verificación: no se pudo leer %1").arg(verifyJson);
            QJsonObject verifyRoot = QJsonDocument::fromJson(vf.readAll()).object();
            // UAssetGUI vuelve a leer los enums de FName numerado expandidos
            // ("Valor_3"); del lado escrito son el índice numérico. Pasar la
            // relectura por la misma reescritura deja ambos lados comparables.
            MergeEngine::rewriteNumberedEnums(verifyRoot, usmapEnums());
            // Comparar por VALORES (normalizado): UAssetGUI puede reordenar la
            // metadata de serialización sin cambiar el contenido real.
            // Comparar contra el JSON que realmente se entregó a UAssetGUI.
            // QJsonDocument puede serializar Undefined dentro de arrays como
            // null (o descartar una clave); comparar contra `base` en memoria
            // producía falsos negativos aunque el JSON escrito y su relectura
            // fueran idénticos.
            QFile ef(mergedJson);
            if (!ef.open(QIODevice::ReadOnly))
                return tr("Verificación: no se pudo releer %1").arg(mergedJson);
            const QJsonObject serializedBase =
                QJsonDocument::fromJson(ef.readAll()).object();
            const QJsonArray expectedRows =
                dataTableRows(normalizeDataTableDoc(serializedBase));
            const QJsonArray actualRows = dataTableRows(normalizeDataTableDoc(verifyRoot));
            if (!jsonValueEquals(expectedRows, actualRows)) {
                verifyDiff = firstJsonDifference(expectedRows, actualRows,
                                                 QStringLiteral("DataTable.Data"));
                return tr("no round-tripea fiel");
            }
            return {};
        };

        MergeEngine::Result res;
        const QString buildErr = tryBuild(res);
        if (!buildErr.isEmpty()) {
            // Una tabla problemática no debe abortar ni invalidar las demás.
            // Se elimina cualquier salida parcial y se deja fuera del contenedor;
            // el aviso final indica que el mod de origen debe seguir habilitado.
            QFile::remove(outUasset);
            const QString stem = outUasset.left(outUasset.size() - 7);
            QFile::remove(stem + QStringLiteral(".uexp"));
            QFile::remove(stem + QStringLiteral(".ubulk"));
            m_lastFailedTables << tableBase;
            report << QStringLiteral("  %1: EXCLUDED — verification failed: %2")
                          .arg(tableBase, buildErr);
            if (!verifyDiff.isEmpty())
                report << QStringLiteral("    first difference: %1").arg(verifyDiff);
            continue;
        }
        m_lastSkipped += res.skipped;
        report << QStringLiteral("  %1: %2 applied, %3 skipped")
                      .arg(tableBase).arg(res.applied).arg(res.skipped);

        // Nada aplicado = la tabla quedaría idéntica a vanilla. Como el pak
        // mergeado carga con máxima prioridad (zzz_), escribirla PISARÍA con
        // vanilla la tabla del mod original. No emitirla es siempre mejor:
        // así el mod de origen sigue mandando en esa tabla.
        if (res.applied == 0) {
            m_lastDroppedTables << tableBase;
            report << QStringLiteral("    -> not written (would have been vanilla, "
                                     "overriding the source mod)");
        }
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
           << QStringLiteral("Skipped = array/object changes from Zen-read mods that don't")
           << QStringLiteral("round-trip reliably. Numbers, text and enums are merged.");
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
        // Los assets no tabulares de un mod Zen (animaciones, AnimBP, mallas) no
        // se pueden reescribir, pero tampoco hacen falta reescribir: el merge no
        // los toca. Se copia el pak original SIN MODIFICAR al lado del mergeado,
        // así el zip queda completo y desinstalar el mod de origen es seguro.
        // El mergeado carga después (zzz_) y sigue ganando en las tablas.
        QStringList bundled;
        for (const auto &m : m_mods) {
            if (m.zenAssetsNotMerged.isEmpty()) continue;
            const QFileInfo fi(m.sourcePath);
            if (fi.suffix().compare(QLatin1String("pak"), Qt::CaseInsensitive) != 0) continue;
            for (const QString &ext : {QStringLiteral("pak"), QStringLiteral("ucas"),
                                       QStringLiteral("utoc")}) {
                const QString src = fi.absolutePath() + QLatin1Char('/')
                                    + fi.completeBaseName() + QLatin1Char('.') + ext;
                if (QFileInfo::exists(src))
                    QFile::copy(src, zipStage + QStringLiteral("/Paks/")
                                         + QFileInfo(src).fileName());
            }
            bundled << QStringLiteral("- %1").arg(fi.completeBaseName());
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
            // Si hubo paks incluidos sin modificar, el zip ya trae todo: hay que
            // decir que esos archivos extra son parte del paquete, no un descuido.
            if (!bundled.isEmpty()) {
                readme.write(tr("\nEste zip incluye ademas, SIN MODIFICAR, los paks de:\n%1\n"
                                "Traen animaciones/blueprints/mallas ademas de tablas, y eso no se\n"
                                "puede reescribir dentro del pak mergeado. Copialos junto con el\n"
                                "mergeado: sin ellos las tablas quedan sin el contenido que\n"
                                "referencian y el mod deja de funcionar. El pak mergeado carga\n"
                                "ultimo (zzz_) y sigue ganando en las tablas.\n")
                                 .arg(bundled.join(QLatin1Char('\n')))
                                 .toUtf8());
            }
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
            // Conservar contexto suficiente para diagnosticar fallos de merge.
            // No se incluyen JSON completos ni assets del juego en este log.
            QString mergeLogPath;
            const QString logDir = QStandardPaths::writableLocation(
                                       QStandardPaths::AppLocalDataLocation)
                                 + QStringLiteral("/logs");
            QDir().mkpath(logDir);
            mergeLogPath = logDir + QStringLiteral("/merge_%1.log")
                                     .arg(QDateTime::currentDateTime()
                                              .toString(QStringLiteral("yyyyMMdd_HHmmss")));
            QFile mergeLog(mergeLogPath);
            if (mergeLog.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                QTextStream log(&mergeLog);
                log << "Stellar Tool merge diagnostic\n"
                    << "Timestamp: " << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n"
                    << "Output directory: " << outDir << "\n"
                    << "Analyzed mods:\n";
                for (const ModPackage &mod : m_mods)
                    log << "  - " << mod.name << " | " << mod.sourcePath << "\n";
                log << "Selected changes: " << m_items.size() << "\n"
                    << "Failed tables: " << m_lastFailedTables.join(", ") << "\n"
                    << "Dropped tables: " << m_lastDroppedTables.join(", ") << "\n"
                    << "Result: " << (error.isEmpty() ? "success" : "failure") << "\n"
                    << "Error: " << (error.isEmpty() ? "(none)" : error) << "\n";
                const QString reportPath = outDir + QStringLiteral("/merge_report.txt");
                QFile report(reportPath);
                if (report.open(QIODevice::ReadOnly))
                    log << "\nMerge report:\n" << QString::fromUtf8(report.readAll()) << "\n";
            } else {
                mergeLogPath.clear();
            }
            if (error.isEmpty()) {
                m_lastMergeOk = true;
                m_lastMergeResult = (m_exportZip ? t(QStringLiteral("merge_ok_zip"))
                                                 : t(QStringLiteral("merge_ok"))).arg(outDir);
                if (m_lastSkipped > 0)
                    m_lastMergeResult += QStringLiteral(" ") + t(QStringLiteral("merge_skipped_note")).arg(m_lastSkipped);
                // Aviso destacado: esas tablas quedaron fuera del pak, así que
                // el mod que las traía tiene que seguir habilitado.
                if (!m_lastDroppedTables.isEmpty())
                    m_lastMergeResult += QStringLiteral("\n\n⚠ ")
                        + t(QStringLiteral("merge_dropped_note"))
                              .arg(m_lastDroppedTables.join(QStringLiteral(", ")));
                // Distinto motivo, distinto aviso: acá el merge sí tenía cambios
                // para escribir pero el uasset no verificó.
                if (!m_lastFailedTables.isEmpty())
                    m_lastMergeResult += QStringLiteral("\n\n⚠ ")
                        + t(QStringLiteral("merge_failed_note"))
                              .arg(m_lastFailedTables.join(QStringLiteral(", ")));
            } else {
                m_lastMergeOk = false;
                m_lastMergeResult = t(QStringLiteral("merge_err")).arg(error);
                const QString diagnostic = mergeLogPath.isEmpty()
                    ? error
                    : error + QStringLiteral("\n\nDiagnóstico: ") + mergeLogPath;
                emit errorOccurred(diagnostic);
            }
            emit mergeFinished();
            setBusy(false);
        }, Qt::QueuedConnection);
    });
}

QString AppController::gamePath() const { return GamePaths::gameRoot(); }
bool AppController::hasGamePath() const { return GamePaths::hasGame(); }
QString AppController::defaultOutDir() const { return GamePaths::modsDir(); }

QString AppController::defaultBuildOutDir() const {
    QString base = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (base.isEmpty())
        base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (base.isEmpty()) return {};
    return QDir::toNativeSeparators(base + QStringLiteral("/StellarTool/builds"));
}

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

QString AppController::themeMode() const {
    QSettings s;
    const QString mode = s.value(QStringLiteral("themeMode"), QStringLiteral("dark")).toString();
    return mode == QLatin1String("light") || mode == QLatin1String("oled")
        ? mode : QStringLiteral("dark");
}

void AppController::setThemeMode(const QString &mode) {
    if (mode != QLatin1String("light") && mode != QLatin1String("dark")
        && mode != QLatin1String("oled"))
        return;
    QSettings s;
    if (s.value(QStringLiteral("themeMode"), QStringLiteral("dark")).toString() == mode)
        return;
    s.setValue(QStringLiteral("themeMode"), mode);
    emit themeModeChanged();
}

bool AppController::setGamePath(const QUrl &dirUrl) {
    const QString dir = dirUrl.isLocalFile() ? dirUrl.toLocalFile() : dirUrl.toString();
    // Se acepta la raiz o cualquier subcarpeta/padre reconocible; una ruta que
    // no sea el juego no pisa la que ya estaba guardada.
    const QString root = GamePaths::normalizeRoot(dir);
    if (root.isEmpty()) {
        emit errorOccurred(t(QStringLiteral("err_game_not_found")));
        return false;
    }
    GamePaths::setGameRoot(root);
    PakService::resetOodleCache();   // la DLL sale del juego: cambia con la ruta
    emit gamePathChanged();
    emit oodleChanged();
    return true;
}

QString AppController::usmapPath() const { return UAssetService::usmapPath(); }

bool AppController::usmapIsCustom() const {
    return !UAssetService::customUsmapPath().isEmpty();
}

void AppController::setUsmapPath(const QUrl &fileUrl) {
    const QString f = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    UAssetService::setCustomUsmapPath(f);
    emit usmapChanged();
}

void AppController::clearUsmapPath() {
    UAssetService::setCustomUsmapPath(QString());
    emit usmapChanged();
}

QString AppController::oodlePath() const { return PakService::oodleFilePath(); }

bool AppController::oodleIsCustom() const { return !PakService::userOodlePath().isEmpty(); }

bool AppController::setOodlePath(const QUrl &fileUrl) {
    const QString f = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    // Elegir otro DLL no sirve: retoc/cue4parse cargan este nombre exacto.
    if (QFileInfo(f).fileName().toLower() != QLatin1String("oo2core_9_win64.dll")) {
        emit errorOccurred(t(QStringLiteral("err_oodle_bad_file")));
        return false;
    }
    PakService::setUserOodlePath(f);
    emit oodleChanged();
    return true;
}

void AppController::clearOodlePath() {
    PakService::setUserOodlePath(QString());
    emit oodleChanged();
}

void AppController::convertSaveToJson(const QUrl &inputUrl, const QUrl &outputUrl, int indent) {
    if (m_busy) return;
    const QString input = inputUrl.toLocalFile();
    const QString output = outputUrl.toLocalFile();
    setBusy(true, QStringLiteral("Convirtiendo partida a JSON…"));
    std::ignore = QtConcurrent::run([this, input, output, indent] {
        const auto result = m_saveConverter->toJson(input, output, indent);
        QMetaObject::invokeMethod(this, [this, result] {
            m_saveConverterResult = result.ok ? result.message : result.message;
            if (!result.ok) emit errorOccurred(result.message);
            setBusy(false, result.ok ? QStringLiteral("Partida convertida a JSON.")
                                     : QStringLiteral("Falló la conversión de la partida."));
            emit saveConverterFinished(result.ok, result.outputPath);
        }, Qt::QueuedConnection);
    });
}

void AppController::convertJsonToSave(const QUrl &inputUrl, const QUrl &outputUrl) {
    if (m_busy) return;
    const QString input = inputUrl.toLocalFile();
    const QString output = outputUrl.toLocalFile();
    setBusy(true, QStringLiteral("Convirtiendo JSON a partida…"));
    std::ignore = QtConcurrent::run([this, input, output] {
        const auto result = m_saveConverter->fromJson(input, output);
        QMetaObject::invokeMethod(this, [this, result] {
            m_saveConverterResult = result.message;
            if (!result.ok) emit errorOccurred(result.message);
            setBusy(false, result.ok ? QStringLiteral("Partida restaurada desde JSON.")
                                     : QStringLiteral("Falló la conversión de la partida."));
            emit saveConverterFinished(result.ok, result.outputPath);
        }, Qt::QueuedConnection);
    });
}

void AppController::fixSave(const QUrl &inputUrl) {
    if (m_busy) return;
    const QString input = inputUrl.toLocalFile();
    setBusy(true, QStringLiteral("Reparando partida CNS…"));
    std::ignore = QtConcurrent::run([this, input] {
        const auto result = m_saveConverter->fix(input);
        QMetaObject::invokeMethod(this, [this, result, input] {
            m_saveConverterResult = result.message;
            if (!result.ok) emit errorOccurred(result.message);
            setBusy(false, result.ok ? QStringLiteral("Reparación CNS terminada.")
                                     : QStringLiteral("Falló la reparación CNS."));
            emit saveConverterFinished(result.ok, input);
        }, Qt::QueuedConnection);
    });
}

void AppController::refreshOodle() {
    // No cambiar la preferencia manual: el usuario puede volver a la detección
    // automática con el botón correspondiente. Esto solo invalida el resultado
    // cacheado, útil después de reparar/verificar los archivos del juego.
    PakService::resetOodleCache();
    emit oodleChanged();
}

QString AppController::detectedGameVersion() const {
    return UsmapService::detectGameVersion();
}

void AppController::downloadUsmap(const QString &version) {
    if (m_downloadingUsmap) return;
    QString ver = version.trimmed();
    if (ver.isEmpty()) ver = UsmapService::detectGameVersion();
    m_downloadingUsmap = true;
    emit usmapDownloadChanged();
    m_usmap->downloadForVersion(ver);
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

void AppController::analyzeMovesets(const QUrl &sourceUrl, const QUrl &gameUrl) {
    if (m_busy || m_movesetAnalyzing) return;
    const QString source = sourceUrl.isLocalFile() ? sourceUrl.toLocalFile() : sourceUrl.toString();
    const QString game = gameUrl.isLocalFile() && !gameUrl.toLocalFile().isEmpty()
            ? gameUrl.toLocalFile() : GamePaths::gameRoot();
    if (source.isEmpty() || !QFileInfo(source).isDir()) {
        emit errorOccurred(t(QStringLiteral("err_moveset_source")));
        return;
    }
    if (game.isEmpty() || !QFileInfo(QDir(game).filePath(QStringLiteral("SB/Content/Paks"))).isDir()) {
        emit errorOccurred(t(QStringLiteral("err_moveset_game")));
        return;
    }
    const QString builderDir = QFileInfo(builderScript()).absolutePath();
    const QString script = QDir(builderDir).filePath(QStringLiteral("moveset_catalog.py"));
    const QString out = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                       + QStringLiteral("/movesets/catalog.json");
    QDir().mkpath(QFileInfo(out).absolutePath());
    m_movesetAnalyzing = true;
    emit movesetAnalyzingChanged();
    setBusy(true, t(QStringLiteral("builder_moveset_analyzing")));
    std::ignore = QtConcurrent::run([this, source, game, script, out] {
        QProcess proc;
        proc.setProgram(pythonExe());
        proc.setArguments({script, QStringLiteral("--source"), source,
                           QStringLiteral("--game"), game,
                           QStringLiteral("--out"), out});
        applyBuilderEnv(proc, game);
        proc.start();
        const bool started = proc.waitForStarted(30000);
        const bool finished = started && proc.waitForFinished(15 * 60 * 1000);
        const int code = proc.exitCode();
        const QString detail = QString::fromUtf8(proc.readAllStandardError()).trimmed();
        const bool ok = finished && proc.exitStatus() == QProcess::NormalExit
                        && code == 0 && QFileInfo::exists(out);
        QMetaObject::invokeMethod(this, [this, ok, out, detail] {
            m_movesetAnalyzing = false;
            emit movesetAnalyzingChanged();
            if (ok) {
                QFile f(out);
                const QJsonObject catalog = f.open(QIODevice::ReadOnly)
                        ? QJsonDocument::fromJson(f.readAll()).object() : QJsonObject();
                m_movesetCatalogPath = out;
                m_movesetChangeModel.setCatalog(catalog);
                emit movesetCatalogChanged();
                emit movesetCatalogFinished(true, out);
                setStatus(t(QStringLiteral("builder_moveset_analyzed"))
                          .arg(catalog.value(QStringLiteral("summary")).toObject()
                               .value(QStringLiteral("changes")).toInt()));
            } else {
                emit movesetCatalogFinished(false, detail.isEmpty()
                    ? t(QStringLiteral("err_moveset_analysis")) : detail);
            }
            setBusy(false);
        }, Qt::QueuedConnection);
    });
}

static QString builderScript() {
    // El Builder vive dentro de Stellar Tool (junto al exe: <appDir>/Builder, o
    // en el repo). Override por env STELLAR_SOULS_BUILDER.
    QString dir = qEnvironmentVariable("STELLAR_SOULS_BUILDER");
    if (dir.isEmpty()) {
        const QString appDir = QCoreApplication::applicationDirPath();
        for (const QString &cand : {appDir + QStringLiteral("/Builder"),
                                    appDir + QStringLiteral("/../Builder"),
                                    QStringLiteral("C:/Users/cristian/Documents/Stellar Tool/Builder")}) {
            if (QFileInfo::exists(cand + QStringLiteral("/compiler/build_custom.py"))) { dir = cand; break; }
        }
        if (dir.isEmpty()) dir = QStringLiteral("C:/Users/cristian/Documents/Stellar Tool/Builder");
    }
    return dir + QStringLiteral("/compiler/build_custom.py");
}

// Interprete Python: el embebido junto al Builder si existe, si no el del sistema.
static QString pythonExe() {
    const QString builderDir = QFileInfo(builderScript()).absolutePath() + QStringLiteral("/..");
    const QString embed = QDir(builderDir).filePath(QStringLiteral("pyembed/python.exe"));
    return QFileInfo::exists(embed) ? embed : QStringLiteral("python");
}

// El Builder resuelve el juego por su cuenta (gamepaths.py) para extraer
// baselines vanilla escribibles; sin esto, una instalacion fuera de Steam no
// le llega aunque el usuario la haya elegido en la app.
// Ademas fuerza UTF-8 en el Python del Builder: leemos su salida como UTF-8, y
// con el locale por defecto (cp936 en Windows chino) los textos acentuados o las
// rutas no-ASCII rompian el proceso al imprimir.
static void applyBuilderEnv(QProcess &proc, const QString &gameDir) {
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PYTHONUTF8"), QStringLiteral("1"));
    env.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
    const QString root = gameDir.isEmpty() ? GamePaths::gameRoot() : gameDir;
    if (!root.isEmpty())
        env.insert(QStringLiteral("STELLARBLADE_DIR"), QDir::toNativeSeparators(root));
    // El Builder repite la busqueda de Oodle por su cuenta; si la app ya la
    // resolvio (o el usuario la eligio a mano) se le pasa hecha.
    const QString oodle = PakService::oodleFilePath();
    if (!oodle.isEmpty())
        env.insert(QStringLiteral("STELLAR_OODLE_DIR"), QDir::toNativeSeparators(oodle));
    proc.setProcessEnvironment(env);
}

// Corre python build_custom.py con args de forma sincrona; devuelve stdout.
static QString runBuilderSync(const QStringList &extraArgs, int *exitCode = nullptr) {
    QProcess proc;
    proc.setProgram(pythonExe());
    proc.setArguments(QStringList{builderScript()} + extraArgs);
    applyBuilderEnv(proc);
    proc.start();
    proc.waitForFinished(600000);
    if (exitCode) *exitCode = proc.exitCode();
    return QString::fromUtf8(proc.readAllStandardOutput());
}

bool AppController::parseBuilderProgress(const QString &line, QString *table, int *step) {
    if (!line.startsWith(QStringLiteral("PROGRESS "))) return false;
    const QStringList f = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    // PROGRESS <kind> <n> <detalle>; hoy sólo 'baseline' tiene texto en la UI.
    if (f.size() < 4 || f.at(1) != QStringLiteral("baseline")) return false;
    bool ok = false;
    const int n = f.at(2).toInt(&ok);
    if (!ok) return false;
    if (step) *step = n;
    // El detalle puede traer espacios: se toma todo lo que sigue al contador.
    if (table) *table = f.mid(3).join(QLatin1Char(' '));
    return true;
}

void AppController::runBuilder(const QString &answersJson, const QUrl &outDirUrl,
                              bool installPaks, bool installHelper, const QString &gameDir) {
    if (m_busy) return;
    const QString outDir = outDirUrl.isLocalFile() ? outDirUrl.toLocalFile() : outDirUrl.toString();
    const QString script = builderScript();
    setBusy(true, t(QStringLiteral("builder_compiling")));
    std::ignore = QtConcurrent::run([this, answersJson, outDir, script, installPaks, installHelper, gameDir] {
        QString answersPath = QDir(QDir::tempPath()).filePath(QStringLiteral("ss_builder_answers.json"));
        { QFile f(answersPath); if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(answersJson.toUtf8()); }
        QStringList args{script, QStringLiteral("--out"), outDir,
                         QStringLiteral("--answers"), QStringLiteral("@") + answersPath};
        if (installPaks) args << QStringLiteral("--install-paks");
        if (installHelper) args << QStringLiteral("--install-helper");
        const QString game = gameDir.isEmpty() ? GamePaths::gameRoot() : gameDir;
        if (!game.isEmpty()) args << QStringLiteral("--game") << game;
        QProcess proc;
        proc.setProgram(pythonExe());
        proc.setArguments(args);
        applyBuilderEnv(proc, game);
        // Extraer una baseline vanilla escanea todos los contenedores del juego:
        // son minutos por tabla. El builder emite "PROGRESS <kind> <n> <detalle>"
        // y se lee en caliente para que el paso largo se vea en la UI.
        QString pending;
        QString collected;
        QObject::connect(&proc, &QProcess::readyReadStandardOutput,
                         &proc, [this, &proc, &pending, &collected] {
            pending += QString::fromUtf8(proc.readAllStandardOutput());
            int nl;
            while ((nl = pending.indexOf(QLatin1Char('\n'))) >= 0) {
                const QString line = pending.left(nl).trimmed();
                pending.remove(0, nl + 1);
                collected += line + QLatin1Char('\n');
                QString table;
                int step = 0;
                if (!parseBuilderProgress(line, &table, &step)) continue;
                const QString msg = t(QStringLiteral("builder_baseline_progress"))
                                        .arg(table).arg(step);
                QMetaObject::invokeMethod(this, [this, msg] { setBusy(true, msg); },
                                          Qt::QueuedConnection);
            }
        });
        proc.start();
        // El pid habilita Cancelar en la UI; cancelBuild() mata ese arbol.
        if (proc.waitForStarted(30000)) {
            m_buildPid.store(proc.processId());
            QMetaObject::invokeMethod(this, [this] { emit cancellableChanged(); }, Qt::QueuedConnection);
        }
        proc.waitForFinished(600000);
        m_buildPid.store(0);
        const bool cancelled = m_buildCancelled.exchange(false);
        // El build murio de golpe: no pudo limpiar nada. El diario que dejo
        // permite reponer la instalacion previa y borrar la salida a medias.
        if (cancelled) runBuilderSync({QStringLiteral("--rollback")});
        // El lector en caliente ya consumio el stdout: la cola es lo que quedo
        // sin newline final mas lo que llego despues del ultimo readyRead.
        const QString out = collected + pending
                            + QString::fromUtf8(proc.readAllStandardOutput());
        const QString err = QString::fromUtf8(proc.readAllStandardError());
        const bool timedOut = proc.state() != QProcess::NotRunning;
        const QString diagnostic = QStringLiteral("builder_%1.log")
                                       .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
        const QString logPath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                              + QStringLiteral("/logs/") + diagnostic;
        QDir().mkpath(QFileInfo(logPath).absolutePath());
        {
            QFile lf(logPath);
            if (lf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                lf.write("STDOUT:\n"); lf.write(out.toUtf8());
                lf.write("\nSTDERR:\n"); lf.write(err.toUtf8());
                lf.write(QByteArray::number(proc.exitCode()));
                lf.write(timedOut ? "\nTIMEOUT\n" : "\n");
            }
        }
        QString zip;
        for (const QString &line : out.split(QLatin1Char('\n')))
            if (line.startsWith(QStringLiteral("OK -> "))) zip = line.mid(6).trimmed();
        const bool ok = proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0 && !zip.isEmpty();
        QMetaObject::invokeMethod(this, [this, ok, zip, err, cancelled, timedOut, logPath] {
            emit cancellableChanged();
            // Cancelar no es un error: el build muere a proposito y el usuario
            // ya sabe por que, asi que no se abre el dialogo de error.
            if (cancelled) { setStatus(t(QStringLiteral("builder_cancelled"))); emit buildCancelled(); }
            else if (ok) { setStatus(t(QStringLiteral("builder_done"))); emit builderFinished(zip); }
            else {
                QString detail = err.trimmed();
                if (detail.isEmpty()) detail = QStringLiteral("El proceso no produjo stderr.");
                const QString prefix = timedOut ? QStringLiteral("El build excedió el tiempo límite.\n") : QString();
                emit errorOccurred(prefix + detail + QStringLiteral("\n\nLog: ") + logPath);
            }
            setBusy(false);
        }, Qt::QueuedConnection);
    });
}

void AppController::cancelBuild() {
    const qint64 pid = m_buildPid.load();
    if (pid == 0) return;
    m_buildCancelled.store(true);
    setStatus(t(QStringLiteral("builder_cancelling")));
    // Matar solo el python deja vivos a repak/retoc/UAssetGUI, que siguen
    // escribiendo en la carpeta de salida. /T se lleva el arbol entero.
#ifdef Q_OS_WIN
    QProcess::startDetached(QStringLiteral("taskkill"),
                            {QStringLiteral("/PID"), QString::number(pid),
                             QStringLiteral("/T"), QStringLiteral("/F")});
#else
    QProcess::startDetached(QStringLiteral("kill"),
                            {QStringLiteral("-9"), QString::number(pid)});
#endif
}

QString AppController::detectStellarBlade() {
    // La ruta guardada (elegida en Ajustes) manda sobre la autodeteccion.
    const QString saved = GamePaths::gameRoot();
    if (!saved.isEmpty() && QFileInfo::exists(saved + QStringLiteral("/SB/Content/Paks")))
        return saved;
    QProcess proc;
    proc.setProgram(pythonExe());
    const QString gp = QFileInfo(builderScript()).absolutePath() + QStringLiteral("/gamepaths.py");
    proc.setArguments({gp});
    applyBuilderEnv(proc);
    proc.start();
    proc.waitForFinished(30000);
    const QString out = QString::fromUtf8(proc.readAllStandardOutput());
    const QString first = out.split(QLatin1Char('\n')).value(0).trimmed();
    return first == QStringLiteral("NO ENCONTRADO") ? QString() : first;
}

QString AppController::builderHistory() {
    const QString gp = QFileInfo(builderScript()).absolutePath() + QStringLiteral("/history.py");
    QProcess proc;
    proc.setProgram(pythonExe());
    proc.setArguments({QStringLiteral("-c"),
        QStringLiteral("import sys,json;sys.path.insert(0,r'%1');import history;print(json.dumps(history.list_records()))")
            .arg(QFileInfo(builderScript()).absolutePath())});
    applyBuilderEnv(proc);
    proc.start();
    proc.waitForFinished(30000);
    return QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
}

QString AppController::builderTemplate(const QString &id) {
    QProcess proc;
    proc.setProgram(pythonExe());
    proc.setArguments({QStringLiteral("-c"),
        QStringLiteral("import sys,json;sys.path.insert(0,r'%1');import history;print(json.dumps(history.as_template('%2') or {}))")
            .arg(QFileInfo(builderScript()).absolutePath(), id)});
    applyBuilderEnv(proc);
    proc.start();
    proc.waitForFinished(30000);
    return QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
}

QString AppController::builderPresets() const {
    QSettings settings;
    const QByteArray raw = settings.value(QStringLiteral("builder/presets"), QByteArray("[]")).toByteArray();
    const QJsonDocument doc = QJsonDocument::fromJson(raw);
    return doc.isArray() ? QString::fromUtf8(doc.toJson(QJsonDocument::Compact))
                         : QStringLiteral("[]");
}

bool AppController::saveBuilderPreset(const QString &name, const QString &answersJson) {
    const QString cleanName = name.trimmed();
    const QJsonDocument answers = QJsonDocument::fromJson(answersJson.toUtf8());
    if (cleanName.isEmpty() || !answers.isObject())
        return false;

    QJsonArray presets = QJsonDocument::fromJson(builderPresets().toUtf8()).array();
    QJsonObject preset{
        {QStringLiteral("name"), cleanName},
        {QStringLiteral("answers"), answers.object()}
    };
    bool replaced = false;
    for (qsizetype i = 0; i < presets.size(); ++i) {
        if (presets.at(i).toObject().value(QStringLiteral("name")).toString()
                .compare(cleanName, Qt::CaseInsensitive) == 0) {
            presets.replace(i, preset);
            replaced = true;
            break;
        }
    }
    if (!replaced)
        presets.prepend(preset);
    QSettings().setValue(QStringLiteral("builder/presets"),
                         QJsonDocument(presets).toJson(QJsonDocument::Compact));
    return true;
}

namespace {
// Archivo de preset del Builder. El formato se declara adentro para que un JSON
// cualquiera no entre por tener extensión .stpreset, y la versión de esquema
// permite rechazar con un mensaje claro un archivo de una versión más nueva.
constexpr int kPresetSchemaVersion = 1;
const QLatin1String kPresetFormat("stellartool.builder-preset");

QString presetResult(bool ok, const QString &key, const QString &value) {
    QJsonObject out{{QStringLiteral("ok"), ok}};
    out.insert(key, value);
    return QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact));
}
}   // namespace

QString AppController::exportBuilderPreset(const QString &name, const QUrl &fileUrl) {
    const QJsonArray presets = QJsonDocument::fromJson(builderPresets().toUtf8()).array();
    QJsonObject preset;
    for (const QJsonValue &v : presets) {
        if (v.toObject().value(QStringLiteral("name")).toString() == name) {
            preset = v.toObject();
            break;
        }
    }
    if (preset.isEmpty())
        return presetResult(false, QStringLiteral("error"), t(QStringLiteral("err_preset_missing")));

    QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    if (QFileInfo(path).suffix().isEmpty())
        path += QStringLiteral(".stpreset");
    const QJsonObject doc{
        {QStringLiteral("format"), QString(kPresetFormat)},
        {QStringLiteral("schemaVersion"), kPresetSchemaVersion},
        {QStringLiteral("appVersion"), QCoreApplication::applicationVersion()},
        {QStringLiteral("exported"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {QStringLiteral("name"), name},
        {QStringLiteral("answers"), preset.value(QStringLiteral("answers")).toObject()},
    };
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return presetResult(false, QStringLiteral("error"),
                            t(QStringLiteral("err_preset_write")).arg(path));
    f.write(QJsonDocument(doc).toJson(QJsonDocument::Indented));
    f.close();
    return presetResult(true, QStringLiteral("path"), path);
}

QString AppController::importBuilderPreset(const QUrl &fileUrl) {
    const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return presetResult(false, QStringLiteral("error"),
                            t(QStringLiteral("err_preset_read")).arg(path));
    const QJsonObject doc = QJsonDocument::fromJson(f.readAll()).object();
    f.close();
    if (doc.value(QStringLiteral("format")).toString() != kPresetFormat
            || !doc.value(QStringLiteral("answers")).isObject())
        return presetResult(false, QStringLiteral("error"), t(QStringLiteral("err_preset_format")));
    const int schema = doc.value(QStringLiteral("schemaVersion")).toInt();
    if (schema > kPresetSchemaVersion)
        return presetResult(false, QStringLiteral("error"), t(QStringLiteral("err_preset_newer")));

    // El nombre del archivo es una sugerencia: si ya existe un preset así, se
    // importa al lado en vez de pisar el que el usuario ya tenía guardado.
    QString name = doc.value(QStringLiteral("name")).toString().trimmed();
    if (name.isEmpty()) name = QFileInfo(path).completeBaseName();
    QStringList taken;
    const QJsonArray presets = QJsonDocument::fromJson(builderPresets().toUtf8()).array();
    for (const QJsonValue &v : presets)
        taken << v.toObject().value(QStringLiteral("name")).toString().toLower();
    QString unique = name;
    for (int i = 2; taken.contains(unique.toLower()); ++i)
        unique = QStringLiteral("%1 (%2)").arg(name).arg(i);

    const QString answers = QString::fromUtf8(
        QJsonDocument(doc.value(QStringLiteral("answers")).toObject())
            .toJson(QJsonDocument::Compact));
    if (!saveBuilderPreset(unique, answers))
        return presetResult(false, QStringLiteral("error"), t(QStringLiteral("err_preset_format")));
    return presetResult(true, QStringLiteral("name"), unique);
}

void AppController::deleteBuilderPreset(const QString &name) {
    QJsonArray presets = QJsonDocument::fromJson(builderPresets().toUtf8()).array();
    for (qsizetype i = presets.size(); i-- > 0;) {
        if (presets.at(i).toObject().value(QStringLiteral("name")).toString() == name)
            presets.removeAt(i);
    }
    QSettings().setValue(QStringLiteral("builder/presets"),
                         QJsonDocument(presets).toJson(QJsonDocument::Compact));
}

QString AppController::installedStatus() {
    return runBuilderSync({QStringLiteral("--installed-status")}).trimmed();
}

void AppController::uninstallMod() {
    if (m_busy) return;
    setBusy(true, t(QStringLiteral("builder_uninstalling")));
    std::ignore = QtConcurrent::run([this] {
        int code = 0;
        runBuilderSync({QStringLiteral("--uninstall-paks")}, &code);
        QMetaObject::invokeMethod(this, [this] {
            setStatus(t(QStringLiteral("builder_uninstalled"))); emit uninstalled(); setBusy(false);
        }, Qt::QueuedConnection);
    });
}

void AppController::uninstallHelper() {
    if (m_busy) return;
    setBusy(true, t(QStringLiteral("builder_uninstalling")));
    std::ignore = QtConcurrent::run([this] {
        int code = 0;
        runBuilderSync({QStringLiteral("--uninstall-helper")}, &code);
        QMetaObject::invokeMethod(this, [this] {
            setStatus(t(QStringLiteral("builder_uninstalled"))); emit uninstalled(); setBusy(false);
        }, Qt::QueuedConnection);
    });
}

void AppController::reexportBuild(const QString &id, const QUrl &outDirUrl) {
    if (m_busy) return;
    const QString outDir = outDirUrl.isLocalFile() ? outDirUrl.toLocalFile() : outDirUrl.toString();
    setBusy(true, t(QStringLiteral("builder_compiling")));
    std::ignore = QtConcurrent::run([this, id, outDir] {
        int code = 0;
        const QString out = runBuilderSync({QStringLiteral("--reexport"), id, QStringLiteral("--out"), outDir}, &code);
        QString zip;
        for (const QString &line : out.split(QLatin1Char('\n')))
            if (line.startsWith(QStringLiteral("OK -> "))) zip = line.mid(6).trimmed();
        QMetaObject::invokeMethod(this, [this, zip] {
            if (!zip.isEmpty()) { setStatus(t(QStringLiteral("builder_done"))); emit builderFinished(zip); }
            else emit errorOccurred(QStringLiteral("reexport failed"));
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

static QString tableBaseOf(const QString &tablePath) {
    return tablePath.section(QLatin1Char('/'), -1).section(QLatin1Char('.'), 0, 0);
}

static bool patchValueAt(const QJsonValue &node, const QStringList &parts, int depth,
                         QStringList *path, QJsonValue *value) {
    if (depth >= parts.size()) { if (value) *value = node; return true; }
    if (node.isArray()) {
        const QJsonArray a = node.toArray();
        for (const QJsonValue &entry : a) {
            const QJsonObject o = entry.toObject();
            if (o.value(QStringLiteral("Name")).toString() != parts.at(depth)) continue;
            if (path) path->append(QStringLiteral("K:") + parts.at(depth));
            const bool ok = patchValueAt(o.value(QStringLiteral("Value")), parts, depth + 1, path, value);
            if (!ok && path) path->removeLast();
            return ok;
        }
        return false;
    }
    if (node.isObject()) {
        const QJsonObject o = node.toObject();
        if (!o.contains(parts.at(depth))) return false;
        if (path) path->append(QStringLiteral("K:") + parts.at(depth));
        const bool ok = patchValueAt(o.value(parts.at(depth)), parts, depth + 1, path, value);
        if (!ok && path) path->removeLast();
        return ok;
    }
    return false;
}

void AppController::exportTomlPatches(const QUrl &dirUrl, bool reveal) {
    const QString dir = dirUrl.isLocalFile() ? dirUrl.toLocalFile() : dirUrl.toString();
    if (dir.isEmpty()) return;
    QDir().mkpath(dir);

    // Agrupar cambios seleccionados escalares por tabla -> fila -> líneas.
    QMap<QString, QMap<QString, QStringList>> byTable;
    int count = 0;
    for (const ChangeItem &c : m_items) {
        if (!c.selected || c.dup) continue;
        if (c.type != ChangeItem::Modified) continue;
        const QJsonValue &v = c.newValue;
        if (!(v.isDouble() || v.isBool() || v.isString())) continue;
        QString line = c.displayPath() + QStringLiteral(" = ") + TomlPatch::valueLiteral(v);
        if (c.baseValue.isDouble() || c.baseValue.isBool() || c.baseValue.isString())
            line += QStringLiteral("    # ") + TomlPatch::valueLiteral(c.baseValue);
        byTable[tableBaseOf(c.tablePath)][c.rowName] << line;
        ++count;
    }
    if (byTable.isEmpty()) {
        emit errorOccurred(t(QStringLiteral("toml_export_empty")));
        return;
    }
    for (auto t1 = byTable.constBegin(); t1 != byTable.constEnd(); ++t1) {
        QFile f(dir + QLatin1Char('/') + t1.key() + QStringLiteral(".toml"));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) continue;
        QTextStream ts(&f);
        ts << "# " << t1.key() << " - Stellar Tool patch export\n"
              "[meta]\n"
            << "table = \"" << t1.key() << "\"\n"
              "game = \"Stellar Blade\"\n\n";
        for (auto r = t1.value().constBegin(); r != t1.value().constEnd(); ++r) {
            ts << '[' << r.key() << "]\n";
            for (const QString &l : r.value()) ts << l << '\n';
            ts << '\n';
        }
        f.close();
    }
    setStatus(t(QStringLiteral("toml_export_done")).arg(count).arg(byTable.size()));
    if (reveal) openDir(dir);
}

void AppController::importTomlPatch(const QUrl &fileUrl) {
    const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        emit errorOccurred(t(QStringLiteral("toml_import_read_fail")));
        return;
    }
    const QString text = QString::fromUtf8(f.readAll());
    f.close();
    const TomlPatch::Document document = TomlPatch::parseDocument(text, path);
    if (!document.errors.isEmpty()) {
        emit errorOccurred(document.errors.join(QStringLiteral("\n")));
        return;
    }
    if (document.rules.isEmpty()) {
        emit errorOccurred(t(QStringLiteral("toml_import_empty")));
        return;
    }
    const QFileInfo fi(path);
    const QString tableBase = document.table.isEmpty() ? fi.completeBaseName()
        : QFileInfo(document.table).completeBaseName();
    // Ruta canónica de las DataTables de SB (igual que el import de mods Zen).
    const QString tablePath = QStringLiteral("SB/Content/Local/Data/") + tableBase
                              + QStringLiteral(".uasset");
    const QString modName = fi.fileName();
    const QString modId = shortHash(path);

    QJsonObject baseRoot;
    QString why;
    const QString vanillaPath = vanillaUAssetJsonPath(tableBase, &why);
    if (!vanillaPath.isEmpty()) {
        QFile vf(vanillaPath);
        if (vf.open(QIODevice::ReadOnly)) baseRoot = QJsonDocument::fromJson(vf.readAll()).object();
    }
    if (baseRoot.isEmpty()) baseRoot = m_baseline->tableFor(tablePath);
    const QJsonObject normalized = baseRoot.isEmpty() ? QJsonObject() : normalizeDataTableDoc(baseRoot);
    const QJsonArray baseRows = dataTableRows(normalized);
    const bool needsBase = std::any_of(document.rules.cbegin(), document.rules.cend(), [](const TomlPatch::Rule &r) {
        return !r.rowRegex.isEmpty() || r.operation != TomlPatch::Operation::Set;
    });
    if (needsBase && baseRows.isEmpty()) {
        emit errorOccurred(QStringLiteral("El patch usa regex u operaciones, pero no hay baseline escribible para %1.").arg(tableBase));
        return;
    }

    int added = 0;
    for (const TomlPatch::Rule &rule : document.rules) {
        QRegularExpression rx;
        if (!rule.rowRegex.isEmpty()) {
            rx.setPattern(rule.rowRegex);
            if (!rx.isValid()) { emit errorOccurred(QStringLiteral("Regex inválido en línea %1: %2").arg(rule.line).arg(rx.errorString())); return; }
        }
        if (!baseRows.isEmpty()) {
            for (const QJsonValue &rv : baseRows) {
                const QJsonObject row = rv.toObject(); const QString rowName = row.value(QStringLiteral("Name")).toString();
                if ((!rule.row.isEmpty() && rowName != rule.row) || (!rule.rowRegex.isEmpty() && !rx.match(rowName).hasMatch())) continue;
                QStringList propPath; QJsonValue oldValue;
                if (!patchValueAt(row.value(QStringLiteral("Value")), rule.property.split('.'), 0, &propPath, &oldValue)) continue;
                QJsonValue next; QString opError;
                if (!rule.expected.isUndefined() && !jsonValueEquals(oldValue, rule.expected)) {
                    emit errorOccurred(QStringLiteral("%1, fila %2, línea %3: el valor esperado no coincide").arg(rule.property, rowName).arg(rule.line, 0, 10)); return;
                }
                if (!TomlPatch::applyOperation(rule.operation, oldValue, rule.value, rule.minValue, rule.maxValue, &next, &opError)) {
                    emit errorOccurred(QStringLiteral("%1, fila %2, línea %3: %4").arg(rule.property, rowName).arg(rule.line, 0, 10).arg(opError)); return;
                }
                ChangeItem c;
                c.modId = modId; c.modName = modName; c.tablePath = tablePath; c.rowName = rowName;
                c.type = ChangeItem::Modified; c.propPath = propPath; c.baseValue = oldValue; c.newValue = next; c.selected = true;
                c.summaryCache = c.summary(m_i18n); m_items << c; ++added;
            }
        } else if (!rule.row.isEmpty() && rule.rowRegex.isEmpty() && rule.operation == TomlPatch::Operation::Set) {
            ChangeItem c;
            c.modId = modId;
            c.modName = modName;
            c.tablePath = tablePath;
            c.rowName = rule.row;
            c.type = ChangeItem::Modified;
            // Clave con puntos -> segmentos K: (path anidado).
            for (const QString &seg : rule.property.split(QLatin1Char('.'), Qt::SkipEmptyParts))
                c.propPath << (QStringLiteral("K:") + seg);
            c.newValue = rule.value;
            c.clean = false;      // literal: se escribe tal cual (incluye strings)
            c.selected = true;
            c.summaryCache = c.summary(m_i18n);
            m_items << c;
            ++added;
        }
    }
    if (added == 0) { emit errorOccurred(QStringLiteral("El patch no encontró filas o propiedades aplicables en %1.").arg(tableBase)); return; }
    // Registrar un "mod" liviano para que aparezca en la lista y el merge lo use.
    ModPackage pkg;
    pkg.id = modId;
    pkg.name = modName;
    pkg.sourcePath = path;
    pkg.loadOrder = m_mods.size();
    m_mods << pkg;

    m_groups = TableDiffEngine::findConflicts(m_items);
    m_analyzed = true;
    m_modModel.setMods(m_mods);
    m_changeModel.refresh();
    m_conflictModel.refresh();
    emit analysisChanged();
    setStatus(t(QStringLiteral("toml_import_done")).arg(added, 0, 10).arg(tableBase));
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
            AnalysisChoices choices;
            choices.selections = state.selections;
            choices.resolutions = state.resolutions;
            std::ignore = QtConcurrent::run([this, choices] { runAnalysis(choices); });
        }, Qt::QueuedConnection);
    });
}

} // namespace st
