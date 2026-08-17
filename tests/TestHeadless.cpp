#include <QtTest>

#include "HeadlessRunner.h"
#include "core/MovesetService.h"

#include <QFile>
#include <QDir>
#include <QTemporaryDir>

// El CLI headless es la unica via para automatizar la app (CI, scripts, usuarios
// sin ganas de abrir la UI). Estos tests fijan el contrato de la linea de
// comandos: que comandos existen y que exige cada uno. Son puros: no tocan el
// juego ni compilan nada, asi que corren en cualquier maquina.
class TestHeadless : public QObject {
    Q_OBJECT

private slots:
    void everyCommandIsListed();
    void unknownCommandIsRejected();
    void analyzeAndMergeNeedTheirArguments();
    void cnsAndReplacerNeedTheirArguments();
    void buildNeedsAnswersAndOut();
    void uninstallNeedsATarget();
    void fixidsNeedsADirectory();
    void saveCommandsNeedTheirArguments();
    void serviceCommandsNeedTheirArguments();
    void commandsWithoutArgumentsValidate();
    void answersAcceptInlineJsonFileAndPreset();
    void movesetCatalogRecognizesFamiliesAndAggro();
};

namespace {
st::HeadlessRunner::Options withMods(const QStringList &mods) {
    st::HeadlessRunner::Options o;
    o.mods = mods;
    return o;
}

QString validationError(const QString &command, const st::HeadlessRunner::Options &o) {
    QString error;
    return st::HeadlessRunner::validate(command, o, &error) ? QString() : error;
}
}   // namespace

// La ayuda de main.cpp y el mensaje de "comando desconocido" salen de esta
// lista: si alguien agrega un comando y se olvida de registrarlo, queda invisible.
void TestHeadless::everyCommandIsListed() {
    const QStringList commands = st::HeadlessRunner::knownCommands();
    const QStringList expected = {
        QStringLiteral("analyze"), QStringLiteral("merge"),   QStringLiteral("cns"),
        QStringLiteral("replacer"), QStringLiteral("build"),  QStringLiteral("baseline"),
        QStringLiteral("status"),  QStringLiteral("detect"),  QStringLiteral("uninstall"),
        QStringLiteral("fixids"),  QStringLiteral("presets"), QStringLiteral("save-to-json"),
        QStringLiteral("save-from-json"), QStringLiteral("fix-save"), QStringLiteral("reshade"),
        QStringLiteral("live"), QStringLiteral("moveset"),
        QStringLiteral("patch-validate"), QStringLiteral("patch-preview"),
        QStringLiteral("patch-apply"), QStringLiteral("patch-export")};
    for (const QString &name : expected) {
        QVERIFY2(commands.contains(name),
                 qPrintable(QStringLiteral("falta el comando %1").arg(name)));
        QVERIFY(st::HeadlessRunner::isKnownCommand(name));
    }
}

void TestHeadless::movesetCatalogRecognizesFamiliesAndAggro() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString dir = root.path() + QStringLiteral("/aggro-scarlet-goddess");
    QVERIFY(QDir().mkpath(dir));
    for (const QString &ext : {QStringLiteral("pak"), QStringLiteral("ucas"), QStringLiteral("utoc")}) {
        QFile f(dir + QStringLiteral("/moveset_P.") + ext);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("fixture");
    }
    QString error;
    const auto variants = st::MovesetService::scan(root.path(), &error);
    QCOMPARE(variants.size(), 1);
    QCOMPARE(variants.first().id, QStringLiteral("aggro-scarlet-goddess"));
    QCOMPARE(variants.first().family, QStringLiteral("scarlet"));
    QVERIFY(variants.first().aggro);
}

void TestHeadless::unknownCommandIsRejected() {
    QVERIFY(!st::HeadlessRunner::isKnownCommand(QStringLiteral("compilar")));
    const QString error = validationError(QStringLiteral("compilar"), {});
    QVERIFY(error.contains(QStringLiteral("Comando desconocido")));
    // El mensaje lista las alternativas: es lo unico que ve el usuario.
    QVERIFY(error.contains(QStringLiteral("build")));
}

