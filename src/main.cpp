#include "AppController.h"
#include "HeadlessRunner.h"

#include <QGuiApplication>
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QQmlApplicationEngine>
#include <QQmlContext>

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
        app.setOrganizationName(QStringLiteral("StellarTool"));
        app.setApplicationName(QStringLiteral("StellarTool"));

        QCommandLineParser parser;
        parser.setApplicationDescription(QStringLiteral(
            "Stellar Tool headless.\n"
            "  StellarTool --headless analyze --mod <ruta>... [--baseline <dir>]\n"
            "  StellarTool --headless merge   --mod <ruta>... --out <dir> [--baseline <dir>] [--prefer <mod>]"));
        parser.addHelpOption();
        parser.addOption({QStringLiteral("headless"), QStringLiteral("Modo sin UI")});
        parser.addOption({QStringLiteral("mod"), QStringLiteral("Mod a cargar (.pak/.zip/carpeta); repetible. El primero tiene prioridad."), QStringLiteral("ruta")});
        parser.addOption({QStringLiteral("out"), QStringLiteral("Carpeta destino del pak mergeado"), QStringLiteral("dir")});
        parser.addOption({QStringLiteral("baseline"), QStringLiteral("Carpeta con JSONs de tablas vanilla"), QStringLiteral("dir")});
        parser.addOption({QStringLiteral("prefer"), QStringLiteral("Nombre de mod que gana todos sus conflictos"), QStringLiteral("mod")});
        parser.process(app);

        const QStringList pos = parser.positionalArguments();
        const QString command = pos.isEmpty() ? QStringLiteral("analyze") : pos.first();
        if (command != QLatin1String("analyze") && command != QLatin1String("merge")) {
            fprintf(stderr, "Comando desconocido: %s (usar analyze | merge)\n", qPrintable(command));
            return 2;
        }

        st::AppController controller;
        st::HeadlessRunner runner(&controller);
        return runner.run(command, parser.values(QStringLiteral("mod")),
                          parser.value(QStringLiteral("out")),
                          parser.value(QStringLiteral("baseline")),
                          parser.value(QStringLiteral("prefer")));
    }

    QGuiApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("StellarTool"));
    app.setApplicationName(QStringLiteral("StellarTool"));

    st::AppController controller;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("App"), &controller);
    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;
    return app.exec();
}
