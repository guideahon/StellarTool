#include <QtTest>

#include "core/GamePaths.h"
#include "AppController.h"
#include "Translator.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTemporaryDir>
#include <QUrl>

class TestBuilderUi : public QObject {
    Q_OBJECT

private slots:
    void itemLabelsAreLocalized();
    void gameRootAcceptsSubfoldersAndParent();
    void cnsConverterUsesThemedCombos();
    void presetFilesRoundTrip();
    void presetImportRejectsForeignFiles();
};

namespace {
// QSettings del proceso de test a un INI temporal: los presets del usuario
// (HKCU\StellarTool) no se tocan.
QString redirectSettings(QTemporaryDir &dir) {
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
    return dir.path();
}

QJsonObject presetIoResult(const QString &json) {
    return QJsonDocument::fromJson(json.toUtf8()).object();
}
}   // namespace

// Un preset se exporta a archivo y vuelve a entrar con las mismas respuestas:
// es lo que hace compartible una config del Builder sin publicar un pak.
void TestBuilderUi::presetFilesRoundTrip() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    redirectSettings(tmp);

    st::Translator i18n;
    st::AppController app(&i18n);
    const QString answers = QStringLiteral(
        "{\"combatProfile\":\"full\",\"worldTweaks\":[\"shopPrices\"],"
        "\"worldTweakValues\":{\"shop_prices\":{\"price_percent\":25}}}");
    QVERIFY(app.saveBuilderPreset(QStringLiteral("Mi build"), answers));

    const QString file = tmp.path() + QStringLiteral("/mi-build.stpreset");
    const QJsonObject exported = presetIoResult(
        app.exportBuilderPreset(QStringLiteral("Mi build"), QUrl::fromLocalFile(file)));
    QVERIFY2(exported.value(QStringLiteral("ok")).toBool(),
             qPrintable(exported.value(QStringLiteral("error")).toString()));
    QVERIFY(QFile::exists(file));

    // Importar con el mismo nombre no pisa el preset que ya estaba guardado.
    const QJsonObject imported = presetIoResult(
        app.importBuilderPreset(QUrl::fromLocalFile(file)));
    QVERIFY2(imported.value(QStringLiteral("ok")).toBool(),
             qPrintable(imported.value(QStringLiteral("error")).toString()));
    QCOMPARE(imported.value(QStringLiteral("name")).toString(),
             QStringLiteral("Mi build (2)"));

    const QJsonArray presets = QJsonDocument::fromJson(app.builderPresets().toUtf8()).array();
    QCOMPARE(presets.size(), 2);
    const QJsonObject copy = presets.at(0).toObject();
    QCOMPARE(copy.value(QStringLiteral("name")).toString(), QStringLiteral("Mi build (2)"));
    QCOMPARE(copy.value(QStringLiteral("answers")).toObject(),
             QJsonDocument::fromJson(answers.toUtf8()).object());
}

// Un JSON cualquiera (o de una version futura) no entra como preset: importar a
// ciegas dejaria respuestas sin sentido en el cuestionario.
void TestBuilderUi::presetImportRejectsForeignFiles() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    redirectSettings(tmp);

    st::Translator i18n;
    st::AppController app(&i18n);

    const auto write = [&tmp](const QString &name, const QString &body) {
        const QString path = tmp.path() + QLatin1Char('/') + name;
        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write(body.toUtf8());
        f.close();
        return QUrl::fromLocalFile(path);
    };

    const QUrl foreign = write(QStringLiteral("otro.stpreset"),
                               QStringLiteral("{\"answers\":{\"combatProfile\":\"full\"}}"));
    QVERIFY(!presetIoResult(app.importBuilderPreset(foreign))
                 .value(QStringLiteral("ok")).toBool());

    const QUrl newer = write(QStringLiteral("futuro.stpreset"), QStringLiteral(
        "{\"format\":\"stellartool.builder-preset\",\"schemaVersion\":99,"
        "\"answers\":{\"combatProfile\":\"full\"}}"));
    QVERIFY(!presetIoResult(app.importBuilderPreset(newer))
                 .value(QStringLiteral("ok")).toBool());

    const QUrl missing = QUrl::fromLocalFile(tmp.path() + QStringLiteral("/no-existe.stpreset"));
    QVERIFY(!presetIoResult(app.importBuilderPreset(missing))
                 .value(QStringLiteral("ok")).toBool());

    QCOMPARE(QJsonDocument::fromJson(app.builderPresets().toUtf8()).array().size(), 0);
}