void TestHeadless::analyzeAndMergeNeedTheirArguments() {
    QVERIFY(!validationError(QStringLiteral("analyze"), {}).isEmpty());
    QVERIFY(validationError(QStringLiteral("analyze"),
                            withMods({QStringLiteral("a.pak")})).isEmpty());

    // merge sin --out escribiria en ningun lado.
    QVERIFY(!validationError(QStringLiteral("merge"),
                             withMods({QStringLiteral("a.pak")})).isEmpty());
    st::HeadlessRunner::Options merge = withMods({QStringLiteral("a.pak")});
    merge.outDir = QStringLiteral("out");
    QVERIFY(validationError(QStringLiteral("merge"), merge).isEmpty());
}

void TestHeadless::cnsAndReplacerNeedTheirArguments() {
    st::HeadlessRunner::Options o = withMods({QStringLiteral("outfit.zip")});
    QVERIFY(!validationError(QStringLiteral("cns"), o).isEmpty());   // falta --out
    o.outDir = QStringLiteral("out");
    QVERIFY(validationError(QStringLiteral("cns"), o).isEmpty());

    // replacer ademas necesita saber que outfit vanilla pisa.
    QVERIFY(!validationError(QStringLiteral("replacer"), o).isEmpty());
    o.replacement = QStringLiteral("Bunny");
    QVERIFY(validationError(QStringLiteral("replacer"), o).isEmpty());
}

void TestHeadless::buildNeedsAnswersAndOut() {
    st::HeadlessRunner::Options o;
    QVERIFY(!validationError(QStringLiteral("build"), o).isEmpty());

    o.answers = QStringLiteral("{}");
    QVERIFY(!validationError(QStringLiteral("build"), o).isEmpty());   // falta --out
    o.outDir = QStringLiteral("out");
    QVERIFY(validationError(QStringLiteral("build"), o).isEmpty());

    // --preset es la alternativa a --answers, pero no las dos a la vez: habria
    // que adivinar cual gana.
    o.preset = QStringLiteral("Mi build");
    QVERIFY(!validationError(QStringLiteral("build"), o).isEmpty());
    o.answers.clear();
    QVERIFY(validationError(QStringLiteral("build"), o).isEmpty());
}

void TestHeadless::uninstallNeedsATarget() {
    st::HeadlessRunner::Options o;
    // Sin --paks ni --helper no habria nada que desinstalar: mejor decirlo que
    // salir con 0 sin haber hecho nada.
    QVERIFY(!validationError(QStringLiteral("uninstall"), o).isEmpty());
    o.uninstallHelper = true;
    QVERIFY(validationError(QStringLiteral("uninstall"), o).isEmpty());
    o.uninstallHelper = false;
    o.uninstallPaks = true;
    QVERIFY(validationError(QStringLiteral("uninstall"), o).isEmpty());
}

void TestHeadless::fixidsNeedsADirectory() {
    QVERIFY(!validationError(QStringLiteral("fixids"), {}).isEmpty());
    QVERIFY(validationError(QStringLiteral("fixids"),
                            withMods({QStringLiteral("C:/mods/outfit")})).isEmpty());
}

void TestHeadless::saveCommandsNeedTheirArguments() {
    st::HeadlessRunner::Options o;
    QVERIFY(!validationError(QStringLiteral("save-to-json"), o).isEmpty());
    o.input = QStringLiteral("DekCNS.sav");
    QVERIFY(!validationError(QStringLiteral("save-to-json"), o).isEmpty());
    o.outDir = QStringLiteral("DekCNS.json");
    QVERIFY(validationError(QStringLiteral("save-to-json"), o).isEmpty());
    o.input = QStringLiteral("DekCNS.json");
    QVERIFY(validationError(QStringLiteral("save-from-json"), o).isEmpty());
    o.outDir.clear();
    QVERIFY(!validationError(QStringLiteral("save-from-json"), o).isEmpty());
    o.input = QStringLiteral("DekCNS.sav");
    QVERIFY(validationError(QStringLiteral("fix-save"), o).isEmpty());
}

