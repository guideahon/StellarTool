#include "CnsConverterService.h"

#include "Cue4Service.h"
#include "GamePaths.h"
#include "PakService.h"
#include "UAssetService.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <algorithm>

namespace {

QString key(const QString &path) {
    return QDir::fromNativeSeparators(path).trimmed().toLower();
}

QString withoutObject(const QString &value) {
    if (!value.startsWith(QLatin1String("/Game/"))) return value;
    const int dot = value.indexOf(QLatin1Char('.'));
    return dot < 0 ? value : value.left(dot);
}

QString alternativeName(const QString &value) {
    const int underscore = value.lastIndexOf(QLatin1Char('_'));
    if (underscore > 0) {
        bool ok = false;
        value.mid(underscore + 1).toInt(&ok);
        if (ok) return value.left(underscore);
    }
    return value;
}

QString replaceInsensitive(QString text, const QString &before, const QString &after) {
    int from = 0;
    while ((from = text.indexOf(before, from, Qt::CaseInsensitive)) >= 0) {
        text.replace(from, before.size(), after);
        from += after.size();
    }
    return text;
}

QJsonValue replaceJsonStrings(const QJsonValue &value,
                              const std::function<QString(const QString &)> &replace) {
    if (value.isString()) return replace(value.toString());
    if (value.isArray()) {
        QJsonArray out;
        for (const auto &item : value.toArray()) out.append(replaceJsonStrings(item, replace));
        return out;
    }
    if (value.isObject()) {
        QJsonObject out;
        const auto object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it)
            out.insert(it.key(), replaceJsonStrings(it.value(), replace));
        return out;
    }
    return value;
}

bool copyFileReplacing(const QString &source, const QString &target, QString *error) {
    QDir().mkpath(QFileInfo(target).absolutePath());
    QFile::remove(target);
    if (QFile::copy(source, target)) return true;
    if (error) *error = QStringLiteral("No se pudo copiar %1 a %2").arg(source, target);
    return false;
}

// "/Game/A/B/MI_X.0" o "/Game/A/B/MI_X.MI_X" -> "/Game/A/B/MI_X".
QString packageOf(const QString &objectPath) {
    const int dot = objectPath.lastIndexOf(QLatin1Char('.'));
    return dot < 0 ? objectPath : objectPath.left(dot);
}

// "/Game/A/B/MI_X" -> "/Game/A/B/MI_X.MI_X" (formato que espera el descriptor).
QString objectRef(const QString &package) {
    return package + QLatin1Char('.') + package.section(QLatin1Char('/'), -1);
}

// "MaterialInstanceConstant'MI_Cowbkn'" -> "MI_Cowbkn".
QString quotedName(const QString &objectName) {
    const int open = objectName.indexOf(QLatin1Char('\''));
    const int close = objectName.lastIndexOf(QLatin1Char('\''));
    if (open < 0 || close <= open) return objectName;
    return objectName.mid(open + 1, close - open - 1);
}

// "EMaterialParameterAssociation::GlobalParameter" -> "Global".
QString associationOf(const QString &value) {
    const QString tail = value.section(QLatin1String("::"), -1);
    if (tail.startsWith(QLatin1String("Layer"))) return QStringLiteral("Layer");
    if (tail.startsWith(QLatin1String("Blend"))) return QStringLiteral("Blend");
    return QStringLiteral("Global");
}

// cue4parse escribe UTF-8 con BOM; QJsonDocument no lo tolera.
QJsonDocument readJsonFile(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QByteArray data = file.readAll();
    if (data.startsWith("\xEF\xBB\xBF")) data.remove(0, 3);
    return QJsonDocument::fromJson(data);
}

// Patrón -p de cue4parse para un paquete /Game/... exacto.
QString packagePattern(const QString &package) {
    return QStringLiteral("SB/Content/") + package.mid(6) + QStringLiteral(".uasset");
}

QStringList splitCsv(const QString &line) {
    // El archivo generado upstream no usa comillas ni comas escapadas.
    return line.split(QLatin1Char(','), Qt::KeepEmptyParts);
}

} // namespace

