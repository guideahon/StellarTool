#include "AppController.h"
#include "HeadlessRunner.h"
#include "Translator.h"
#include "core/GamePaths.h"
#include "core/LiveService.h"
#include "core/ReShadePresetService.h"
#include "core/UpdateService.h"

#include <QGuiApplication>
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QIcon>
#include <QWindow>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#include <cstdio>
// El exe es WIN32 (sin consola); en headless nos re-adjuntamos a la consola
// del padre para que stdout/stderr lleguen a la terminal que lo invocó.
static void attachParentConsole() {
    // Si stdout ya apunta a algo (redirección a archivo/pipe), no tocar nada.
    const HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h != nullptr && h != INVALID_HANDLE_VALUE && GetFileType(h) != FILE_TYPE_UNKNOWN)
        return;
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE *f;
        freopen_s(&f, "CONOUT$", "w", stdout);
        freopen_s(&f, "CONOUT$", "w", stderr);
    }
}

// La barra superior es la decoración nativa de Windows, por fuera de QML.
// Sin esto el canvas puede estar en Dark/OLED mientras el caption permanece
// claro. DWMWA_USE_IMMERSIVE_DARK_MODE está disponible en Windows 10/11;
// algunos builds antiguos exponen el mismo atributo con el índice 19.
static void applyWindowChrome(QWindow *window, const QString &mode) {
    if (!window) return;
    const HWND hwnd = reinterpret_cast<HWND>(window->winId());
    const BOOL dark = mode != QLatin1String("light");
    constexpr DWORD kUseImmersiveDarkMode = 20;
    constexpr DWORD kUseImmersiveDarkModeLegacy = 19;
    constexpr DWORD kCaptionColor = 35;
    constexpr DWORD kTextColor = 36;

    HRESULT result = DwmSetWindowAttribute(hwnd, kUseImmersiveDarkMode,
                                           &dark, sizeof(dark));
    if (FAILED(result)) {
        DwmSetWindowAttribute(hwnd, kUseImmersiveDarkModeLegacy,
                              &dark, sizeof(dark));
    }

    const COLORREF caption = dark ? RGB(0, 0, 0) : RGB(243, 245, 248);
    const COLORREF text = dark ? RGB(241, 244, 248) : RGB(23, 27, 34);
    DwmSetWindowAttribute(hwnd, kCaptionColor, &caption, sizeof(caption));
    DwmSetWindowAttribute(hwnd, kTextColor, &text, sizeof(text));
}
#else
static void attachParentConsole() {}
#endif

