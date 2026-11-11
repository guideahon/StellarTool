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
    void repairsBrokenReplacerPhysicsReferences();
    void adaptsReplacerMeshToCurrentVanillaSchema();
    void removesUnresolvedReplacerDependencyChains();
    void removesUnusedUnknownReplacerImports();
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

void TestCnsConverter::repairsBrokenReplacerPhysicsReferences() {
    const auto import = [](const QString &object, const QString &classPackage,
                           const QString &className, int outer) {
        return QJsonObject{{QStringLiteral("ObjectName"), object},
                           {QStringLiteral("ClassPackage"), classPackage},
                           {QStringLiteral("ClassName"), className},
                           {QStringLiteral("OuterIndex"), outer}};
    };
    const auto property = [](const QString &name, int value) {
        return QJsonObject{{QStringLiteral("$type"),
                            QStringLiteral("UAssetAPI.PropertyTypes.Objects.ObjectPropertyData, UAssetAPI")},
                           {QStringLiteral("Name"), name},
                           {QStringLiteral("Value"), value}};
    };
    const QString root = QStringLiteral("/Game/Outfit/MeshRoot");
    QJsonObject output{
        {QStringLiteral("NameMap"), QJsonArray{}},
        {QStringLiteral("Imports"), QJsonArray{
             import(QStringLiteral("/Engine/UnknownPackage"), QStringLiteral("/Script/CoreUObject"),
                    QStringLiteral("Package"), 0),
             import(QStringLiteral("UnknownExport"), QStringLiteral("/Script/CoreUObject"),
                    QStringLiteral("Object"), -1)}},
        {QStringLiteral("Exports"), QJsonArray{QJsonObject{
             {QStringLiteral("ObjectName"), QStringLiteral("MeshRoot")},
             {QStringLiteral("Data"), QJsonArray{property(QStringLiteral("PhysicsAsset"), -2)}},
             {QStringLiteral("SerializationBeforeCreateDependencies"), QJsonArray{}}}}}
    };
    QJsonObject vanilla{
        {QStringLiteral("Imports"), QJsonArray{
             import(root + QStringLiteral("_Physics"), QStringLiteral("/Script/CoreUObject"),
                    QStringLiteral("Package"), 0),
             import(QStringLiteral("MeshRoot_Physics"), QStringLiteral("/Script/Engine"),
                    QStringLiteral("PhysicsAsset"), -1),
             import(QStringLiteral("SkeletalBodySetup_0"), QStringLiteral("/Script/Engine"),
                    QStringLiteral("SkeletalBodySetup"), -2),
             import(root + QStringLiteral("_ShadowPhysics"), QStringLiteral("/Script/CoreUObject"),
                    QStringLiteral("Package"), 0),
             import(QStringLiteral("MeshRoot_ShadowPhysics"), QStringLiteral("/Script/Engine"),
                    QStringLiteral("PhysicsAsset"), -4),
             import(QStringLiteral("SkeletalBodySetup_1"), QStringLiteral("/Script/Engine"),
                    QStringLiteral("SkeletalBodySetup"), -5)}},
        {QStringLiteral("Exports"), QJsonArray{QJsonObject{
             {QStringLiteral("ObjectName"), QStringLiteral("MeshRoot")},
             {QStringLiteral("Data"), QJsonArray{
                  property(QStringLiteral("PhysicsAsset"), -2),
                  property(QStringLiteral("ShadowPhysicsAsset"), -5)}},
             {QStringLiteral("SerializationBeforeCreateDependencies"), QJsonArray{-3, -6}}}}}
    };

    const QJsonObject repaired = CnsConverterService::repairReplacerPhysicsReferences(
        output, vanilla, root);
    const QJsonObject mesh = repaired.value(QStringLiteral("Exports")).toArray().first().toObject();
    const QJsonArray imports = repaired.value(QStringLiteral("Imports")).toArray();
    const QJsonArray data = mesh.value(QStringLiteral("Data")).toArray();
    int physicsRef = 0;
    int shadowRef = 0;
    for (const QJsonValue &value : data) {
        const QJsonObject p = value.toObject();
        if (p.value(QStringLiteral("Name")).toString() == QLatin1String("PhysicsAsset"))
            physicsRef = p.value(QStringLiteral("Value")).toInt();
        if (p.value(QStringLiteral("Name")).toString() == QLatin1String("ShadowPhysicsAsset"))
            shadowRef = p.value(QStringLiteral("Value")).toInt();
    }
    QVERIFY(physicsRef < 0);
    QVERIFY(shadowRef < 0);
    QCOMPARE(imports.at(-physicsRef - 1).toObject().value(QStringLiteral("ObjectName")).toString(),
             QStringLiteral("MeshRoot_Physics"));
    QCOMPARE(imports.at(-shadowRef - 1).toObject().value(QStringLiteral("ObjectName")).toString(),
             QStringLiteral("MeshRoot_ShadowPhysics"));
    QVERIFY(repaired.value(QStringLiteral("NameMap")).toArray().contains(
        QStringLiteral("ShadowPhysicsAsset")));
    const QJsonArray dependencies = mesh.value(
        QStringLiteral("SerializationBeforeCreateDependencies")).toArray();
    QVERIFY(dependencies.size() >= 2);
}

