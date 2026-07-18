#pragma once

#include "ModTypes.h"
#include <QObject>

namespace st {

// Cache de tablas vanilla en %LOCALAPPDATA%/StellarTool/baseline/.
// Sin baseline la app funciona en modo degradado (diff solo entre mods).
class UAssetService;
class Cue4Service;

class BaselineManager : public QObject {
    Q_OBJECT
public:
    BaselineManager(UAssetService *uasset, Cue4Service *cue4, QObject *parent = nullptr);

    QString baselineDir() const;
    bool hasBaseline() const;

    // JSON de la tabla vanilla para un gamePath; objeto vacío si no está.
    QJsonObject tableFor(const QString &gamePath) const;

    // Importa un dir con tablas vanilla a la cache: acepta JSONs de UAssetGUI
    // y/o .uasset legacy (se convierten con UAssetService).
    bool importFromDir(const QString &dir, QString *error, int *imported);

    // Construye la baseline leyendo TODAS las DataTables vanilla del juego con
    // CUE4Parse (misma representación que los mods Zen -> diff limpio).
    // gamePaksDir = <juego>/SB/Content/Paks ; mappings = usmap.
    bool buildFromGame(const QString &gamePaksDir, const QString &mappings,
                       QString *error, int *imported);

signals:
    void progress(const QString &message);

private:
    QString keyFor(const QString &gamePath) const;
    UAssetService *m_uasset;
    Cue4Service *m_cue4;
};

} // namespace st
