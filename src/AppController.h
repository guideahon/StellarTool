#pragma once

#include "core/ModTypes.h"
#include "models/ModListModel.h"
#include "models/ChangeListModel.h"
#include "models/ConflictModel.h"

#include <QObject>
#include <QFutureWatcher>

#include <atomic>

namespace st {

class PakService;
class UAssetService;
class UsmapService;
class Cue4Service;
class CnsConverterService;
class CnsIdFixerService;
class ModImporter;
class BaselineManager;
class ProjectStore;
class Translator;

class AppController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    Q_PROPERTY(bool hasBaseline READ hasBaseline NOTIFY baselineChanged)
    Q_PROPERTY(bool baselineStale READ baselineStale NOTIFY baselineChanged)
    Q_PROPERTY(QString gamePath READ gamePath NOTIFY gamePathChanged)
    Q_PROPERTY(bool hasGamePath READ hasGamePath NOTIFY gamePathChanged)
    Q_PROPERTY(QString usmapPath READ usmapPath NOTIFY usmapChanged)
    Q_PROPERTY(bool usmapIsCustom READ usmapIsCustom NOTIFY usmapChanged)
    Q_PROPERTY(QString detectedGameVersion READ detectedGameVersion NOTIFY gamePathChanged)
    Q_PROPERTY(bool downloadingUsmap READ downloadingUsmap NOTIFY usmapDownloadChanged)
    Q_PROPERTY(bool advancedMode READ advancedMode WRITE setAdvancedMode NOTIFY advancedModeChanged)
    Q_PROPERTY(QString themeMode READ themeMode WRITE setThemeMode NOTIFY themeModeChanged)
    Q_PROPERTY(bool toolsAvailable READ toolsAvailable CONSTANT)
    Q_PROPERTY(QString toolsError READ toolsError CONSTANT)
    Q_PROPERTY(st::ModListModel *modModel READ modModel CONSTANT)
    Q_PROPERTY(st::ChangeListModel *changeModel READ changeModel CONSTANT)
    Q_PROPERTY(st::ConflictModel *conflictModel READ conflictModel CONSTANT)
    Q_PROPERTY(bool analyzed READ analyzed NOTIFY analysisChanged)
    Q_PROPERTY(QString lastMergeResult READ lastMergeResult NOTIFY mergeFinished)
    Q_PROPERTY(bool lastMergeOk READ lastMergeOk NOTIFY mergeFinished)
    Q_PROPERTY(bool exportZip READ exportZip WRITE setExportZip NOTIFY exportZipChanged)
    // Hay un build en curso que se puede cortar (solo runBuilder: es el único
    // proceso largo del que guardamos el pid).
    Q_PROPERTY(bool cancellable READ cancellable NOTIFY cancellableChanged)
    Q_PROPERTY(QString lastCnsResult READ lastCnsResult NOTIFY cnsConversionFinished)
    Q_PROPERTY(QString cnsHistoryJson READ cnsHistory NOTIFY cnsHistoryChanged)
    Q_PROPERTY(QString cnsIdFixerReport READ cnsIdFixerReport NOTIFY cnsIdFixerFinished)
public:
    explicit AppController(Translator *i18n, QObject *parent = nullptr);
    ~AppController() override;

    bool busy() const { return m_busy; }
    QString statusText() const { return m_statusText; }
    bool hasBaseline() const;
    bool baselineStale() const;
    bool toolsAvailable() const;
    QString toolsError() const;
    bool analyzed() const { return m_analyzed; }
    QString lastMergeResult() const { return m_lastMergeResult; }
    bool lastMergeOk() const { return m_lastMergeOk; }
    bool exportZip() const { return m_exportZip; }
    bool cancellable() const { return m_buildPid.load() != 0; }
    // Mata el árbol del build en curso (python + repak/retoc hijos). No-op si
    // no hay build corriendo.
    Q_INVOKABLE void cancelBuild();
    void setExportZip(bool v) { if (m_exportZip != v) { m_exportZip = v; emit exportZipChanged(); } }

    ModListModel *modModel() { return &m_modModel; }
    ChangeListModel *changeModel() { return &m_changeModel; }
    ConflictModel *conflictModel() { return &m_conflictModel; }

    Q_INVOKABLE void addMod(const QUrl &url);
    Q_INVOKABLE void removeMod(int row);
    Q_INVOKABLE void clearMods();
    Q_INVOKABLE void analyze();
    Q_INVOKABLE void resolveConflict(int groupId, const QString &modId);
    Q_INVOKABLE void resolveAllWithMod(const QString &modId);
    Q_INVOKABLE void resolveAllByPriority(); // gana el mod con menor loadOrder

