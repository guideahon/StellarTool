#pragma once

#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QSet>
#include <QStringList>

namespace st {

class Cue4Service;
class PakService;
class UAssetService;

// Conversor nativo de outfits Stellar Blade entre replacer y CNS.
// La lógica es un port C++ del proyecto MIT StellarBladeCNSRepacker de Deron Fer.
// Los binarios UE siguen pasando por PakService/UAssetService: aquí sólo se
// planifican dependencias y se reescriben referencias del JSON UAssetAPI.
class CnsConverterService : public QObject {
    Q_OBJECT
public:
    enum class Mode { ToCns, ToReplacer };

    struct Request {
        QString inputPath;
        QString outputDir;
        QString modName;
        Mode mode = Mode::ToCns;
        QString replacementName;
        QString selection; // nombre CNS o índice (1-based); vacío = primera variante
        bool checkSaveData = false;
    };
    struct Result {
        bool ok = false;
        QString outputDir;
        QString descriptorPath;
        QString utocPath;
        QString zipPath;
        QString error;
        QStringList warnings;
        int assetsWritten = 0;
    };

    explicit CnsConverterService(PakService *pak, UAssetService *uasset,
                                 Cue4Service *cue4 = nullptr, QObject *parent = nullptr);

    Result convert(const Request &request);
    QStringList replacementNames(QString *error = nullptr) const;
    static QString dataRoot();

    // Helpers puros expuestos para pruebas de regresión.
    static QJsonObject relocateAssetJson(const QJsonObject &asset,
                                         const QMap<QString, QString> &relocations);
    static QString normalizeAssetPath(const QString &path);
    static QString shortId(const QString &text);

signals:
    void progress(const QString &message);

private:
    struct AssetInfo {
        QString id;
        QString path;
        QString characterId;
        QString fitMeshType;
        QString meshSubType;
        QString iconObjectPath;
        QString animationBPPath;
        QString requirementDLC;
        QString ponyPhysics;
        bool forLongHair = false;
        QString name;
    };

    bool loadData(QString *error) const;
    bool prepareInput(const QString &input, const QString &stage,
                      QString *assetsDir, QString *error) const;
    QString assetPathForFile(const QString &file, const QString &assetsDir) const;
    QString fileForAsset(const QString &path, const QString &assetsDir,
                         const QString &extension = QStringLiteral("uasset")) const;
    QMap<QString, AssetInfo> readCnsDescriptors(const QString &inputRoot,
                                                QString *error) const;
    QStringList discoverRoots(const QStringList &moddedAssets,
                              const QMap<QString, AssetInfo> &cnsInfos) const;
    bool writeRelocatedAsset(const QString &sourceUasset, const QString &targetUasset,
                             const QMap<QString, QString> &relocations,
                             QString *error) const;

    // Un replacer suele cambiar la piel pisando las TEXTURAS que lee un material
    // vanilla (p. ej. MI_P_EVE_09_Skin), no el material. Relocalizadas bajo
    // /Game/CNSRepacked/<id>/ quedan huérfanas y el outfit se ve con la piel
    // vanilla. CNS resuelve esto con "Materials" + "Parameters" en el descriptor:
    // overrides que aplica sólo mientras el outfit está equipado. Estos helpers
    // reconstruyen esa lista leyendo los slots reales de la malla y los
    // parámetros de textura de cada material vanilla.

    // Carpeta montable por CUE4Parse con los contenedores vanilla (hardlink) +
    // el contenedor recién empaquetado. Hay que borrarla al terminar.
    QString stageWithGame(const QString &utocPath, const QString &workDir) const;
    // Slots de material de la malla ya relocalizada, en orden (MaterialIndex).
    // Devuelve "/Game/…/MI_X.MI_X". Vacío si no se puede leer.
    QStringList meshMaterialSlots(const QString &mountDir, const QString &meshPath,
                                  const QString &workDir) const;
    // Para cada slot vanilla, redirige a la copia relocalizada las texturas que
    // el mod pisaba. Devuelve las entradas "Parameters" y marca como usadas las
    // rutas que quedaron cubiertas.
    QJsonArray textureParameterOverrides(const QString &mountDir,
                                         const QStringList &slotList,
                                         const QMap<QString, QString> &relocations,
                                         const QString &workDir,
                                         QSet<QString> *covered) const;

    PakService *m_pak;
    UAssetService *m_uasset;
    Cue4Service *m_cue4;
    mutable bool m_loaded = false;
    mutable QMap<QString, QSet<QString>> m_assetToRoots;
    mutable QMap<QString, QSet<QString>> m_assetToImports;
    mutable QMap<QString, AssetInfo> m_rootInfos;
    mutable QMap<QString, QList<AssetInfo>> m_replacerInfos;
    mutable QSet<QString> m_excluded;
};

} // namespace st
