#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

namespace st {

class AppController;

// Modo CLI sin UI. Toda funcion de la app tiene que poder correrse asi: la UI
// es una vista sobre AppController, no el unico camino.
//
//   StellarTool --headless analyze   --mod <ruta>... [--baseline <dir>]
//   StellarTool --headless merge     --mod <ruta>... --out <dir> [--prefer <mod>]
//   StellarTool --headless cns       --mod <ruta> --out <dir> [--name <nombre>]
//   StellarTool --headless replacer  --mod <ruta> --out <dir> --replace <outfit>
//   StellarTool --headless build     --answers <json|archivo> --out <dir>
//                                    [--install-paks] [--install-helper]
//   StellarTool --headless baseline  [--game <dir>]
//   StellarTool --headless status
//   StellarTool --headless detect
//   StellarTool --headless uninstall [--paks] [--helper]
//   StellarTool --headless fixids    --mod <dir> [--apply]
//   StellarTool --headless presets
//
// Salida por stdout; exit code 0 = OK.
class HeadlessRunner : public QObject {
    Q_OBJECT
public:
    // Opciones de la linea de comandos ya parseadas. Es un struct plano para
    // que validate() se pueda probar sin levantar la app entera.
    struct Options {
        QStringList mods;
        QString outDir;
        QString baselineDir;
        QString preferMod;
        QString name;
        QString replacement;
        QString selection;
        QString answers;     // JSON inline o ruta a un .json/.stpreset
        QString preset;      // nombre de un preset guardado
        bool rebuildBaseline = false;
        bool installPaks = false;
        bool installHelper = false;
        bool uninstallPaks = false;
        bool uninstallHelper = false;
        bool applyFixes = false;
    };

    explicit HeadlessRunner(AppController *controller, QObject *parent = nullptr);

    // Comandos aceptados por --headless (la ayuda de main.cpp sale de aca).
    static QStringList knownCommands();
    static bool isKnownCommand(const QString &command);
    // Chequea que el comando traiga lo que necesita. false + *error si falta
    // algo. No toca disco ni el juego: es lo que hace testeable el CLI.
    static bool validate(const QString &command, const Options &options,
                         QString *error = nullptr);
    // Resuelve --answers: JSON pegado en la linea de comandos, un .json del
    // cuestionario o un .stpreset exportado desde la UI (que envuelve las
    // respuestas). Devuelve vacio + *error si no se pudo.
    static QString answersFromValue(const QString &value, QString *error = nullptr);

    // Despacha un comando ya validado. Devuelve el exit code del proceso.
    int exec(const QString &command, const Options &options);

    int run(const QString &command, const QStringList &mods,
            const QString &outDir, const QString &baselineDir,
            const QString &preferMod, bool rebuildBaseline = false);
    int runCns(const QString &command, const QString &input, const QString &outDir,
               const QString &name, const QString &replacement,
               const QString &selection);
    int runBuild(const Options &options);
    int runBaseline(const Options &options);
    int runStatus();
    int runDetect();
    int runUninstall(const Options &options);
    int runFixIds(const Options &options);
    int runPresets();

private:
    bool waitIdle();   // procesa el event loop hasta que controller no esté busy
    AppController *m_controller;
};

} // namespace st