    // Acceso de solo lectura (headless / tests).
    const QList<ModPackage> &mods() const { return m_mods; }
    const QList<ChangeItem> &items() const { return m_items; }
    const QList<ConflictGroup> &groups() const { return m_groups; }
    Q_INVOKABLE void merge(const QUrl &outDirUrl);
    Q_INVOKABLE void importBaseline(const QUrl &dirUrl);
    // Stellar Souls Builder: compila un mod personalizado desde respuestas del
    // cuestionario (JSON). QProcess -> python Builder/compiler/build_custom.py.
    // installPaks/installHelper = instalar directo en el juego (aprobado por el
    // usuario en la UI). gameDir vacio = autodeteccion.
    Q_INVOKABLE void runBuilder(const QString &answersJson, const QUrl &outDirUrl,
                                bool installPaks = false, bool installHelper = false,
                                const QString &gameDir = {});
    // Autodeteccion de la instalacion de Stellar Blade (vacio si no se encuentra).
    Q_INVOKABLE QString detectStellarBlade();
    // Historial de configs: JSON array [{id,label,timestamp,zip,answers}].
    Q_INVOKABLE QString builderHistory();
    // Respuestas (JSON) de una config previa, para usar como plantilla.
    Q_INVOKABLE QString builderTemplate(const QString &id);
    // Presets nombrados del Builder, persistidos en QSettings.
    Q_INVOKABLE QString builderPresets() const;
    Q_INVOKABLE bool saveBuilderPreset(const QString &name, const QString &answersJson);
    Q_INVOKABLE void deleteBuilderPreset(const QString &name);
    // Re-exporta el zip de una config previa a outDir (copia o recompila).
    Q_INVOKABLE void reexportBuild(const QString &id, const QUrl &outDirUrl);
    // Estado de lo instalado por la tool: JSON {game, paks:[...], helper:bool}.
    Q_INVOKABLE QString installedStatus();
    // Desinstala (aprobado por el usuario) SOLO lo que la tool instalo.
    Q_INVOKABLE void uninstallMod();
    Q_INVOKABLE void uninstallHelper();
    Q_INVOKABLE void buildBaselineFromGame();
    QString gamePath() const;
    bool hasGamePath() const;
    Q_INVOKABLE bool setGamePath(const QUrl &dirUrl);  // false si no es el juego
    // Override del usmap (mappings). Vacío = usar el bundled. Para actualizar
    // mappings tras un parche del juego sin esperar una nueva release.
    QString usmapPath() const;
    bool usmapIsCustom() const;
    Q_INVOKABLE void setUsmapPath(const QUrl &fileUrl);
    Q_INVOKABLE void clearUsmapPath();
    // Auto-descarga del usmap para una versión del juego desde el archivo de la
    // comunidad. Vacío = usar detectedGameVersion. Async: emite usmapDownloadDone.
    QString detectedGameVersion() const;
    bool downloadingUsmap() const { return m_downloadingUsmap; }
    Q_INVOKABLE void downloadUsmap(const QString &version);
    Q_INVOKABLE QString defaultOutDir() const;   // <juego>/~mods si hay juego, si no vacío
    // Salida del builder: fuera del juego. Deja stage/ y compile*/ junto al ZIP
    // y ~mods se carga recursivamente, así que ~mods como default dejaba el
    // botón Compilar deshabilitado (check_out_dir la rechaza).
    Q_INVOKABLE QString defaultBuildOutDir() const;
    Q_INVOKABLE void openDir(const QString &path); // abre la carpeta en el Explorador
    // Mods cargados cuyo origen vive dentro de ~mods (candidatos a desactivar).
    Q_INVOKABLE int disableableSourceCount() const;
    // Mueve esos orígenes a ~mods/disabled/. Devuelve cuántos movió.
    Q_INVOKABLE int disableSourceMods();
    bool advancedMode() const;
    void setAdvancedMode(bool v);
    QString themeMode() const;
    void setThemeMode(const QString &mode);
    Q_INVOKABLE void saveProject(const QUrl &fileUrl);
    Q_INVOKABLE void loadProject(const QUrl &fileUrl);
    // Exporta los cambios seleccionados como patches TOML legibles (uno por
    // tabla) en 'dirUrl'. Formato tipo automod: [Fila] / Prop = valor  # viejo.
    Q_INVOKABLE void exportTomlPatches(const QUrl &dirUrl);
    // Importa un patch TOML como un "mod" de cambios literales (row/prop=value).
    Q_INVOKABLE void importTomlPatch(const QUrl &fileUrl);
    Q_INVOKABLE QStringList unresolvedConflictTitles() const;
    // Conversor nativo de outfits. mode: "cns" o "replacer".
    Q_INVOKABLE QStringList cnsReplacementNames() const;
    Q_INVOKABLE void convertCns(const QUrl &inputUrl, const QUrl &outDirUrl,
                                const QString &modName, const QString &mode,
                                const QString &replacementName = {},
                                const QString &selection = {});
    QString lastCnsResult() const { return m_lastCnsResult; }
    Q_INVOKABLE QString cnsHistory() const;
    Q_INVOKABLE void openCnsOutputDir();
    Q_INVOKABLE void runCnsIdFixer(const QUrl &directoryUrl, bool applyFixes);
    QString cnsIdFixerReport() const { return m_cnsIdFixerReport; }

signals:
    void busyChanged();
    void statusChanged();
    void baselineChanged();
    void gamePathChanged();
    void usmapChanged();
    void usmapDownloadChanged();
    void usmapDownloadDone(bool ok, const QString &message);
    void advancedModeChanged();
    void themeModeChanged();
    void analysisChanged();
    void mergeFinished();
    void builderFinished(const QString &zipPath);
    void uninstalled();
    void exportZipChanged();
    void cancellableChanged();
    void buildCancelled();
    void cnsConversionFinished(bool ok, const QString &outputPath);
    void cnsHistoryChanged();
    void cnsIdFixerFinished(bool ok);
    void errorOccurred(const QString &message);

private:
    void setBusy(bool b, const QString &status = {});
    void setStatus(const QString &s);
    QString t(const QString &key) const;   // traducción vía Translator
    QString workRoot() const;
    void importNextMod();                    // drena m_pendingMods de a uno
    // Lo que el usuario decidió sobre un análisis y no debe perderse al volver
    // a analizar (agregar/quitar un mod obliga a re-analizar).
    struct AnalysisChoices {
        QMap<QString, bool> selections;        // ChangeItem::key + "|" + modId
        QMap<QString, QJsonValue> edits;       // misma clave -> valor editado a mano
        QMap<QString, QString> resolutions;    // ConflictGroup::key -> modId ganador
        bool isEmpty() const {
            return selections.isEmpty() && edits.isEmpty() && resolutions.isEmpty();
        }
    };
    AnalysisChoices captureChoices() const;