void TestHeadless::serviceCommandsNeedTheirArguments() {
    st::HeadlessRunner::Options o;
    QVERIFY(!validationError(QStringLiteral("reshade"), o).isEmpty());
    o.action = QStringLiteral("list");
    QVERIFY(validationError(QStringLiteral("reshade"), o).isEmpty());
    o.action = QStringLiteral("restore");
    QVERIFY(!validationError(QStringLiteral("reshade"), o).isEmpty());
    o.name = QStringLiteral("Preset");
    QVERIFY(validationError(QStringLiteral("reshade"), o).isEmpty());

    o = {};
    QVERIFY(!validationError(QStringLiteral("live"), o).isEmpty());
    o.action = QStringLiteral("status");
    QVERIFY(validationError(QStringLiteral("live"), o).isEmpty());
    o.action = QStringLiteral("set");
    QVERIFY(!validationError(QStringLiteral("live"), o).isEmpty());
    o.speed = 1.25;
    QVERIFY(validationError(QStringLiteral("live"), o).isEmpty());
}

void TestHeadless::commandsWithoutArgumentsValidate() {
    for (const QString &command : {QStringLiteral("status"), QStringLiteral("detect"),
                                   QStringLiteral("presets"), QStringLiteral("baseline")}) {
        QVERIFY2(validationError(command, {}).isEmpty(), qPrintable(command));
    }
}

// --answers tiene que aceptar lo que el usuario tenga a mano: JSON pegado, el
// .json del cuestionario, o el .stpreset que exporta la UI (que envuelve las
// respuestas en {format, schemaVersion, answers}).
void TestHeadless::answersAcceptInlineJsonFileAndPreset() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const auto write = [&tmp](const QString &name, const QString &body) {
        const QString path = tmp.path() + QLatin1Char('/') + name;
        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write(body.toUtf8());
        f.close();
        return path;
    };

    QString error;
    const QString inlineJson = QStringLiteral("{\"combatProfile\":\"full\"}");
    QCOMPARE(st::HeadlessRunner::answersFromValue(inlineJson, &error), inlineJson);
    QVERIFY(error.isEmpty());

    const QString plain = write(QStringLiteral("answers.json"), inlineJson);
    QVERIFY(st::HeadlessRunner::answersFromValue(plain, &error)
                .contains(QStringLiteral("combatProfile")));

    const QString preset = write(QStringLiteral("mi-build.stpreset"), QStringLiteral(
        "{\"format\":\"stellartool.builder-preset\",\"schemaVersion\":1,"
        "\"answers\":{\"combatProfile\":\"full\",\"miniBoss\":\"on\"}}"));
    const QString unwrapped = st::HeadlessRunner::answersFromValue(preset, &error);
    QVERIFY(error.isEmpty());
    QVERIFY(unwrapped.contains(QStringLiteral("miniBoss")));
    // Se desenvuelve: el builder espera las respuestas, no el sobre del preset.
    QVERIFY(!unwrapped.contains(QStringLiteral("schemaVersion")));

    // Archivo inexistente y basura: error explicito, no un build a medias.
    QVERIFY(st::HeadlessRunner::answersFromValue(tmp.path() + QStringLiteral("/nope.json"),
                                                 &error).isEmpty());
    QVERIFY(!error.isEmpty());
    const QString garbage = write(QStringLiteral("garbage.json"), QStringLiteral("no soy json"));
    QVERIFY(st::HeadlessRunner::answersFromValue(garbage, &error).isEmpty());
    QVERIFY(!error.isEmpty());
}

#include "TestHeadless.moc"

void runTestHeadless(int &failures, int argc, char **argv) {
    TestHeadless test;
    failures += QTest::qExec(&test, argc, argv);
}
