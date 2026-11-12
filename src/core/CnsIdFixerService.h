#pragma once

#include <QObject>
#include <QList>
#include <QStringList>

namespace st {

class PakService;

class CnsIdFixerService : public QObject {
    Q_OBJECT
public:
    struct IntegrityIssue {
        QString path;
        QString detail;
        bool repairableHeaderHash = false;
    };
    struct HeaderHashRepair {
        QString path;
        bool repaired = false;
        QString detail;
    };
    struct RepairResult {
        QString backupDirectory;
        QList<HeaderHashRepair> outcomes;
        int repairedContainers = 0;
    };

    struct Result {
        bool ok = false;
        int containers = 0;
        int duplicateContainers = 0;
        int fixedContainers = 0;
        int packageConflicts = 0;
        int sharedPackageReferences = 0;
        int vanillaPackageConflicts = 0;
        int vanillaPackageReferences = 0;
        QStringList sharedPackageContainerPaths;
        QStringList vanillaPackageContainerPaths;
        QStringList repairableHeaderHashPaths;
        QList<IntegrityIssue> integrityIssues;
        QString report;
        QString error;
    };

    explicit CnsIdFixerService(PakService *pak = nullptr, QObject *parent = nullptr);

    // Escanea .utoc recursivamente. Si fixDuplicates es true, conserva el
    // primer Container_Id y asigna IDs determinísticos a los duplicados.
    // También devuelve los contenedores implicados en Package_Id compartidos
    // entre mods y en colisiones con los contenedores vanilla de Paks. El
    // reporte resuelve rutas de asset y asocia descriptores CNS a cada source.
    // Cada escritura crea antes <archivo>.cnsidfixer.bak.
    Result run(const QString &directory, bool fixDuplicates,
               bool verifyContents = false,
               const QString &vanillaPaksDirectory = {}) const;
    bool packageIdsMatch(const QString &firstUtoc, const QString &secondUtoc,
                         QString *error = nullptr) const;
    RepairResult repairHeaderHashMismatches(const QString &directory,
                                             const QStringList &containerPaths) const;

private:
    struct Container;
    static bool readContainer(const QString &path, Container *out, QString *error);
    static bool patchContainer(const Container &container, quint64 newId, QString *error);
    static QString idText(quint64 id);
    PakService *m_pak = nullptr;
};

} // namespace st
