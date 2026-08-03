#pragma once

#include <QObject>
#include <QString>

namespace st {

// Wrapper de repak.exe (unpack/pack) y extracción de zips (tar.exe de Windows).
// Todas las operaciones son sincrónicas; llamarlas desde un hilo de trabajo.
class PakService : public QObject {
    Q_OBJECT
public:
    explicit PakService(QObject *parent = nullptr);

    static QString repakPath();       // tools/repak.exe junto al exe (o override por env ST_REPAK)
    static QString retocPath();       // tools/retoc.exe (o env ST_RETOC); Zen/IoStore
    static QString cnsRetocPath();    // fork con --mount-folder usado por CNS
    // Carpeta que contiene oo2core_9_win64.dll (Oodle). No se redistribuye
    // (propietario): se usa directo de la instalación del juego, como hace
    // Builder/compiler/toolchain.py. Sin ella, retoc/cue4parse intentan
    // DESCARGARLA de GitHub en runtime, lo que cuelga sin red o con GitHub
    // bloqueado (China). Orden: ruta elegida por el usuario (Ajustes), env
    // STELLAR_OODLE_DIR, tools/ junto al exe, SB/Binaries/Win64 y demás carpetas
    // del juego, y por último cualquier copia bajo el juego. "" si no hay.
    // La comparación de nombre ignora mayúsculas (Wine/Proton).
    static QString oodleDir();
    static QString oodleFilePath();     // ruta completa a la DLL encontrada ("" si no hay)
    static QString oodleSearchReport(); // dónde se buscó: para mensajes de error
    static void resetOodleCache();
    // Override del usuario: puede ser la DLL o la carpeta que la contiene.
    static QString userOodlePath();
    static void setUserOodlePath(const QString &pathOrFile);
    bool available() const;
    bool zenAvailable() const;

    bool unpack(const QString &pakPath, const QString &outDir, QString *error = nullptr);
    // Convierte un contenedor Zen/IoStore (.utoc) a assets legacy con retoc.
    // Devuelve la cantidad de assets extraídos (0 = no se pudo revertir).
    int toLegacy(const QString &utocPath, const QString &outDir, QString *error = nullptr);
    // to-legacy sobre un directorio de contenedores (ej. Paks del juego) con
    // filtro de nombre de asset. Devuelve cantidad de .uasset producidos.
    int toLegacyFiltered(const QString &inputDir, const QString &filter,
                         const QString &outDir, QString *error = nullptr);
    // Nombres de assets dentro de un contenedor Zen (vía retoc unpack), sin
    // convertir. Útil para informar qué tablas toca un mod Zen no convertible.
    QStringList listZenAssets(const QString &utocPath);
    bool pack(const QString &contentDir, const QString &outPak, QString *error = nullptr);
    // Empaqueta a contenedor Zen/IoStore (.utoc/.ucas/.pak) con retoc y verifica.
    bool packZen(const QString &contentDir, const QString &outUtoc, QString *error = nullptr);
    // Global "compat" para revertir mods Zen. El global.utoc del juego es
    // versión PartitionSize y los paks de mods son DirectoryIndex; retoc no
    // compone contenedores de versiones distintas, así que sin esto to-legacy
    // no puede resolver los script objects y falla en TODOS los assets del mod.
    // Se reempaqueta el global del juego con la versión del manifest cambiada.
    // Se genera de los archivos del propio usuario y se cachea en AppData.
    // Devuelve la carpeta que contiene global.utoc/.ucas, o vacío si no se pudo.
    static QString compatGlobalDir(QString *error = nullptr);
    // to-legacy de un mod Zen usando ese global. Devuelve cuántos .uasset salieron.
    int toLegacyWithGlobal(const QString &utocPath, const QString &outDir,
                           QString *error = nullptr);
    // Extrae uno o más contenedores de mod resolviendo imports contra los paks
    // del juego. Es el modo requerido por outfits CNS ya reubicados.
    int toLegacyMounted(const QString &modDir, const QString &outDir,
                        QString *error = nullptr);
    bool extractZip(const QString &zipPath, const QString &outDir, QString *error = nullptr);
    // Comprime el contenido de srcDir (recursivo) en outZip.
    bool createZip(const QString &srcDir, const QString &outZip, QString *error = nullptr);

private:
    bool runProcess(const QString &exe, const QStringList &args, QString *error, int timeoutMs = 300000);
};

} // namespace st