namespace st {

CnsConverterService::CnsConverterService(PakService *pak, UAssetService *uasset,
                                         Cue4Service *cue4, QObject *parent)
    : QObject(parent), m_pak(pak), m_uasset(uasset), m_cue4(cue4) {}

QString CnsConverterService::stageWithGame(const QString &utocPath,
                                           const QString &workDir) const {
    // CUE4Parse necesita el juego entero montado, no sólo el global: los slots
    // que apuntan a un material vanilla (el de piel, justamente) quedan en null
    // si ese paquete no está montado, y entonces no hay nada que redirigir.
    // Los contenedores vanilla se hardlinkean (instantáneo, 0 bytes de disco);
    // el stage va en el volumen del juego pero FUERA de Paks, para no dejar un
    // mod fantasma si el proceso muere.
    const QString paksDir = GamePaths::paksDir();
    if (paksDir.isEmpty()) return {};
    QString stage = GamePaths::cue4StageDir();
    QDir(stage).removeRecursively();
    if (!QDir().mkpath(stage)) {
        stage = QDir(workDir).filePath(QStringLiteral("cue4stage"));
        QDir(stage).removeRecursively();
        if (!QDir().mkpath(stage)) return {};
    }
    // Sólo el nivel superior de Paks: los mods de ~mods no hacen falta y
    // podrían pisar los assets vanilla que queremos leer.
    const QDir vanilla(paksDir);
    for (const QString &f : vanilla.entryList({QStringLiteral("*.utoc"), QStringLiteral("*.ucas"),
                                               QStringLiteral("*.pak"), QStringLiteral("*.sig")},
                                              QDir::Files))
        PakService::linkOrCopy(vanilla.filePath(f), QDir(stage).filePath(f));
    const QFileInfo utoc(utocPath);
    for (const QString &ext : {QStringLiteral("utoc"), QStringLiteral("ucas"),
                               QStringLiteral("pak")}) {
        const QString f = utoc.absolutePath() + QLatin1Char('/')
                          + utoc.completeBaseName() + QLatin1Char('.') + ext;
        if (QFileInfo::exists(f)) QFile::copy(f, QDir(stage).filePath(QFileInfo(f).fileName()));
    }
    return stage;
}

QStringList CnsConverterService::meshMaterialSlots(const QString &mountDir,
                                                   const QString &meshPath,
                                                   const QString &workDir) const {
    if (!m_cue4 || !m_cue4->available() || mountDir.isEmpty() || meshPath.isEmpty()) return {};
    const QString out = QDir(workDir).filePath(QStringLiteral("cue4mesh"));
    const auto exported = m_cue4->exportPackages(mountDir, out, UAssetService::usmapPath(),
                                                 {packagePattern(meshPath)});
    if (exported.isEmpty()) return {};
    const auto doc = readJsonFile(exported.first());
    QStringList slotList;
    for (const auto &objectValue : doc.array()) {
        const QJsonObject object = objectValue.toObject();
        if (object.value(QStringLiteral("Type")).toString()
            != QLatin1String("SkeletalMesh")) continue;
        for (const auto &slotValue : object.value(QStringLiteral("SkeletalMaterials")).toArray()) {
            const QJsonObject material =
                slotValue.toObject().value(QStringLiteral("Material")).toObject();
            const QString package = packageOf(
                material.value(QStringLiteral("ObjectPath")).toString());
            if (package.isEmpty()) { slotList << QString(); continue; }
            const QString name = quotedName(material.value(QStringLiteral("ObjectName")).toString());
            slotList << package + QLatin1Char('.')
                     + (name.isEmpty() ? package.section(QLatin1Char('/'), -1) : name);
        }
        break;
    }
    return slotList;
}

QJsonArray CnsConverterService::textureParameterOverrides(
    const QString &mountDir, const QStringList &slotList,
    const QMap<QString, QString> &relocations,
    const QString &workDir, QSet<QString> *covered) const {
    QJsonArray parameters;
    if (!m_cue4 || !m_cue4->available() || mountDir.isEmpty() || slotList.isEmpty())
        return parameters;

    // Sólo los slots vanilla: los materiales del propio mod ya se relocalizaron
    // junto con sus texturas y apuntan bien.
    QStringList patterns;
    QMap<QString, int> indexOfPackage;
    for (int i = 0; i < slotList.size(); ++i) {
        const QString package = packageOf(slotList[i]);
        if (package.isEmpty() || relocations.contains(key(package))) continue;
        if (indexOfPackage.contains(key(package))) continue;
        indexOfPackage.insert(key(package), i);
        patterns << packagePattern(package);
    }
    if (patterns.isEmpty()) return parameters;

    const QString out = QDir(workDir).filePath(QStringLiteral("cue4mat"));
    const auto exported = m_cue4->exportPackages(mountDir, out, UAssetService::usmapPath(),
                                                 patterns);
    for (auto it = exported.begin(); it != exported.end(); ++it) {
        // exportPackages devuelve nombre-de-asset -> json. Ubicamos el slot por
        // la hoja de la ruta del paquete, que es ese mismo nombre.
        int materialIndex = -1;
        for (auto p = indexOfPackage.begin(); p != indexOfPackage.end(); ++p)
            if (p.key().section(QLatin1Char('/'), -1) == it.key().toLower())
                materialIndex = p.value();
        if (materialIndex < 0) continue;
        const auto doc = readJsonFile(it.value());
        for (const auto &objectValue : doc.array()) {
            const QJsonObject object = objectValue.toObject();
            const auto textures = object.value(QStringLiteral("Properties")).toObject()
                                      .value(QStringLiteral("TextureParameterValues")).toArray();
            for (const auto &textureValue : textures) {
                const QJsonObject entry = textureValue.toObject();
                const QString target = packageOf(
                    entry.value(QStringLiteral("ParameterValue")).toObject()
                         .value(QStringLiteral("ObjectPath")).toString());
                const QString relocated = relocations.value(key(target));
                if (relocated.isEmpty()) continue;  // el mod no pisa esta textura
                const QJsonObject info = entry.value(QStringLiteral("ParameterInfo")).toObject();
                parameters.append(QJsonObject{
                    {QStringLiteral("MaterialIndex"), materialIndex},
                    {QStringLiteral("LayerIndex"),
                     info.value(QStringLiteral("Index")).toInt(-1)},
                    {QStringLiteral("ParamType"), QStringLiteral("Texture")},
                    {QStringLiteral("ParamName"), info.value(QStringLiteral("Name")).toString()},
                    {QStringLiteral("Association"),
                     associationOf(info.value(QStringLiteral("Association")).toString())},
                    {QStringLiteral("Value"), objectRef(relocated)}
                });
                if (covered) covered->insert(key(target));
            }
        }
    }
    return parameters;
}

QString CnsConverterService::normalizeAssetPath(const QString &path) {
    QString p = QDir::fromNativeSeparators(path.trimmed());
    const int content = p.indexOf(QLatin1String("/Content/"), 0, Qt::CaseInsensitive);
    if (content >= 0)
        p = QStringLiteral("/Game/") + p.mid(content + 9);
    else if (p.startsWith(QLatin1String("Content/"), Qt::CaseInsensitive))
        p = QStringLiteral("/Game/") + p.mid(8);
    if (!p.startsWith(QLatin1String("/Game/"), Qt::CaseInsensitive)) return {};
    p = withoutObject(p);
    return QStringLiteral("/Game/") + p.mid(6);
}

QString CnsConverterService::shortId(const QString &text) {
    return QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(),
                                                        QCryptographicHash::Sha256).toHex().left(16));
}