    void runAnalysis(const AnalysisChoices &choices = {}); // en worker thread
    QString runMerge(const QString &outDir); // en worker thread; devuelve error o vacío
    // Carpeta con SOLO los contenedores vanilla, lista para retoc (hardlinks;
    // si no se puede linkear, la carpeta Paks del juego tal cual).
    QString ensureVanillaStage(QString *error = nullptr);
    // JSON UAssetGUI real de la tabla vanilla (cacheado en disco); vacío si no
    // se pudo obtener, con el motivo real en *error.
    QString vanillaUAssetJsonPath(const QString &tableBase, QString *error = nullptr);
    // Enums del usmap (nombre -> valores en orden), leídos una sola vez.
    const QHash<QString, QStringList> &usmapEnums();

    Translator *m_i18n;
    PakService *m_pak;
    UAssetService *m_uasset;
    UsmapService *m_usmap;
    Cue4Service *m_cue4;
    CnsConverterService *m_cns;
    CnsIdFixerService *m_cnsIdFixer;
    ModImporter *m_importer;
    BaselineManager *m_baseline;
    ProjectStore *m_store;

    QStringList m_pendingMods;   // cola de imports (drag&drop / multi-selección)
    QList<ModPackage> m_mods;
    QList<ChangeItem> m_items;
    QList<ConflictGroup> m_groups;

    ModListModel m_modModel;
    ChangeListModel m_changeModel;
    ConflictModel m_conflictModel;

    bool m_busy = false;
    bool m_analyzed = false;
    bool m_exportZip = true;
    bool m_lastMergeOk = false;
    bool m_downloadingUsmap = false;
    QString m_statusText;
    QString m_lastMergeResult;
    QString m_lastCnsResult;
    QString m_cnsIdFixerReport;
    QHash<QString, QStringList> m_usmapEnums;
    bool m_usmapEnumsLoaded = false;
    // pid del build en curso (0 = ninguno) y si el usuario lo canceló: los lee
    // el hilo de UI mientras el worker corre el QProcess.
    std::atomic<qint64> m_buildPid{0};
    std::atomic<bool> m_buildCancelled{false};
    QStringList m_lastDroppedTables;  // tablas no emitidas (0 cambios aplicados)
    QStringList m_lastFailedTables;   // tablas excluidas por fallar la verificación
    int m_lastSkipped = 0;
};

} // namespace st
