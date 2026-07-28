#include <QtTest>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

class TestBuilderUi : public QObject {
    Q_OBJECT

private slots:
    void itemLabelsAreLocalized();
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

#include "TestBuilderUi.moc"

void runTestBuilderUi(int &failures, int argc, char **argv) {
    TestBuilderUi test;
    failures += QTest::qExec(&test, argc, argv);
}