void TestCnsConverter::removesUnusedUnknownReplacerImports() {
    const auto import = [](const QString &object, const QString &classPackage,
                           const QString &className, int outer) {
        return QJsonObject{{QStringLiteral("ObjectName"), object},
                           {QStringLiteral("ClassPackage"), classPackage},
                           {QStringLiteral("ClassName"), className},
                           {QStringLiteral("OuterIndex"), outer}};
    };
    const QString propertyType = QStringLiteral(
        "UAssetAPI.PropertyTypes.Objects.ObjectPropertyData, UAssetAPI");
    QJsonObject asset{
        {QStringLiteral("Imports"), QJsonArray{
             import(QStringLiteral("/Engine/UnknownPackage"), QStringLiteral("/Script/CoreUObject"),
                    QStringLiteral("Package"), 0),
             import(QStringLiteral("UnknownExport"), QStringLiteral("/Script/CoreUObject"),
                    QStringLiteral("Object"), -1),
             import(QStringLiteral("/Script/Engine"), QStringLiteral("/Script/CoreUObject"),
                    QStringLiteral("Package"), 0),
             import(QStringLiteral("SkeletalMesh"), QStringLiteral("/Script/CoreUObject"),
                    QStringLiteral("Class"), -3),
             import(QStringLiteral("/Game/Outfit/Mesh_Physics"), QStringLiteral("/Script/CoreUObject"),
                    QStringLiteral("Package"), 0),
             import(QStringLiteral("Mesh_Physics"), QStringLiteral("/Script/Engine"),
                    QStringLiteral("PhysicsAsset"), -5),
             import(QStringLiteral("/Engine/UnknownPackage"), QStringLiteral("/Script/CoreUObject"),
                    QStringLiteral("Package"), 0),
             import(QStringLiteral("UnknownExport"), QStringLiteral("/Script/CoreUObject"),
                    QStringLiteral("Object"), -7)}},
        {QStringLiteral("Exports"), QJsonArray{QJsonObject{
             {QStringLiteral("ObjectName"), QStringLiteral("Mesh")},
             {QStringLiteral("ClassIndex"), -4},
             {QStringLiteral("Data"), QJsonArray{QJsonObject{
                  {QStringLiteral("$type"), propertyType},
                  {QStringLiteral("Name"), QStringLiteral("PhysicsAsset")},
                  {QStringLiteral("Value"), -6}}}},
             {QStringLiteral("SerializationBeforeCreateDependencies"), QJsonArray{-6, -4}}}}}
    };

    int removed = 0;
    const QJsonObject cleaned = CnsConverterService::pruneUnusedUnknownImports(asset, &removed);
    QCOMPARE(removed, 4);
    const QJsonArray imports = cleaned.value(QStringLiteral("Imports")).toArray();
    QCOMPARE(imports.size(), 4);
    QCOMPARE(imports.at(1).toObject().value(QStringLiteral("OuterIndex")).toInt(), -1);
    QCOMPARE(imports.at(3).toObject().value(QStringLiteral("OuterIndex")).toInt(), -3);
    QCOMPARE(imports.at(3).toObject().value(QStringLiteral("ObjectName")).toString(),
             QStringLiteral("Mesh_Physics"));

    const QJsonObject mesh = cleaned.value(QStringLiteral("Exports")).toArray().first().toObject();
    QCOMPARE(mesh.value(QStringLiteral("ClassIndex")).toInt(), -2);
    QCOMPARE(mesh.value(QStringLiteral("Data")).toArray().first().toObject()
                 .value(QStringLiteral("Value")).toInt(), -4);
    const QJsonArray expectedDependencies{-4, -2};
    QCOMPARE(mesh.value(QStringLiteral("SerializationBeforeCreateDependencies")).toArray(),
             expectedDependencies);
    for (const QJsonValue &value : imports)
        QVERIFY(!value.toObject().value(QStringLiteral("ObjectName")).toString()
                     .contains(QStringLiteral("Unknown"), Qt::CaseInsensitive));
}

