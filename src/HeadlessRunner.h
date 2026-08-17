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
//   StellarTool --headless save-to-json   --input <sav> --out <json> [--indent <n>]
//   StellarTool --headless save-from-json --input <json> --out <sav>
//   StellarTool --headless fix-save       --input <sav>
//   StellarTool --headless reshade        --action <list|save|restore|rename|delete|import|export>
//   StellarTool --headless live           --action <status|install|uninstall|reset|set>
//   StellarTool --headless moveset        --mod <carpeta> --action <list|install|uninstall>
//   StellarTool --headless patch-validate --input <patch.toml>
//   StellarTool --headless patch-preview  --input <patch.toml> [--baseline <dir>]
//   StellarTool --headless patch-apply    --input <patch.toml> --out <dir> [--baseline <dir>]
//   StellarTool --headless patch-export  --mod <ruta>... --out <dir>
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
        QString input;
        QString action;
        QString oldName;
        QString newName;
        int indent = 2;
        double fov = -1;
        double speed = -1;
        double jump = -1;
        bool fovEnabled = false;
        bool fovEnabledSet = false;
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
    int runSave(const QString &command, const Options &options);
    int runReShade(const Options &options);
    int runLive(const Options &options);
    int runMoveset(const Options &options);
    int runPatch(const QString &command, const Options &options);

private:
    bool waitIdle();   // procesa el event loop hasta que controller no esté busy
    AppController *m_controller;
};

} // namespace st
