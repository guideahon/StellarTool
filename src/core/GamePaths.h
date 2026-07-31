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
    static QString modsDir();                // <root>/SB/Content/Paks/~mods (destino de instalación)
    static bool hasGame();                   // paksDir existe con global.utoc
    static QStringList globalContainerFiles(); // global.utoc/.ucas/.upak del juego

    // Stage temporal para CUE4Parse: mismo volumen que el juego (para
    // hardlinkear el global de varios GB) pero FUERA de Paks, porque la
    // baseline exporta con -i <Paks> recursivo y un stage ahí dentro hace
    // releer el juego entero por segunda vez.
    static QString cue4StageDir();
    // Borra stages de corridas anteriores (incluido el legacy dentro de Paks,
    // que quedaba si la app se cerró/crasheó a mitad de un import).
    static void cleanCue4Stages();

    static QString detectSteam();            // intenta ubicar StellarBlade en Steam
    // Acomoda lo que eligio el usuario: acepta la raiz, una subcarpeta (SB,
    // Content, Paks, ~mods) o la carpeta que la contiene. "" si no es el juego.
    static QString normalizeRoot(const QString &dir);
};

} // namespace st
