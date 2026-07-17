#pragma once

#include "ModTypes.h"
#include <QObject>

namespace st {

// Cache de tablas vanilla en %LOCALAPPDATA%/StellarTool/baseline/.
// Sin baseline la app funciona en modo degradado (diff solo entre mods).
class UAssetService;

class BaselineManager : public QObject {
    Q_OBJECT
public:
    explicit BaselineManager(UAssetService *uasset, QObject *parent = nullptr);

    QString baselineDir() const;
    bool hasBaseline() const;

    // JSON de la tabla vanilla para un gamePath; objeto vacío si no está.
    QJsonObject tableFor(const QString &gamePath) const;

    // Importa un dir con tablas vanilla a la cache: acepta JSONs de UAssetGUI
    // y/o .uasset legacy (se convierten con UAssetService).
    bool importFromDir(const QString &dir, QString *error, int *imported);

private:
    QString keyFor(const QString &gamePath) const;
    UAssetService *m_uasset;
};

} // namespace st
