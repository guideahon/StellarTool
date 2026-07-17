#pragma once

#include "core/ModTypes.h"
#include "models/ModListModel.h"
#include "models/ChangeListModel.h"
#include "models/ConflictModel.h"

#include <QObject>
#include <QFutureWatcher>

namespace st {

class PakService;
class UAssetService;
class ModImporter;
class BaselineManager;
class ProjectStore;

class AppController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    Q_PROPERTY(bool hasBaseline READ hasBaseline NOTIFY baselineChanged)
    Q_PROPERTY(bool toolsAvailable READ toolsAvailable CONSTANT)
    Q_PROPERTY(QString toolsError READ toolsError CONSTANT)
    Q_PROPERTY(st::ModListModel *modModel READ modModel CONSTANT)
    Q_PROPERTY(st::ChangeListModel *changeModel READ changeModel CONSTANT)
    Q_PROPERTY(st::ConflictModel *conflictModel READ conflictModel CONSTANT)
    Q_PROPERTY(bool analyzed READ analyzed NOTIFY analysisChanged)
    Q_PROPERTY(QString lastMergeResult READ lastMergeResult NOTIFY mergeFinished)
    Q_PROPERTY(bool exportZip READ exportZip WRITE setExportZip NOTIFY exportZipChanged)
public:
    explicit AppController(QObject *parent = nullptr);
    ~AppController() override;

    bool busy() const { return m_busy; }
    QString statusText() const { return m_statusText; }
    bool hasBaseline() const;
    bool toolsAvailable() const;
    QString toolsError() const;
    bool analyzed() const { return m_analyzed; }
    QString lastMergeResult() const { return m_lastMergeResult; }
    bool exportZip() const { return m_exportZip; }
    void setExportZip(bool v) { if (m_exportZip != v) { m_exportZip = v; emit exportZipChanged(); } }

    ModListModel *modModel() { return &m_modModel; }
    ChangeListModel *changeModel() { return &m_changeModel; }
    ConflictModel *conflictModel() { return &m_conflictModel; }

    Q_INVOKABLE void addMod(const QUrl &url);
    Q_INVOKABLE void removeMod(int row);
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
    Q_INVOKABLE void saveProject(const QUrl &fileUrl);
    Q_INVOKABLE void loadProject(const QUrl &fileUrl);
    Q_INVOKABLE QStringList unresolvedConflictTitles() const;

signals:
    void busyChanged();
    void statusChanged();
    void baselineChanged();
    void analysisChanged();
    void mergeFinished();
    void exportZipChanged();
    void errorOccurred(const QString &message);

private:
    void setBusy(bool b, const QString &status = {});
    void setStatus(const QString &s);
    QString workRoot() const;
    void runAnalysis();                      // en worker thread
    QString runMerge(const QString &outDir); // en worker thread; devuelve error o vacío

    PakService *m_pak;
    UAssetService *m_uasset;
    ModImporter *m_importer;
    BaselineManager *m_baseline;
    ProjectStore *m_store;

    QList<ModPackage> m_mods;
    QList<ChangeItem> m_items;
    QList<ConflictGroup> m_groups;

    ModListModel m_modModel;
    ChangeListModel m_changeModel;
    ConflictModel m_conflictModel;

    bool m_busy = false;
    bool m_analyzed = false;
    bool m_exportZip = true;
    QString m_statusText;
    QString m_lastMergeResult;
};

} // namespace st
