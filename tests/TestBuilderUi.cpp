#include <QtTest>

#include "core/GamePaths.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

class TestBuilderUi : public QObject {
    Q_OBJECT

private slots:
    void itemLabelsAreLocalized();
    void gameRootAcceptsSubfoldersAndParent();
    void cnsConverterUsesThemedCombos();
};

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
        QStringLiteral("builder_q_rest_fx"),
        QStringLiteral("builder_q_shield_regen"),
        QStringLiteral("builder_outfit_fix_hint"),
        QStringLiteral("builder_out_in_mods"),
        QStringLiteral("builder_shadow_title"),
        QStringLiteral("builder_shadow_hint"),
        QStringLiteral("builder_game_pick"),
        QStringLiteral("builder_game_prompt_title"),
        QStringLiteral("builder_game_prompt_body"),
        QStringLiteral("builder_game_later"),
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