QString CnsConverterService::dataRoot() {
    const QString env = QProcessEnvironment::systemEnvironment()
                            .value(QStringLiteral("ST_CNSREPACKER_DATA"));
    QStringList candidates;
    if (!env.isEmpty()) candidates << env;
    candidates << QCoreApplication::applicationDirPath() + QStringLiteral("/tools/CNSRepacker")
               << QCoreApplication::applicationDirPath() + QStringLiteral("/../../tools/CNSRepacker");
    const QString game = GamePaths::gameRoot();
    if (!game.isEmpty()) candidates << game + QStringLiteral("/CNSRepacker");
    candidates << QStringLiteral("C:/Program Files (x86)/Steam/steamapps/common/"
                                 "StellarBlade/CNSRepacker");
    for (const QString &candidate : candidates) {
        const QString root = QFileInfo(candidate).absoluteFilePath();
        if (QFileInfo::exists(root + QStringLiteral("/data/rootAssetToInfo.txt"))
            && QFileInfo::exists(root + QStringLiteral("/data/assetToRootAsset.txt")))
            return root;
    }
    return {};
}

bool CnsConverterService::loadData(QString *error) const {
    if (m_loaded) return true;
    const QString root = dataRoot();
    if (root.isEmpty()) {
        if (error) *error = QStringLiteral(
            "No se encontraron los datos de CNSRepacker. Instalalos en "
            "<StellarBlade>/CNSRepacker o tools/CNSRepacker.");
        return false;
    }
    auto readPairs = [&](const QString &name, auto consume) -> bool {
        QFile file(root + QStringLiteral("/data/") + name);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            if (error) *error = QStringLiteral("No se pudo leer %1").arg(file.fileName());
            return false;
        }
        while (!file.atEnd()) {
            const QString line = QString::fromUtf8(file.readLine()).trimmed();
            if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
            const int equals = line.indexOf(QLatin1Char('='));
            if (equals < 0) continue;
            consume(normalizeAssetPath(line.left(equals)),
                    normalizeAssetPath(line.mid(equals + 1)));
        }
        return true;
    };
    if (!readPairs(QStringLiteral("assetToRootAsset.txt"),
                   [&](const QString &a, const QString &b) {
                       if (!a.isEmpty() && !b.isEmpty()) m_assetToRoots[key(a)].insert(b);
                   })) return false;
    if (!readPairs(QStringLiteral("assetToImportAsset.txt"),
                   [&](const QString &a, const QString &b) {
                       if (!a.isEmpty() && !b.isEmpty()) m_assetToImports[key(a)].insert(b);
                   })) return false;

    QFile infos(root + QStringLiteral("/data/rootAssetToInfo.txt"));
    if (!infos.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) *error = QStringLiteral("No se pudo leer %1").arg(infos.fileName());
        return false;
    }
    while (!infos.atEnd()) {
        const QString line = QString::fromUtf8(infos.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
        const int equals = line.indexOf(QLatin1Char('='));
        if (equals < 0) continue;
        const QStringList v = splitCsv(line.mid(equals + 1));
        if (v.size() < 11) continue;
        AssetInfo info;
        info.id = v[0];
        info.path = normalizeAssetPath(v[1]);
        info.characterId = v[2];
        info.fitMeshType = v[3];
        info.meshSubType = v[4];
        info.iconObjectPath = v[5];
        info.animationBPPath = normalizeAssetPath(v[6]);
        info.requirementDLC = v[7];
        info.ponyPhysics = normalizeAssetPath(v[8]);
        info.forLongHair = v[9].compare(QLatin1String("true"), Qt::CaseInsensitive) == 0;
        info.name = v[10];
        const QString rootPath = normalizeAssetPath(line.left(equals));
        if (info.path.isEmpty()) info.path = rootPath;
        m_rootInfos.insert(key(rootPath), info);
        QString replacer;
        if (info.characterId.compare(QLatin1String("EVE"), Qt::CaseInsensitive) != 0)
            replacer += info.characterId + QLatin1Char(' ');
        if (info.fitMeshType.compare(QLatin1String("Body"), Qt::CaseInsensitive) != 0)
            replacer += info.fitMeshType + QLatin1Char(' ');
        if (info.fitMeshType.compare(QLatin1String("Face"), Qt::CaseInsensitive) != 0)
            replacer += info.name;
        replacer = replacer.trimmed();
        if (!replacer.isEmpty()) m_replacerInfos[key(replacer)].append(info);
    }
    QFile excluded(root + QStringLiteral("/data/excludedAssets.txt"));
    if (excluded.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!excluded.atEnd()) {
            const QString path = normalizeAssetPath(QString::fromUtf8(excluded.readLine()).trimmed());
            if (!path.isEmpty()) m_excluded.insert(key(path));
        }
    }
    m_loaded = true;
    return true;
}

QStringList CnsConverterService::replacementNames(QString *error) const {
    if (!loadData(error)) return {};
    QStringList result;
    for (auto it = m_replacerInfos.begin(); it != m_replacerInfos.end(); ++it) {
        const AssetInfo &info = it.value().first();
        QString name;
        if (info.characterId.compare(QLatin1String("EVE"), Qt::CaseInsensitive) != 0)
            name += info.characterId + QLatin1Char(' ');
        if (info.fitMeshType.compare(QLatin1String("Body"), Qt::CaseInsensitive) != 0)
            name += info.fitMeshType + QLatin1Char(' ');
        if (info.fitMeshType.compare(QLatin1String("Face"), Qt::CaseInsensitive) != 0)
            name += info.name;
        result << name.trimmed();
    }
    result.removeDuplicates();
    result.sort(Qt::CaseInsensitive);
    return result;
}

QString CnsConverterService::assetPathForFile(const QString &file,
                                              const QString &assetsDir) const {
    QString relative = QDir(assetsDir).relativeFilePath(file);
    relative = QDir::fromNativeSeparators(relative);
    relative = relative.left(relative.lastIndexOf(QLatin1Char('.')));
    return normalizeAssetPath(relative);
}