int main(int argc, char *argv[]) {
    // Modo headless: sin ventana, apto para consola/CI.
    bool headless = false;
    for (int i = 1; i < argc; ++i)
        if (qstrcmp(argv[i], "--headless") == 0) headless = true;

    if (headless) {
        attachParentConsole();
        QCoreApplication app(argc, argv);
        app.setApplicationVersion(QStringLiteral(STELLAR_TOOL_VERSION));
        app.setOrganizationName(QStringLiteral("StellarTool"));
        app.setApplicationName(QStringLiteral("StellarTool"));

        QCommandLineParser parser;
        parser.setApplicationDescription(QStringLiteral(
            "Stellar Tool headless.\n"
            "  StellarTool --headless analyze   --mod <ruta>... [--baseline <dir>]\n"
            "  StellarTool --headless merge     --mod <ruta>... --out <dir> [--baseline <dir>] [--prefer <mod>]\n"
            "  StellarTool --headless cns       --mod <ruta> --out <dir> [--name <nombre>]\n"
            "  StellarTool --headless replacer  --mod <ruta> --out <dir> --replace <outfit> [--select <variante>]\n"
            "  StellarTool --headless build     --answers <json|archivo> --out <dir> [--install-paks] [--install-helper]\n"
            "  StellarTool --headless build     --preset <nombre> --out <dir>\n"
            "  StellarTool --headless baseline  [--game <dir>]\n"
            "  StellarTool --headless status\n"
            "  StellarTool --headless detect\n"
            "  StellarTool --headless uninstall [--paks] [--helper]\n"
            "  StellarTool --headless fixids    --mod <dir> [--apply]\n"
            "  StellarTool --headless presets\n"
            "  StellarTool --headless save-to-json   --input <sav> --out <json> [--indent <n>]\n"
            "  StellarTool --headless save-from-json --input <json> --out <sav>\n"
            "  StellarTool --headless fix-save       --input <sav>\n"
            "  StellarTool --headless reshade --action <list|save|restore|rename|delete|import|export>\n"
            "  StellarTool --headless live    --action <status|install|uninstall|reset|set>\n"
            "  StellarTool --headless moveset --mod <carpeta> --action <list|install|uninstall> [--select <variante>]\n"
            "  StellarTool --headless moveset-catalog --mod <carpeta> --out <catalog.json> [--game <dir>]\n"
            "  StellarTool --headless patch-validate --input <patch.toml>\n"
            "  StellarTool --headless patch-preview --input <patch.toml> [--baseline <dir>]\n"
            "  StellarTool --headless patch-apply --input <patch.toml> --out <dir> [--baseline <dir>]\n"
            "  StellarTool --headless patch-export --mod <ruta>... --out <dir>"));
        parser.addHelpOption();
        parser.addVersionOption();
        parser.addOption({QStringLiteral("headless"), QStringLiteral("Modo sin UI")});
        parser.addOption({QStringLiteral("mod"), QStringLiteral("Mod a cargar (.pak/.zip/carpeta); repetible. El primero tiene prioridad."), QStringLiteral("ruta")});
        parser.addOption({QStringLiteral("out"), QStringLiteral("Carpeta destino del pak mergeado"), QStringLiteral("dir")});
        parser.addOption({QStringLiteral("baseline"), QStringLiteral("Carpeta con JSONs de tablas vanilla"), QStringLiteral("dir")});
        parser.addOption({QStringLiteral("prefer"), QStringLiteral("Nombre de mod que gana todos sus conflictos"), QStringLiteral("mod")});
        parser.addOption({QStringLiteral("no-zip"), QStringLiteral("No generar el zip instalable junto al pak")});
        parser.addOption({QStringLiteral("rebuild-baseline"), QStringLiteral("Reconstruir baseline vanilla desde el juego (CUE4Parse) antes de analizar")});
        parser.addOption({QStringLiteral("game"), QStringLiteral("Ruta de instalación de Stellar Blade (para leer mods Zen)"), QStringLiteral("dir")});
        parser.addOption({QStringLiteral("name"), QStringLiteral("Nombre visible del outfit convertido"), QStringLiteral("nombre")});
        parser.addOption({QStringLiteral("replace"), QStringLiteral("Nombre del outfit vanilla a reemplazar"), QStringLiteral("outfit")});
        parser.addOption({QStringLiteral("select"), QStringLiteral("Variante CNS por nombre o índice"), QStringLiteral("variante")});
        parser.addOption({QStringLiteral("answers"), QStringLiteral("Respuestas del Builder: JSON inline, .json o .stpreset"), QStringLiteral("json|archivo")});
        parser.addOption({QStringLiteral("preset"), QStringLiteral("Nombre de un preset guardado del Builder"), QStringLiteral("nombre")});
        parser.addOption({QStringLiteral("install-paks"), QStringLiteral("Instalar los paks compilados en el juego")});
        parser.addOption({QStringLiteral("install-helper"), QStringLiteral("Instalar y activar el helper UE4SS")});
        parser.addOption({QStringLiteral("paks"), QStringLiteral("uninstall: quitar los paks que instaló la tool")});
        parser.addOption({QStringLiteral("helper"), QStringLiteral("uninstall: quitar el helper que instaló la tool")});
        parser.addOption({QStringLiteral("apply"), QStringLiteral("fixids: escribir los cambios (por defecto solo reporta)")});
        parser.addOption({QStringLiteral("input"), QStringLiteral("Archivo de entrada para partidas/ReShade"), QStringLiteral("archivo")});
        parser.addOption({QStringLiteral("action"), QStringLiteral("Operación de ReShade o Live"), QStringLiteral("operacion")});
        parser.addOption({QStringLiteral("old-name"), QStringLiteral("Nombre anterior de preset ReShade"), QStringLiteral("nombre")});
        parser.addOption({QStringLiteral("new-name"), QStringLiteral("Nombre nuevo de preset ReShade"), QStringLiteral("nombre")});
        parser.addOption({QStringLiteral("indent"), QStringLiteral("Indentación JSON para save-to-json"), QStringLiteral("n"), QStringLiteral("2")});
        parser.addOption({QStringLiteral("fov"), QStringLiteral("FOV Live (40-170)"), QStringLiteral("valor")});
        parser.addOption({QStringLiteral("speed"), QStringLiteral("Multiplicador de velocidad Live (0.1-10)"), QStringLiteral("valor")});
        parser.addOption({QStringLiteral("jump"), QStringLiteral("Multiplicador de salto Live (0.1-10)"), QStringLiteral("valor")});
        parser.addOption({QStringLiteral("fov-enabled"), QStringLiteral("Activar FOV Live")});
        parser.addOption({QStringLiteral("no-fov"), QStringLiteral("Desactivar FOV Live")});
        parser.process(app);

        const QStringList pos = parser.positionalArguments();
        const QString command = pos.isEmpty() ? QStringLiteral("analyze") : pos.first();

        st::HeadlessRunner::Options options;
        options.mods = parser.values(QStringLiteral("mod"));
        options.outDir = parser.value(QStringLiteral("out"));
        options.baselineDir = parser.value(QStringLiteral("baseline"));
        options.preferMod = parser.value(QStringLiteral("prefer"));
        options.name = parser.value(QStringLiteral("name"));
        options.replacement = parser.value(QStringLiteral("replace"));
        options.selection = parser.value(QStringLiteral("select"));
        options.answers = parser.value(QStringLiteral("answers"));
        options.preset = parser.value(QStringLiteral("preset"));
        options.rebuildBaseline = parser.isSet(QStringLiteral("rebuild-baseline"));
        options.installPaks = parser.isSet(QStringLiteral("install-paks"));
        options.installHelper = parser.isSet(QStringLiteral("install-helper"));
        options.uninstallPaks = parser.isSet(QStringLiteral("paks"));
        options.uninstallHelper = parser.isSet(QStringLiteral("helper"));
        options.applyFixes = parser.isSet(QStringLiteral("apply"));
        options.input = parser.value(QStringLiteral("input"));
        options.action = parser.value(QStringLiteral("action"));
        options.oldName = parser.value(QStringLiteral("old-name"));
        options.newName = parser.value(QStringLiteral("new-name"));
        options.indent = parser.value(QStringLiteral("indent")).toInt();
        bool numberOk = false;
        options.fov = parser.value(QStringLiteral("fov")).toDouble(&numberOk);
        if (!numberOk) options.fov = -1;
        options.speed = parser.value(QStringLiteral("speed")).toDouble(&numberOk);
        if (!numberOk) options.speed = -1;
        options.jump = parser.value(QStringLiteral("jump")).toDouble(&numberOk);
        if (!numberOk) options.jump = -1;
        options.fovEnabledSet = parser.isSet(QStringLiteral("fov-enabled")) || parser.isSet(QStringLiteral("no-fov"));
        options.fovEnabled = parser.isSet(QStringLiteral("fov-enabled"));

        // Validar antes de construir el controller: un comando mal escrito no
        // tiene por que arrancar servicios ni tocar el juego.
        QString error;
        if (!st::HeadlessRunner::validate(command, options, &error)) {
            fprintf(stderr, "%s\n", qPrintable(error));
            return 2;
        }

        if (parser.isSet(QStringLiteral("game")))
            st::GamePaths::setGameRoot(parser.value(QStringLiteral("game")));
        st::Translator translator;
        st::AppController controller(&translator);
        controller.setExportZip(!parser.isSet(QStringLiteral("no-zip")));
        st::HeadlessRunner runner(&controller);
        return runner.exec(command, options);
    }

    QGuiApplication app(argc, argv);
    app.setApplicationVersion(QStringLiteral(STELLAR_TOOL_VERSION));
    app.setOrganizationName(QStringLiteral("StellarTool"));
    app.setApplicationName(QStringLiteral("StellarTool"));
    app.setWindowIcon(QIcon(QStringLiteral(":/assets/app_icon.png")));

    st::Translator translator;
    st::AppController controller(&translator);
    st::UpdateService updater;
    st::LiveService live;
    st::ReShadePresetService reshade;
    QObject::connect(&controller, &st::AppController::gamePathChanged,
                     &reshade, &st::ReShadePresetService::refresh);
    // El .bat de reemplazo ya está corriendo: hay que soltar el exe viejo.
    QObject::connect(&updater, &st::UpdateService::quitRequested, &app, &QGuiApplication::quit);

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::warnings, [](const QList<QQmlError> &ws) {
        QFile f(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                + QStringLiteral("/qml.log"));
        QDir().mkpath(QFileInfo(f).absolutePath());
        if (f.open(QIODevice::WriteOnly | QIODevice::Append)) {
            for (const QQmlError &w : ws)
                f.write((w.toString() + QLatin1Char('\n')).toUtf8());
        }
    });
    engine.rootContext()->setContextProperty(QStringLiteral("App"), &controller);
    engine.rootContext()->setContextProperty(QStringLiteral("I18n"), &translator);
    engine.rootContext()->setContextProperty(QStringLiteral("Updater"), &updater);
    engine.rootContext()->setContextProperty(QStringLiteral("Live"), &live);
    engine.rootContext()->setContextProperty(QStringLiteral("ReShade"), &reshade);
    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;
#ifdef Q_OS_WIN
    if (auto *window = qobject_cast<QWindow *>(engine.rootObjects().constFirst())) {
        applyWindowChrome(window, controller.themeMode());
        QObject::connect(&controller, &st::AppController::themeModeChanged,
                         window, [window, &controller] {
                             applyWindowChrome(window, controller.themeMode());
                         });
    }
#endif
    // Chequeo silencioso de actualización al arrancar (desactivable en Settings).
    if (updater.checkOnStartup())
        QMetaObject::invokeMethod(&updater, [&updater] { updater.checkForUpdates(true); },
                                  Qt::QueuedConnection);
    return app.exec();
}
