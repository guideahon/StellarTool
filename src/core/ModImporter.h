#pragma once

#include "ModTypes.h"
#include <QObject>

namespace st {

class PakService;
class UAssetService;
class Cue4Service;
class Translator;

// Ingesta: zip/carpeta/pak -> ModPackage con assets clasificados.
// Sincrónico; correr en worker thread (QtConcurrent desde AppController).
class ModImporter : public QObject {
    Q_OBJECT
public:
    ModImporter(PakService *pak, UAssetService *uasset, Cue4Service *cue4,
                Translator *i18n, QObject *parent = nullptr);

    // workRoot: dir base donde extraer (un subdir por mod).
    ModPackage import(const QString &sourcePath, const QString &workRoot, QString *error);

signals:
    void progress(const QString &message);

private:
    bool stageSource(const QString &sourcePath, const QString &modWorkDir,
                     ModPackage &pkg, QString *contentDir, QString *error);
    bool unpackPakInto(const QString &pak, const QString &extractDir,
                       const QString &modWorkDir, ModPackage &pkg, QString *error);
    // Lee las DataTables de un contenedor Zen con CUE4Parse (requiere ruta del
    // juego). Agrega ModAsset DataTable a pkg. Devuelve cantidad de tablas.
    int importZenTables(const QString &utoc, const QString &modWorkDir,
                        ModPackage &pkg, QString *error);
    void scanAssets(ModPackage &pkg);

    QString t(const QString &key) const;

    PakService *m_pak;
    UAssetService *m_uasset;
    Cue4Service *m_cue4;
    Translator *m_i18n;
};

} // namespace st