QString CnsConverterService::fileForAsset(const QString &path, const QString &assetsDir,
                                          const QString &extension) const {
    return QDir(assetsDir).filePath(QStringLiteral("SB/Content/")
           + normalizeAssetPath(path).mid(6) + QLatin1Char('.') + extension);
}

bool CnsConverterService::prepareInput(const QString &input, const QString &stage,
                                       QString *assetsDir, QString *error) const {
    const QFileInfo fi(input);
    const QString source = QDir(stage).filePath(QStringLiteral("source"));
    QDir().mkpath(source);
    if (fi.isFile() && fi.suffix().compare(QLatin1String("zip"), Qt::CaseInsensitive) == 0) {
        emit const_cast<CnsConverterService *>(this)->progress(
            QStringLiteral("Extrayendo ZIP de outfit..."));
        if (!m_pak->extractZip(input, source, error)) return false;
    } else if (fi.isDir()) {
        QDirIterator it(input, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString from = it.next();
            const QString to = QDir(source).filePath(QDir(input).relativeFilePath(from));
            if (!copyFileReplacing(from, to, error)) return false;
        }
    } else if (fi.isFile()) {
        if (!copyFileReplacing(input, QDir(source).filePath(fi.fileName()), error)) return false;
    } else {
        if (error) *error = QStringLiteral("La entrada no existe: %1").arg(input);
        return false;
    }

    QStringList utocs;
    QDirIterator ui(source, {QStringLiteral("*.utoc")}, QDir::Files,
                    QDirIterator::Subdirectories);
    while (ui.hasNext()) utocs << ui.next();
    if (!utocs.isEmpty()) {
        const QString legacy = QDir(stage).filePath(QStringLiteral("legacy"));
        emit const_cast<CnsConverterService *>(this)->progress(
            QStringLiteral("Convirtiendo IoStore a assets editables..."));
        QString localError;
        if (m_pak->toLegacyMounted(source, legacy, &localError) <= 0) {
            if (error) *error = localError;
            return false;
        }
        *assetsDir = legacy;
        return true;
    }
    QStringList paks;
    QDirIterator pi(source, {QStringLiteral("*.pak")}, QDir::Files,
                    QDirIterator::Subdirectories);
    while (pi.hasNext()) paks << pi.next();
    if (!paks.isEmpty()) {
        const QString legacy = QDir(stage).filePath(QStringLiteral("legacy"));
        for (const QString &pak : paks) {
            QString localError;
            if (!m_pak->unpack(pak, legacy, &localError)) {
                if (error) *error = localError;
                return false;
            }
        }
        *assetsDir = legacy;
        return true;
    }
    *assetsDir = source;
    return true;
}

QMap<QString, CnsConverterService::AssetInfo>
CnsConverterService::readCnsDescriptors(const QString &inputRoot, QString *error) const {
    QMap<QString, AssetInfo> result;
    QDirIterator it(inputRoot, {QStringLiteral("*.dekcns.json")}, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QFile file(it.next());
        if (!file.open(QIODevice::ReadOnly)) continue;
        QJsonParseError parse;
        const auto doc = QJsonDocument::fromJson(file.readAll(), &parse);
        if (parse.error != QJsonParseError::NoError || !doc.isArray()) {
            if (error) *error = QStringLiteral("Descriptor CNS inválido: %1").arg(file.fileName());
            continue;
        }
        for (const auto &entryValue : doc.array()) {
            const QJsonObject entry = entryValue.toObject();
            const QStringList names = [&] {
                QStringList out;
                for (const auto &v : entry.value(QStringLiteral("OutfitNames")).toArray())
                    out << v.toString();
                return out;
            }();
            const auto datas = entry.value(QStringLiteral("OutfitDatas")).toArray();
            for (int i = 0; i < datas.size(); ++i) {
                const QJsonObject data = datas[i].toObject();
                AssetInfo info;
                info.path = normalizeAssetPath(data.value(QStringLiteral("Mesh")).toString());
                info.ponyPhysics = normalizeAssetPath(data.value(QStringLiteral("PonyPhysics")).toString());
                info.characterId = entry.value(QStringLiteral("CharacterID")).toString(QStringLiteral("EVE"));
                info.fitMeshType = entry.value(QStringLiteral("FitMeshType")).toString(QStringLiteral("Body"));
                info.meshSubType = entry.value(QStringLiteral("MeshSubType")).toString();
                info.animationBPPath = normalizeAssetPath(entry.value(QStringLiteral("AnimationBP")).toString());
                info.iconObjectPath = entry.value(QStringLiteral("OutfitImage")).toString();
                info.requirementDLC = entry.value(QStringLiteral("Requirement")).toString();
                info.name = names.value(i);
                if (!info.path.isEmpty()) result.insert(key(info.path), info);
            }
            const auto paths = entry.value(QStringLiteral("OutfitPaths")).toArray();
            for (int i = 0; i < paths.size(); ++i) {
                AssetInfo info;
                info.path = normalizeAssetPath(paths[i].toString());
                info.characterId = entry.value(QStringLiteral("CharacterID")).toString(QStringLiteral("EVE"));
                info.fitMeshType = entry.value(QStringLiteral("FitMeshType")).toString(QStringLiteral("Body"));
                info.name = names.value(i);
                if (!info.path.isEmpty()) result.insert(key(info.path), info);
            }
        }
    }
    return result;
}

