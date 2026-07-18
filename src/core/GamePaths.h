#pragma once

#include <QString>
#include <QStringList>

namespace st {

// Ubicación de la instalación de Stellar Blade. Necesaria para leer mods Zen
// (CUE4Parse resuelve tipos con el global.utoc del juego) y para extraer la
// baseline vanilla. Persistida en QSettings; autodetecta Steam.
class GamePaths {
public:
    static QString gameRoot();               // .../StellarBlade  (guardado o autodetectado)
    static void setGameRoot(const QString &root);
    static QString paksDir();                // <root>/SB/Content/Paks
    static bool hasGame();                   // paksDir existe con global.utoc
    static QStringList globalContainerFiles(); // global.utoc/.ucas/.upak del juego

    static QString detectSteam();            // intenta ubicar StellarBlade en Steam
};

} // namespace st
