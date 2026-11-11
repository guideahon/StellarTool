#include "core/CnsConverterService.h"

#include <QJsonArray>
#include <QtTest>

using namespace st;

class TestCnsConverter : public QObject {
    Q_OBJECT
private slots:
    void normalizesPaths();
    void replacerRequiresItsRootAsset();
    void createsStableShortId();
    void rewritesUassetReferences();
    void plansFusionCompatibilityCopies();
};

void TestCnsConverter::normalizesPaths() {
    QCOMPARE(CnsConverterService::normalizeAssetPath(
                 QStringLiteral("SB\\Content\\Art\\Suit\\Mesh.uasset")),
             QStringLiteral("/Game/Art/Suit/Mesh"));
    QCOMPARE(CnsConverterService::normalizeAssetPath(
                 QStringLiteral("Lemi21_Mods/Content/Art/Suit/Mesh.uasset")),
             QStringLiteral("/Game/Art/Suit/Mesh"));
    QCOMPARE(CnsConverterService::normalizeAssetPath(
                 QStringLiteral("/Game/Art/Suit/Mesh.Mesh")),
             QStringLiteral("/Game/Art/Suit/Mesh"));
    QVERIFY(CnsConverterService::normalizeAssetPath(QStringLiteral("not/an/asset")).isEmpty());
}

void TestCnsConverter::replacerRequiresItsRootAsset() {
    const QString root = QStringLiteral(
        "/Game/Art/Character/PC/CH_P_EVE_61/CH_P_EVE_61");
    const QString texture = QStringLiteral(
        "/Game/Art/Character/PC/CH_P_EVE_61/Textures/CH_P_EVE_61_A");

    QVERIFY(!CnsConverterService::hasReplacerRootAsset({texture}, root));
    QVERIFY(CnsConverterService::hasReplacerRootAsset({texture, root.toUpper()}, root));
}

void TestCnsConverter::createsStableShortId() {
    QCOMPARE(CnsConverterService::shortId(QStringLiteral("Latex Suit 0")),
             CnsConverterService::shortId(QStringLiteral("Latex Suit 0")));
    QCOMPARE(CnsConverterService::shortId(QStringLiteral("Latex Suit 0")).size(), 16);
    QVERIFY(CnsConverterService::shortId(QStringLiteral("Latex Suit 0"))
            != CnsConverterService::shortId(QStringLiteral("Latex Suit 1")));
}

void TestCnsConverter::rewritesUassetReferences() {
    QJsonObject asset{
        {QStringLiteral("FolderName"), QStringLiteral("/Game/Old/Mesh")},
        {QStringLiteral("NameMap"), QJsonArray{QStringLiteral("/Game/Old/Mesh.Mesh")}},
        {QStringLiteral("Imports"), QJsonArray{QJsonObject{
             {QStringLiteral("ClassPackage"), QStringLiteral("/Game/Old/Material")},
             {QStringLiteral("ObjectName"), QStringLiteral("Material")}
         }}},
        {QStringLiteral("Exports"), QJsonArray{QJsonObject{
             {QStringLiteral("ObjectName"), QStringLiteral("Mesh_LOD0")}
         }}},
        {QStringLiteral("OtherAssetsFailedToAccess"),
         QJsonArray{QStringLiteral("/Game/Old/Material.Material")}}
    };
    QMap<QString, QString> map{
        {QStringLiteral("/game/old/mesh"), QStringLiteral("/Game/CNSRepacked/abc/NewMesh")},
        {QStringLiteral("/game/old/material"), QStringLiteral("/Game/CNSRepacked/abc/NewMaterial")}
    };
    const QJsonObject out = CnsConverterService::relocateAssetJson(asset, map);
    QCOMPARE(out.value(QStringLiteral("FolderName")).toString(),
             QStringLiteral("/Game/CNSRepacked/abc/NewMesh"));
    const auto import = out.value(QStringLiteral("Imports")).toArray().first().toObject();
    QCOMPARE(import.value(QStringLiteral("ClassPackage")).toString(),
             QStringLiteral("/Game/CNSRepacked/abc/NewMaterial"));
    QCOMPARE(import.value(QStringLiteral("ObjectName")).toString(),
             QStringLiteral("NewMaterial"));
    QCOMPARE(out.value(QStringLiteral("Exports")).toArray().first().toObject()
                 .value(QStringLiteral("ObjectName")).toString(),
             QStringLiteral("NewMesh_LOD0"));
    QCOMPARE(out.value(QStringLiteral("OtherAssetsFailedToAccess")).toArray().first().toString(),
             QStringLiteral("/Game/CNSRepacked/abc/NewMaterial.NewMaterial"));
}

void TestCnsConverter::plansFusionCompatibilityCopies() {
    const QString sourceRoot = QStringLiteral(
        "/Game/CNSRepacked/id/Art/Character/PC/CH_P_EVE_Fusion/CH_P_EVE_Fusion");
    const QString materialSource = QStringLiteral("/Game/CNSRepacked/id/Art/Mods/MI_Custom");
    const QString materialTarget = QStringLiteral(
        "/Game/CNSRepacked/id/Art/Mods/MI_Custom");
    const QStringList materialSlots{materialTarget + QStringLiteral(".MI_Custom")};
    const QMap<QString, QString> relocations{
        {sourceRoot.toLower(), sourceRoot},
        {materialSource.toLower(), materialTarget}
    };

    const auto copies = CnsConverterService::fusionCompatibilityCopies(
        sourceRoot, QStringLiteral("/Game/Art/Character/PC/CH_P_EVE_Fusion/CH_P_EVE_Fusion"),
        materialSlots, relocations);
    QCOMPARE(copies.size(), 2);
    QCOMPARE(copies.at(0).sourceAsset, sourceRoot.toLower());
    QCOMPARE(copies.at(0).targetAsset,
             QStringLiteral("/Game/Art/Character/PC/CH_P_EVE_Fusion/CH_P_EVE_Fusion_NoAcc"));
    QCOMPARE(copies.at(1).sourceAsset, materialSource.toLower());
    QCOMPARE(copies.at(1).targetAsset,
             QStringLiteral("/Game/Art/Character/PC/CH_P_EVE_Fusion/Materials/MI_CH_P_EVE_Fusion_Suit01"));

    QVERIFY(CnsConverterService::fusionCompatibilityCopies(
                sourceRoot, QStringLiteral("/Game/Art/Character/PC/CH_P_EVE_08/CH_P_EVE_08"),
                materialSlots, relocations).isEmpty());
}

void runTestCnsConverter(int &failures, int argc, char **argv) {
    TestCnsConverter test;
    failures += QTest::qExec(&test, argc, argv);
}

#include "TestCnsConverter.moc"