QStringList CnsConverterService::discoverRoots(
    const QStringList &moddedAssets, const QMap<QString, AssetInfo> &cnsInfos) const {
    if (!cnsInfos.isEmpty()) {
        QStringList roots;
        for (const auto &info : cnsInfos) roots << info.path;
        roots.removeDuplicates();
        return roots;
    }
    QSet<QString> explicitRoots;
    QSet<QString> implicitRoots;
    for (const QString &asset : moddedAssets) {
        const auto roots = m_assetToRoots.value(key(asset));
        implicitRoots.unite(roots);
        if (roots.size() == 1 && key(*roots.begin()) == key(asset)) explicitRoots.insert(asset);
    }
    const QSet<QString> selected = explicitRoots.isEmpty() ? implicitRoots : explicitRoots;
    QStringList result(selected.begin(), selected.end());
    std::sort(result.begin(), result.end(), [](const QString &a, const QString &b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });
    return result;
}

QJsonObject CnsConverterService::relocateAssetJson(
    const QJsonObject &asset, const QMap<QString, QString> &relocations) {
    // El orden del NameMap define los índices FName del asset. Preservamos el
    // orden original y sólo agregamos al final; reordenarlo (p. ej. volcando un
    // QSet) reasigna los índices y el juego resuelve nombres equivocados.
    QStringList names;
    QSet<QString> seen;
    auto addName = [&](const QString &name) {
        if (!seen.contains(name)) { seen.insert(name); names << name; }
    };
    for (const auto &v : asset.value(QStringLiteral("NameMap")).toArray())
        addName(v.toString());

    auto relocated = [&](const QString &oldValue) -> QString {
        if (!oldValue.startsWith(QLatin1String("/Game/"))) return oldValue;
        const QString oldAsset = withoutObject(oldValue);
        const QString target = relocations.value(key(oldAsset));
        if (target.isEmpty()) return oldValue;
        QString objectName;
        if (oldValue.contains(QLatin1Char('.'))) objectName = oldValue.section(QLatin1Char('.'), -1);
        const QString oldName = oldAsset.section(QLatin1Char('/'), -1);
        const QString newName = target.section(QLatin1Char('/'), -1);
        if (!objectName.isEmpty() && oldName.compare(newName, Qt::CaseInsensitive) != 0)
            objectName = replaceInsensitive(objectName, oldName, newName);
        const QString value = target + (objectName.isEmpty() ? QString() : QLatin1Char('.') + objectName);
        addName(value);
        addName(alternativeName(value));
        return value;
    };

    auto replaceObjectName = [&](const QString &oldPackage, const QString &newPackage,
                                 const QString &object) {
        if (object.isEmpty() || object.startsWith(QLatin1String("/Game/"))) return relocated(object);
        const QString oldName = withoutObject(oldPackage).section(QLatin1Char('/'), -1);
        const QString newName = withoutObject(newPackage).section(QLatin1Char('/'), -1);
        const QString value = replaceInsensitive(object, oldName, newName);
        addName(value);
        addName(alternativeName(value));
        return value;
    };

    QJsonObject out = asset;
    const QString oldFolder = asset.value(QStringLiteral("FolderName")).toString();
    const QString newFolder = relocated(oldFolder);
    out.insert(QStringLiteral("FolderName"), newFolder);
    QJsonArray imports;
    for (const auto &v : asset.value(QStringLiteral("Imports")).toArray()) {
        QJsonObject entry = v.toObject();
        const QString oldPackage = entry.value(QStringLiteral("ClassPackage")).toString();
        const QString newPackage = relocated(oldPackage);
        entry.insert(QStringLiteral("ClassPackage"), newPackage);
        entry.insert(QStringLiteral("ObjectName"),
                     replaceObjectName(oldPackage, newPackage,
                         entry.value(QStringLiteral("ObjectName")).toString()));
        imports.append(entry);
    }
    out.insert(QStringLiteral("Imports"), imports);
    QJsonArray exports;
    for (const auto &v : asset.value(QStringLiteral("Exports")).toArray()) {
        QJsonObject entry = v.toObject();
        entry.insert(QStringLiteral("ObjectName"),
                     replaceObjectName(oldFolder, newFolder,
                         entry.value(QStringLiteral("ObjectName")).toString()));
        exports.append(entry);
    }
    out.insert(QStringLiteral("Exports"), exports);
    QJsonArray failed;
    for (const auto &v : asset.value(QStringLiteral("OtherAssetsFailedToAccess")).toArray())
        failed.append(relocated(v.toString()));
    out.insert(QStringLiteral("OtherAssetsFailedToAccess"), failed);
    QJsonArray nameMap;
    for (const QString &name : std::as_const(names)) nameMap.append(name);
    out.insert(QStringLiteral("NameMap"), nameMap);
    return out;
}

bool CnsConverterService::writeRelocatedAsset(
    const QString &sourceUasset, const QString &targetUasset,
    const QMap<QString, QString> &relocations, QString *error) const {
    const QString jsonPath = targetUasset + QStringLiteral(".json");
    if (!m_uasset->toJson(sourceUasset, jsonPath, error)) return false;
    QFile in(jsonPath);
    if (!in.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("No se pudo leer JSON temporal %1").arg(jsonPath);
        return false;
    }
    QJsonParseError parse;
    QJsonDocument doc = QJsonDocument::fromJson(in.readAll(), &parse);
    in.close();
    if (parse.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) *error = QStringLiteral("JSON UAssetAPI inválido para %1").arg(sourceUasset);
        return false;
    }
    doc.setObject(relocateAssetJson(doc.object(), relocations));
    QSaveFile out(jsonPath);
    if (!out.open(QIODevice::WriteOnly) || out.write(doc.toJson(QJsonDocument::Indented)) < 0
        || !out.commit()) {
        if (error) *error = QStringLiteral("No se pudo escribir JSON temporal %1").arg(jsonPath);
        return false;
    }
    QFile::remove(targetUasset);
    return m_uasset->fromJson(jsonPath, targetUasset, error);
}