void TestBuilderUi::itemLabelsAreLocalized() {
    const QString sourceDir = QString::fromUtf8(ST_SOURCE_DIR);
    const QStringList keys = {
        QStringLiteral("builder_item_slug"),
        QStringLiteral("builder_item_blaster_cell"),
        QStringLiteral("builder_item_stinger"),
        QStringLiteral("builder_item_shotgun_shell"),
        QStringLiteral("builder_item_nikke_ammo"),
        QStringLiteral("builder_item_explosive_shell"),
        QStringLiteral("builder_item_shock_grenade"),
        QStringLiteral("builder_item_lingering_potion"),
        QStringLiteral("builder_item_smart_mine"),
        QStringLiteral("builder_item_concentrated_potion"),
        QStringLiteral("builder_item_pulse_grenade"),
        QStringLiteral("builder_item_wb_pump"),
        QStringLiteral("builder_item_sonic_grenade"),
        QStringLiteral("builder_q_shield_regen"),
        QStringLiteral("builder_outfit_fix_hint"),
        QStringLiteral("builder_q_outfit_mode"),
        QStringLiteral("builder_q_outfit_off"),
        QStringLiteral("builder_q_outfit"),
        QStringLiteral("builder_q_outfit_no_helper"),
        QStringLiteral("builder_helper_last_nocns"),
        QStringLiteral("builder_helper_nocns_hint"),
        QStringLiteral("builder_out_in_mods"),
        QStringLiteral("builder_shadow_title"),
        QStringLiteral("builder_shadow_hint"),
        QStringLiteral("builder_game_pick"),
        QStringLiteral("builder_game_prompt_title"),
        QStringLiteral("builder_game_prompt_body"),
        QStringLiteral("builder_game_later"),
        QStringLiteral("builder_world_title"),
        QStringLiteral("builder_world_shop"),
        QStringLiteral("builder_world_drops"),
        QStringLiteral("builder_world_sp"),
        QStringLiteral("builder_world_upgrades"),
        QStringLiteral("builder_world_fishing"),
        QStringLiteral("builder_value_percent"),
        QStringLiteral("builder_value_chance"),
        QStringLiteral("builder_preset_export"),
        QStringLiteral("builder_preset_import"),
        QStringLiteral("err_preset_format"),
        QStringLiteral("err_preset_newer"),
    };

    QDir translations(sourceDir + QStringLiteral("/i18n"));
    const QStringList files = translations.entryList(
        {QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    QVERIFY(!files.isEmpty());
    for (const QString &fileName : files) {
        QFile file(translations.filePath(fileName));
        QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.fileName()));
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
        QVERIFY2(error.error == QJsonParseError::NoError,
                 qPrintable(fileName + QStringLiteral(": ") + error.errorString()));
        const QJsonObject strings = document.object();
        for (const QString &key : keys) {
            QVERIFY2(!strings.value(key).toString().trimmed().isEmpty(),
                     qPrintable(fileName + QStringLiteral(" is missing ") + key));
        }
    }

    QFile qml(sourceDir + QStringLiteral("/qml/pages/BuilderPage.qml"));
    QVERIFY(qml.open(QIODevice::ReadOnly));
    const QByteArray source = qml.readAll();
    QVERIFY(!source.contains("label: \"StackBullet"));
    QVERIFY(!source.contains("label: \"StackConsumable"));
    QVERIFY(source.contains("technicalName: \"StackBullet1\""));
    QVERIFY(source.contains("technicalName: \"StackConsumable7\""));
}

// El picker de carpeta del juego tiene que perdonar lo que el usuario elige:
// la raiz, una subcarpeta (Paks, ~mods) o la carpeta que contiene el juego.
void TestBuilderUi::gameRootAcceptsSubfoldersAndParent() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString root = tmp.path() + QStringLiteral("/StellarBlade");
    const QString paks = root + QStringLiteral("/SB/Content/Paks");
    QVERIFY(QDir().mkpath(paks + QStringLiteral("/~mods")));
    QFile utoc(paks + QStringLiteral("/global.utoc"));
    QVERIFY(utoc.open(QIODevice::WriteOnly));
    utoc.write("x");
    utoc.close();

    QCOMPARE(st::GamePaths::normalizeRoot(root), root);
    QCOMPARE(st::GamePaths::normalizeRoot(paks), root);
    QCOMPARE(st::GamePaths::normalizeRoot(paks + QStringLiteral("/~mods")), root);
    QCOMPARE(st::GamePaths::normalizeRoot(tmp.path()), root);          // carpeta padre
    QVERIFY(st::GamePaths::normalizeRoot(tmp.path() + QStringLiteral("/nope")).isEmpty());
    QVERIFY(st::GamePaths::normalizeRoot(QString()).isEmpty());
}

void TestBuilderUi::cnsConverterUsesThemedCombos() {
    const QString sourceDir = QString::fromUtf8(ST_SOURCE_DIR);
    QFile qml(sourceDir + QStringLiteral("/qml/pages/CnsConverterPage.qml"));
    QVERIFY(qml.open(QIODevice::ReadOnly));
    const QByteArray source = qml.readAll();
    QVERIFY(source.contains("import \"../components\""));
    QCOMPARE(source.count("FieldCombo {"), 2);
    QVERIFY(!source.contains("ComboBox {"));
}

#include "TestBuilderUi.moc"

void runTestBuilderUi(int &failures, int argc, char **argv) {
    TestBuilderUi test;
    failures += QTest::qExec(&test, argc, argv);
}
