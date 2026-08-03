#include "AppController.h"
#include "HeadlessRunner.h"
#include "Translator.h"
#include "core/GamePaths.h"
#include "core/LiveService.h"
#include "core/UpdateService.h"

#include <QGuiApplication>
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QIcon>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#ifdef Q_OS_WIN
#include <windows.h>
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
            "  StellarTool --headless presets"));
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
    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;
    // Chequeo silencioso de actualización al arrancar (desactivable en Settings).
    if (updater.checkOnStartup())
        QMetaObject::invokeMethod(&updater, [&updater] { updater.checkForUpdates(true); },
                                  Qt::QueuedConnection);
    return app.exec();
}