CnsConverterService::Result CnsConverterService::convert(const Request &request) {
    Result result;
    QString error;
    if (!loadData(&error)) { result.error = error; return result; }
    if (!GamePaths::hasGame()) {
        result.error = QStringLiteral("Configurá primero la carpeta de Stellar Blade.");
        return result;
    }
    if (request.inputPath.isEmpty() || request.outputDir.isEmpty()) {
        result.error = QStringLiteral("Elegí una entrada y una carpeta de salida.");
        return result;
    }
    QTemporaryDir temp(QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                        + QStringLiteral("/StellarToolCns-XXXXXX"));
    if (!temp.isValid()) { result.error = QStringLiteral("No se pudo crear la carpeta temporal."); return result; }
    QString assetsDir;
    if (!prepareInput(request.inputPath, temp.path(), &assetsDir, &error)) {
        result.error = error; return result;
    }
    QDirIterator ai(assetsDir, {QStringLiteral("*.uasset")}, QDir::Files,
                    QDirIterator::Subdirectories);
    QMap<QString, QString> assetFiles;
    QStringList moddedAssets;
    while (ai.hasNext()) {
        const QString file = ai.next();
        const QString path = assetPathForFile(file, assetsDir);
        if (!path.isEmpty()) { assetFiles.insert(key(path), file); moddedAssets << path; }
    }
    const auto cnsInfos = readCnsDescriptors(QFileInfo(request.inputPath).isDir()
                                                  ? request.inputPath : temp.path(), &error);
    QStringList roots = discoverRoots(moddedAssets, cnsInfos);
    if (roots.isEmpty()) {
        result.error = QStringLiteral("No se encontró ningún outfit raíz compatible en el mod.");
        return result;
    }
    bool selectionOk = false;
    const int selectedIndex = request.selection.toInt(&selectionOk);
    if (!request.selection.trimmed().isEmpty()) {
        QStringList filtered;
        for (int i = 0; i < roots.size(); ++i) {
            const auto info = cnsInfos.value(key(roots[i]));
            if ((selectionOk && selectedIndex == i + 1)
                || (!selectionOk && info.name.compare(request.selection.trimmed(),
                                                       Qt::CaseInsensitive) == 0))
                filtered << roots[i];
        }
        if (!filtered.isEmpty()) roots = filtered;
    }
    // Una conversión interactiva produce una variante por corrida.
    const QString rootPath = roots.first();
    AssetInfo sourceInfo = cnsInfos.value(key(rootPath), m_rootInfos.value(key(rootPath)));
    if (sourceInfo.path.isEmpty()) sourceInfo.path = rootPath;
    AssetInfo targetInfo;
    if (request.mode == Mode::ToReplacer) {
        const auto targets = m_replacerInfos.value(key(request.replacementName));
        if (targets.isEmpty()) {
            result.error = QStringLiteral("El reemplazo “%1” no existe.").arg(request.replacementName);
            return result;
        }
        auto compatible = std::find_if(targets.begin(), targets.end(), [&](const AssetInfo &candidate) {
            return candidate.characterId.compare(sourceInfo.characterId, Qt::CaseInsensitive) == 0
                && candidate.fitMeshType.compare(sourceInfo.fitMeshType, Qt::CaseInsensitive) == 0;
        });
        if (compatible == targets.end()) {
            result.error = QStringLiteral("El outfit y el reemplazo elegido no son del mismo tipo.");
            return result;
        }
        targetInfo = *compatible;
    }

    const QString name = request.modName.trimmed().isEmpty()
        ? QFileInfo(request.inputPath).completeBaseName() : request.modName.trimmed();
    const QString optionId = shortId(name + QStringLiteral(" 0"));
    const QString packageDir = QDir(temp.path()).filePath(QStringLiteral("package"));
    QDir().mkpath(packageDir);

    // Conservamos todos los assets incluidos en el contenedor. Esto cubre
    // referencias desde exports que no aparecen en el mapa de imports y sigue
    // el modo seguro upstream para conversiones CNS.
    QMap<QString, QString> relocations;
    for (const QString &asset : moddedAssets) {
        QString target = QStringLiteral("/Game/CNSRepacked/%1/%2")
                             .arg(optionId, asset.mid(6));
        if (request.mode == Mode::ToReplacer && key(asset) == key(rootPath))
            target = targetInfo.path;
        else if (request.mode == Mode::ToReplacer && !sourceInfo.ponyPhysics.isEmpty()
                 && key(asset) == key(sourceInfo.ponyPhysics) && !targetInfo.ponyPhysics.isEmpty())
            target = targetInfo.ponyPhysics;
        else if (request.mode == Mode::ToReplacer && !sourceInfo.animationBPPath.isEmpty()
                 && key(asset) == key(sourceInfo.animationBPPath)
                 && !targetInfo.animationBPPath.isEmpty())
            target = targetInfo.animationBPPath;
        relocations.insert(key(asset), target);
    }

    // Un replacer también puede pisar assets de OTRA malla (típico: las texturas
    // y materiales de cara). Relocalizados quedan huérfanos: la malla vanilla que
    // los usaba sigue apuntando a /Game/Art/... Solución: traer esa malla vanilla
    // al paquete. Al pasar por la misma reubicación, sus referencias se reescriben
    // hacia las copias del mod, y se publica como cosmético CNS aparte (Face,
    // Hair, …). Sólo mallas que NO son Body: el Body es el outfit mismo, y una
    // textura de cuerpo compartida cuelga de decenas de outfits.
    const QString rootFolder = rootPath.section(QLatin1Char('/'), 0, -2) + QLatin1Char('/');
    QMap<QString, AssetInfo> extraRoots;   // key(root) -> info vanilla
    if (request.mode == Mode::ToCns) {
        for (const QString &asset : moddedAssets) {
            if (asset.startsWith(rootFolder, Qt::CaseInsensitive)) continue;
            for (const QString &candidate : m_assetToRoots.value(key(asset))) {
                if (key(candidate) == key(rootPath)) continue;
                if (relocations.contains(key(candidate))) continue;   // ya viene en el mod
                if (m_excluded.contains(key(candidate))) continue;
                const AssetInfo info = m_rootInfos.value(key(candidate));
                if (info.fitMeshType.isEmpty()
                    || info.fitMeshType.compare(QLatin1String("Body"), Qt::CaseInsensitive) == 0)
                    continue;
                extraRoots.insert(key(candidate), info);
            }
        }
    }
    QMap<QString, QString> extraRootTargets;  // key(root) -> ruta relocalizada
    if (!extraRoots.isEmpty()) {
        const QString vanillaDir = QDir(temp.path()).filePath(QStringLiteral("vanilla"));
        for (auto it = extraRoots.begin(); it != extraRoots.end();) {
            const QString rootAsset = it.value().path;
            emit progress(QStringLiteral("Extrayendo malla vanilla %1…")
                              .arg(rootAsset.section(QLatin1Char('/'), -1)));
            QString localError;
            const QString outDir = QDir(vanillaDir).filePath(shortId(rootAsset));
            if (m_pak->toLegacyFiltered(GamePaths::paksDir(),
                                        rootAsset.section(QLatin1Char('/'), -1),
                                        outDir, &localError) <= 0) {
                result.warnings << QStringLiteral("No se pudo extraer la malla vanilla %1: %2")
                                       .arg(rootAsset, localError);
                it = extraRoots.erase(it);
                continue;
            }
            // El filtro de retoc es por nombre: puede traer homónimos de otras
            // carpetas. Nos quedamos con el que tiene la ruta exacta.
            QString file;
            QDirIterator vi(outDir, {QStringLiteral("*.uasset")}, QDir::Files,
                            QDirIterator::Subdirectories);
            while (vi.hasNext()) {
                const QString candidateFile = vi.next();
                if (key(assetPathForFile(candidateFile, outDir)) == key(rootAsset)) {
                    file = candidateFile;
                    break;
                }
            }
            if (file.isEmpty()) { it = extraRoots.erase(it); continue; }
            const QString target = QStringLiteral("/Game/CNSRepacked/%1/%2")
                                       .arg(optionId, rootAsset.mid(6));
            relocations.insert(key(rootAsset), target);
            extraRootTargets.insert(key(rootAsset), target);
            assetFiles.insert(key(rootAsset), file);
            ++it;
        }
    }

    int index = 0;
    for (auto it = assetFiles.begin(); it != assetFiles.end(); ++it) {
        const QString sourcePath = it.key();
        const QString targetPath = relocations.value(it.key());
        const QString targetUasset = fileForAsset(targetPath, packageDir);
        emit progress(QStringLiteral("Reubicando assets… %1/%2").arg(++index).arg(assetFiles.size()));
        if (!writeRelocatedAsset(it.value(), targetUasset, relocations, &error)) {
            result.error = QStringLiteral("%1\nAsset: %2").arg(error, sourcePath);
            return result;
        }
        const QFileInfo sourceFi(it.value());
        QDir sourceFolder = sourceFi.dir();
        const QString base = sourceFi.completeBaseName();
        // El .uexp lo REESCRIBE UAssetGUI fromjson junto al .uasset: contiene los
        // exports serializados, con los índices FName y las referencias de package
        // ya reubicadas. Pisarlo con el .uexp de origen deja el header nuevo y los
        // exports viejos, y el juego crashea al equipar el outfit. Sólo se copia
        // como fallback si fromjson no lo emitió (asset sin datos separados).
        // El .ubulk es data cruda (mips de textura) y sí se copia siempre.
        for (const QString &ext : {QStringLiteral("ubulk"), QStringLiteral("uexp")}) {
            const QString companion = sourceFolder.filePath(base + QLatin1Char('.') + ext);
            if (!QFileInfo::exists(companion)) continue;
            const QString target = QFileInfo(targetUasset).dir().filePath(
                QFileInfo(targetUasset).completeBaseName() + QLatin1Char('.') + ext);
            if (ext == QLatin1String("uexp") && QFileInfo::exists(target)) continue;
            if (!copyFileReplacing(companion, target, &error)) {
                result.error = error; return result;
            }
        }
        ++result.assetsWritten;
    }

    QDir().mkpath(request.outputDir);
    const QString safeName = name;
    const QString outFolder = QDir(request.outputDir).filePath(safeName);
    QDir(outFolder).removeRecursively();
    QDir().mkpath(outFolder);
    const QString utoc = QDir(outFolder).filePath(safeName + QStringLiteral("_P.utoc"));
    emit progress(QStringLiteral("Empaquetando IoStore UE4.26…"));
    if (!m_pak->packZen(packageDir, utoc, &error)) {
        result.error = error; return result;
    }

    if (request.mode == Mode::ToCns) {
        const QString relocatedRoot = relocations.value(key(rootPath));
        // Slots reales de la malla + redirección de las texturas vanilla que el
        // mod pisaba: sin esto el outfit se ve con la piel vanilla (ver
        // textureParameterOverrides).
        emit progress(QStringLiteral("Leyendo materiales del outfit…"));
        const QString mount = stageWithGame(utoc, temp.path());
        QSet<QString> covered;
        // Mismo tratamiento para el outfit y para cada malla vanilla que hayamos
        // traído: leer sus slots y redirigir las texturas que el mod pisaba.
        auto meshData = [&](const QString &meshPath) {
            const QStringList slotList = meshMaterialSlots(mount, meshPath, temp.path());
            const QJsonArray parameters =
                textureParameterOverrides(mount, slotList, relocations, temp.path(), &covered);
            QJsonArray materials;
            // "Materials" sólo tiene sentido junto a los overrides: es la lista
            // que indexa MaterialIndex.
            if (!parameters.isEmpty())
                for (const QString &slot : slotList) materials.append(slot);
            return QJsonObject{
                {QStringLiteral("Mesh"), meshPath + QLatin1Char('.')
                                         + meshPath.section(QLatin1Char('/'), -1)},
                {QStringLiteral("Materials"), materials},
                {QStringLiteral("Parameters"), parameters}
            };
        };
        QJsonObject outfitData = meshData(relocatedRoot);

        // Cosméticos extra (cara, pelo…): la malla vanilla ya viaja en el paquete
        // con sus referencias reescritas hacia los assets del mod.
        QJsonArray extraDescriptors;
        for (auto it = extraRootTargets.begin(); it != extraRootTargets.end(); ++it) {
            const AssetInfo info = extraRoots.value(it.key());
            const QString label = info.name.isEmpty() ? info.fitMeshType : info.name;
            covered.insert(it.key());
            extraDescriptors.append(QJsonObject{
                {QStringLiteral("UniqueFitID"),
                 QStringLiteral("Repacked %1 %2").arg(name, label)},
                {QStringLiteral("Requirement"), QStringLiteral("None")},
                {QStringLiteral("DisplayName"), QStringLiteral("%1 (%2)").arg(name, label)},
                {QStringLiteral("Description"), QStringLiteral("%1 (%2)").arg(name, label)},
                {QStringLiteral("OutfitTypes"), QJsonArray()},
                {QStringLiteral("OutfitDatas"), QJsonArray{meshData(it.value())}},
                {QStringLiteral("CharacterID"),
                 info.characterId.isEmpty() ? QStringLiteral("EVE") : info.characterId},
                {QStringLiteral("FitMeshType"), info.fitMeshType}
            });
        }
        if (!mount.isEmpty()) QDir(mount).removeRecursively();

        // Lo que el mod pisaba fuera del outfit y no quedó cubierto ni por un
        // override de textura ni por una malla extra no se aplica en CNS.
        QStringList orphans;
        for (const QString &asset : moddedAssets) {
            if (asset.startsWith(rootFolder, Qt::CaseInsensitive)) continue;
            if (covered.contains(key(asset))) continue;
            bool reachable = false;
            for (const QString &candidate : m_assetToRoots.value(key(asset)))
                if (extraRootTargets.contains(key(candidate))) reachable = true;
            if (!reachable) orphans << asset;
        }
        orphans.removeDuplicates();
        if (!orphans.isEmpty())
            result.warnings << QStringLiteral(
                "Este mod también reemplaza assets fuera del outfit que CNS no puede "
                "aplicar al equipar:\n  %1").arg(orphans.join(QStringLiteral("\n  ")));
        if (!extraDescriptors.isEmpty())
            result.warnings << QStringLiteral(
                "Se agregaron %1 cosmético(s) CNS extra (cara/pelo) con los cambios "
                "del mod: equipalos por separado en el menú de CNS.")
                                   .arg(extraDescriptors.size());
        if (!sourceInfo.ponyPhysics.isEmpty()) {
            const QString pony = relocations.value(key(sourceInfo.ponyPhysics),
                                                   sourceInfo.ponyPhysics);
            outfitData.insert(QStringLiteral("PonyPhysics"),
                              pony + QLatin1Char('.') + pony.section(QLatin1Char('/'), -1));
        }
        QJsonObject descriptor{
            {QStringLiteral("UniqueFitID"), QStringLiteral("Repacked ") + name},
            {QStringLiteral("Requirement"),
             request.checkSaveData && !sourceInfo.id.isEmpty()
                 ? QStringLiteral("CheckSaveData-") + sourceInfo.id
                 : (sourceInfo.requirementDLC.isEmpty() ? QStringLiteral("None")
                                                       : sourceInfo.requirementDLC)},
            {QStringLiteral("DisplayName"), name},
            {QStringLiteral("Description"), name},
            {QStringLiteral("OutfitTypes"), QJsonArray()},
            {QStringLiteral("OutfitDatas"), QJsonArray{outfitData}},
            {QStringLiteral("CharacterID"),
             sourceInfo.characterId.isEmpty() ? QStringLiteral("EVE") : sourceInfo.characterId},
            {QStringLiteral("FitMeshType"),
             sourceInfo.fitMeshType.isEmpty() ? QStringLiteral("Body") : sourceInfo.fitMeshType}
        };
        if (!sourceInfo.meshSubType.isEmpty())
            descriptor.insert(QStringLiteral("MeshSubType"), sourceInfo.meshSubType);
        if (!sourceInfo.iconObjectPath.isEmpty())
            descriptor.insert(QStringLiteral("OutfitImage"), sourceInfo.iconObjectPath);
        QJsonArray entries{descriptor};
        for (const auto &extra : extraDescriptors) entries.append(extra);
        const QString descriptorPath = QDir(outFolder).filePath(name + QStringLiteral(".dekcns.json"));
        QSaveFile file(descriptorPath);
        if (!file.open(QIODevice::WriteOnly)
            || file.write(QJsonDocument(entries).toJson(QJsonDocument::Indented)) < 0
            || !file.commit()) {
            result.error = QStringLiteral("No se pudo escribir el descriptor CNS.");
            return result;
        }
        result.descriptorPath = descriptorPath;
    }
    const QString zipPath = QDir(request.outputDir).filePath(safeName + QStringLiteral(".zip"));
    emit progress(QStringLiteral("Creando ZIP instalable para Vortex…"));
    if (!m_pak->createZip(outFolder, zipPath, &error)) {
        result.error = error;
        return result;
    }
    if (!QDir(outFolder).removeRecursively()) {
        result.error = QStringLiteral(
            "El ZIP se creó, pero no se pudo borrar la carpeta intermedia: %1")
                           .arg(outFolder);
        return result;
    }
    result.ok = true;
    result.outputDir.clear();
    result.utocPath = utoc;
    result.zipPath = zipPath;
    return result;
}

} // namespace st