void TestCnsConverter::adaptsReplacerMeshToCurrentVanillaSchema() {
    const QString meshName = QStringLiteral("Mesh");
    const QString arrayType = QStringLiteral(
        "UAssetAPI.PropertyTypes.Objects.ArrayPropertyData, UAssetAPI");
    const QString structType = QStringLiteral(
        "UAssetAPI.PropertyTypes.Structs.StructPropertyData, UAssetAPI");
    const QString intType = QStringLiteral(
        "UAssetAPI.PropertyTypes.Objects.IntPropertyData, UAssetAPI");
    const QString byteType = QStringLiteral(
        "UAssetAPI.PropertyTypes.Objects.BytePropertyData, UAssetAPI");
    const QString enumType = QStringLiteral(
        "UAssetAPI.PropertyTypes.Objects.EnumPropertyData, UAssetAPI");
    const QString boolType = QStringLiteral(
        "UAssetAPI.PropertyTypes.Objects.BoolPropertyData, UAssetAPI");
    const QString objectType = QStringLiteral(
        "UAssetAPI.PropertyTypes.Objects.ObjectPropertyData, UAssetAPI");

    const auto scalar = [](const QString &type, const QString &name, const QJsonValue &value) {
        return QJsonObject{{QStringLiteral("$type"), type},
                           {QStringLiteral("Name"), name},
                           {QStringLiteral("Value"), value}};
    };
    const auto structValue = [&](const QString &name, const QJsonArray &fields) {
        return QJsonObject{{QStringLiteral("$type"), structType},
                           {QStringLiteral("Name"), name},
                           {QStringLiteral("Value"), fields}};
    };
    const auto lodInfo = [&](const QString &elementName, const QString &qualityType,
                             int quality, const QString &reductionType,
                             const QJsonValue &reduction) {
        const QJsonArray fields{
            scalar(intType, QStringLiteral("BuildQuality"), quality),
            scalar(reductionType, QStringLiteral("ReductionMethod"), reduction)};
        return QJsonObject{{QStringLiteral("$type"), arrayType},
                           {QStringLiteral("Name"), QStringLiteral("LODInfo")},
                           {QStringLiteral("ArrayType"), QStringLiteral("StructProperty")},
                           {QStringLiteral("Value"), QJsonArray{
                                structValue(elementName, fields)}}};
    };
    const QJsonObject sourceMesh{
        {QStringLiteral("ObjectName"), meshName},
        {QStringLiteral("Extras"), QStringLiteral("mod-mesh-payload")},
        {QStringLiteral("Data"), QJsonArray{
             lodInfo(QStringLiteral("LODInfo"), intType, 99, byteType, 7),
             scalar(boolType, QStringLiteral("bHasVertexColors"), false),
             scalar(objectType, QStringLiteral("PhysicsAsset"), -1),
             scalar(objectType, QStringLiteral("PostProcessAnimBlueprint"), -2)}}};
    const QJsonObject targetMesh{
        {QStringLiteral("ObjectName"), meshName},
        {QStringLiteral("Extras"), QStringLiteral("vanilla-mesh-payload")},
        {QStringLiteral("Data"), QJsonArray{
             scalar(objectType, QStringLiteral("Skeleton"), -1),
             lodInfo(QStringLiteral("0"), intType, 1, enumType,
                     QStringLiteral("SMOT_Default")),
             scalar(boolType, QStringLiteral("bHasVertexColors"), true),
             scalar(objectType, QStringLiteral("PhysicsAsset"), -2),
             scalar(objectType, QStringLiteral("ShadowPhysicsAsset"), -3),
             QJsonObject{{QStringLiteral("$type"), arrayType},
                         {QStringLiteral("Name"), QStringLiteral("MorphTargets")},
                         {QStringLiteral("ArrayType"), QStringLiteral("ObjectProperty")},
                         {QStringLiteral("Value"), QJsonArray{
                              scalar(objectType, QStringLiteral("0"), 1)}}},
             scalar(objectType, QStringLiteral("PostProcessAnimBlueprint"), -4)}}};
    const QJsonObject source{
        {QStringLiteral("NameMap"), QJsonArray{QStringLiteral("ModOnlyName")}},
        {QStringLiteral("Imports"), QJsonArray{}},
        {QStringLiteral("Exports"), QJsonArray{sourceMesh}}};
    const QJsonObject vanilla{
        {QStringLiteral("NameMap"), QJsonArray{QStringLiteral("VanillaName")}},
        {QStringLiteral("Imports"), QJsonArray{}},
        {QStringLiteral("Exports"), QJsonArray{
             QJsonObject{{QStringLiteral("ObjectName"), QStringLiteral("Jaw_open")}},
             targetMesh}}};

    QString error;
    const QJsonObject adapted = CnsConverterService::adaptReplacerMeshToVanillaSchema(
        source, vanilla, QStringLiteral("/Game/Art/Mesh"), true, &error);
    QVERIFY2(!adapted.isEmpty(), qPrintable(error));
    const QJsonArray exports = adapted.value(QStringLiteral("Exports")).toArray();
    QCOMPARE(exports.size(), 2);
    QCOMPARE(exports.at(0).toObject().value(QStringLiteral("ObjectName")).toString(),
             QStringLiteral("Jaw_open"));
    const QJsonObject mesh = exports.at(1).toObject();
    QCOMPARE(mesh.value(QStringLiteral("Extras")).toString(), QStringLiteral("mod-mesh-payload"));
    const QJsonArray properties = mesh.value(QStringLiteral("Data")).toArray();
    auto findProperty = [&](const QString &name) {
        for (const QJsonValue &value : properties)
            if (value.toObject().value(QStringLiteral("Name")).toString() == name)
                return value.toObject();
        return QJsonObject{};
    };
    const QJsonObject adaptedLod = findProperty(QStringLiteral("LODInfo"));
    const QJsonObject adaptedLodEntry = adaptedLod.value(QStringLiteral("Value"))
                                            .toArray().first().toObject();
    const QJsonArray adaptedFields = adaptedLodEntry.value(QStringLiteral("Value")).toArray();
    QCOMPARE(adaptedFields.at(0).toObject().value(QStringLiteral("Value")).toInt(), 99);
    QCOMPARE(adaptedFields.at(1).toObject().value(QStringLiteral("$type")).toString(), enumType);
    QCOMPARE(adaptedFields.at(1).toObject().value(QStringLiteral("Value")).toString(),
             QStringLiteral("SMOT_Default"));
    QCOMPARE(findProperty(QStringLiteral("PhysicsAsset")).value(QStringLiteral("Value")).toInt(), -2);
    QVERIFY(findProperty(QStringLiteral("PostProcessAnimBlueprint")).isEmpty());
    QVERIFY(!findProperty(QStringLiteral("ShadowPhysicsAsset")).isEmpty());
    QCOMPARE(adapted.value(QStringLiteral("NameMap")).toArray().last().toString(),
             QStringLiteral("ModOnlyName"));
}

