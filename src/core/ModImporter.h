#pragma once

#include "ModTypes.h"
#include <QObject>

namespace st {

class PakService;
class UAssetService;
class Translator;

// Ingesta: zip/carpeta/pak -> ModPackage con assets clasificados.
// Sincrónico; correr en worker thread (QtConcurrent desde AppController).
class ModImporter : public QObject {
    Q_OBJECT
public:
    ModImporter(PakService *pak, UAssetService *uasset, Translator *i18n, QObject *parent = nullptr);

    // workRoot: dir base donde extraer (un subdir por mod).
    ModPackage import(const QString &sourcePath, const QString &workRoot, QString *error);

signals:
    void progress(const QString &message);

private:
    bool stageSource(const QString &sourcePath, const QString &modWorkDir,
                     QString *contentDir, QString *error);
    bool unpackPakInto(const QString &pak, const QString &extractDir, QString *error);
    void scanAssets(ModPackage &pkg);

    QString t(const QString &key) const;

    PakService *m_pak;
    UAssetService *m_uasset;
    Translator *m_i18n;
};

} // namespace st