void TestCnsConverter::removesUnresolvedReplacerDependencyChains() {
    const auto import = [](const QString &object, const QString &className, int outer) {
        return QJsonObject{{QStringLiteral("ObjectName"), object},
                           {QStringLiteral("ClassPackage"), QStringLiteral("/Script/CoreUObject")},
                           {QStringLiteral("ClassName"), className},
                           {QStringLiteral("OuterIndex"), outer}};
    };
    QJsonObject asset{
        {QStringLiteral("Imports"), QJsonArray{
             import(QStringLiteral("/Engine/UnknownPackage"), QStringLiteral("Package"), 0),
             import(QStringLiteral("UnknownExport"), QStringLiteral("Object"), -1),
             import(QStringLiteral("/Script/Engine"), QStringLiteral("Package"), 0),
             import(QStringLiteral("SkeletalBodySetup_99"), QStringLiteral("SkeletalBodySetup"), -2),
             import(QStringLiteral("/Game/Physics"), QStringLiteral("Package"), 0),
             import(QStringLiteral("PhysicsAsset"), QStringLiteral("PhysicsAsset"), -5)}},
        {QStringLiteral("Exports"), QJsonArray{QJsonObject{
             {QStringLiteral("ObjectName"), QStringLiteral("Mesh")},
             {QStringLiteral("SerializationBeforeCreateDependencies"), QJsonArray{-4, -6}}}}},
        {QStringLiteral("DependsMap"), QJsonArray{QJsonArray{-4, -6}}}
    };

    int removedDependencies = 0;
    const QJsonObject dependenciesCleaned =
        CnsConverterService::removeUnresolvedReplacerDependencies(asset,
                                                                   &removedDependencies);
    QCOMPARE(removedDependencies, 2);
    const QJsonArray dependencies = dependenciesCleaned.value(QStringLiteral("Exports"))
                                        .toArray().first().toObject()
                                        .value(QStringLiteral("SerializationBeforeCreateDependencies"))
                                        .toArray();
    QCOMPARE(dependencies, QJsonArray{-6});

    int removedImports = 0;
    const QJsonObject importsPruned = CnsConverterService::pruneUnusedUnknownImports(
        dependenciesCleaned, &removedImports);
    QCOMPARE(removedImports, 3);
    QCOMPARE(importsPruned.value(QStringLiteral("Imports")).toArray().size(), 3);
    const QJsonArray remappedDependencies = importsPruned.value(QStringLiteral("Exports"))
                                               .toArray().first().toObject()
                                               .value(QStringLiteral("SerializationBeforeCreateDependencies"))
                                               .toArray();
    QCOMPARE(remappedDependencies, QJsonArray{-3});
    const QJsonArray imports = importsPruned.value(QStringLiteral("Imports")).toArray();
    for (const QJsonValue &value : imports)
        QVERIFY(!value.toObject().value(QStringLiteral("ObjectName")).toString()
                     .contains(QStringLiteral("Unknown"), Qt::CaseInsensitive));
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
