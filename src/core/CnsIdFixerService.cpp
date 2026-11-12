#include "CnsIdFixerService.h"

#include "PakService.h"
#include "GamePaths.h"
#include "Translator.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDataStream>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtEndian>
#include <algorithm>

namespace st {

namespace {
constexpr qint64 HeaderSize = 0x90;
constexpr qint64 ContainerIdOffset = 52;
constexpr qint64 ChunkIdsOffset = HeaderSize;
constexpr int ChunkIdSize = 12;
const QByteArray TocMagic("-==--==--==--==-", 16);

quint32 readU32(const QByteArray &bytes, int offset) {
    const auto *p = reinterpret_cast<const uchar *>(bytes.constData() + offset);
    return qFromLittleEndian<quint32>(p);
}

quint64 readU64(const QByteArray &bytes, int offset) {
    const auto *p = reinterpret_cast<const uchar *>(bytes.constData() + offset);
    return qFromLittleEndian<quint64>(p);
}

QByteArray littleEndianId(quint64 id) {
    QByteArray bytes(8, Qt::Uninitialized);
    qToLittleEndian<quint64>(id, reinterpret_cast<uchar *>(bytes.data()));
    return bytes;
}

struct CnsOutfitIdentity {
    QString displayName;
    QString uniqueFitId;
    QStringList variants;
};

QList<CnsOutfitIdentity> readCnsOutfitIdentities(const QString &containerPath) {
    const QFileInfo containerInfo(containerPath);
    QStringList acceptedStems{containerInfo.completeBaseName()};
    if (acceptedStems.constFirst().endsWith(QLatin1String("_P"), Qt::CaseInsensitive))
        acceptedStems << acceptedStems.constFirst().left(acceptedStems.constFirst().size() - 2);

    QList<CnsOutfitIdentity> identities;
    const QDir directory(containerInfo.absolutePath());
    const QFileInfoList descriptors = directory.entryInfoList(
        {QStringLiteral("*.dekcns.json")}, QDir::Files, QDir::Name);
    for (const QFileInfo &descriptor : descriptors) {
        QString descriptorStem = descriptor.fileName();
        const QString descriptorSuffix = QStringLiteral(".dekcns.json");
        if (!descriptorStem.endsWith(descriptorSuffix, Qt::CaseInsensitive)) continue;
        descriptorStem.chop(descriptorSuffix.size());
        const bool matchesContainer = std::any_of(
            acceptedStems.cbegin(), acceptedStems.cend(),
            [&descriptorStem](const QString &stem) {
                return stem.compare(descriptorStem, Qt::CaseInsensitive) == 0;
            });
        if (!matchesContainer) continue;

        QFile file(descriptor.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly)) continue;
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
        QJsonArray entries;
        if (document.isArray()) {
            entries = document.array();
        } else if (document.isObject()) {
            const QJsonObject object = document.object();
            if (object.contains(QStringLiteral("DisplayName")))
                entries.append(object);
            else if (object.value(QStringLiteral("Outfits")).isArray())
                entries = object.value(QStringLiteral("Outfits")).toArray();
        }

        for (const QJsonValue &value : entries) {
            if (!value.isObject()) continue;
            const QJsonObject object = value.toObject();
            CnsOutfitIdentity identity;
            identity.displayName = object.value(QStringLiteral("DisplayName")).toString().trimmed();
            identity.uniqueFitId = object.value(QStringLiteral("UniqueFitID")).toString().trimmed();
            const QJsonArray names = object.value(QStringLiteral("OutfitNames")).toArray();
            for (const QJsonValue &name : names) {
                const QString variant = name.toString().trimmed();
                if (!variant.isEmpty() && !identity.variants.contains(variant, Qt::CaseInsensitive))
                    identity.variants << variant;
            }
            if (!identity.displayName.isEmpty() || !identity.uniqueFitId.isEmpty())
                identities << identity;
        }
    }
    return identities;
}
}

struct CnsIdFixerService::Container {
    QString path;
    quint8 version = 0;
    quint64 containerId = 0;
    quint32 entryCount = 0;
    QList<quint64> packageIds;
    QList<qint64> containerChunkOffsets;
    QList<CnsOutfitIdentity> outfitIdentities;
};

CnsIdFixerService::CnsIdFixerService(PakService *pak, QObject *parent)
    : QObject(parent), m_pak(pak) {}

QString CnsIdFixerService::idText(quint64 id) {
    return QStringLiteral("0x%1").arg(id, 16, 16, QLatin1Char('0')).toUpper();
}

bool CnsIdFixerService::readContainer(const QString &path, Container *out, QString *error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("No se pudo abrir %1: %2").arg(path, file.errorString());
        return false;
    }
    const QByteArray header = file.read(HeaderSize);
    if (header.size() != HeaderSize || header.left(16) != TocMagic) {
        if (error) *error = QStringLiteral("%1 no es un .utoc válido.").arg(path);
        return false;
    }
    if (readU32(header, 20) != HeaderSize) {
        if (error) *error = QStringLiteral("%1 usa un encabezado IoStore no compatible.").arg(path);
        return false;
    }

    Container parsed;
    parsed.path = path;
    parsed.version = quint8(header.at(16));
    parsed.entryCount = readU32(header, 24);
    parsed.containerId = readU64(header, ContainerIdOffset);
    if (parsed.entryCount > 10000000U
        || file.size() < ChunkIdsOffset + qint64(parsed.entryCount) * ChunkIdSize) {
        if (error) *error = QStringLiteral("%1 tiene una tabla de chunks inválida.").arg(path);
        return false;
    }

    const QByteArray chunks = file.read(qint64(parsed.entryCount) * ChunkIdSize);
    QSet<quint64> packages;
    for (quint32 i = 0; i < parsed.entryCount; ++i) {
        const int offset = int(i * ChunkIdSize);
        const quint64 id = readU64(chunks, offset);
        const quint8 rawType = quint8(chunks.at(offset + 11));
        // UE4 (TOC <= PerfectHash): ExportBundleData=2, ContainerHeader=10.
        // Formatos nuevos: ExportBundleData=1, ContainerHeader=6.
        const bool newerTypes = parsed.version > 4;
        const quint8 exportType = newerTypes ? 1 : 2;
        const quint8 headerType = newerTypes ? 6 : 10;
        if (rawType == exportType) packages.insert(id);
        if (rawType == headerType && id == parsed.containerId)
            parsed.containerChunkOffsets << ChunkIdsOffset + qint64(offset);
    }
    parsed.packageIds = packages.values();
    *out = parsed;
    return true;
}

bool CnsIdFixerService::patchContainer(const Container &container, quint64 newId,
                                       QString *error) {
    // PerfectHash y posteriores indexan los chunk IDs; editarlos exigiría
    // reconstruir seeds/overflow. Stellar Blade UE4.26 usa DirectoryIndex.
    if (container.version >= 4) {
        if (error) *error = QStringLiteral(
            "%1 usa perfect-hash IoStore; no se modificó por seguridad.")
            .arg(QFileInfo(container.path).fileName());
        return false;
    }
    if (container.containerChunkOffsets.isEmpty()) {
        if (error) *error = QStringLiteral(
            "%1 no contiene el chunk de cabecera esperado.")
            .arg(QFileInfo(container.path).fileName());
        return false;
    }

    const QString backup = container.path + QStringLiteral(".cnsidfixer.bak");
    if (!QFileInfo::exists(backup) && !QFile::copy(container.path, backup)) {
        if (error) *error = QStringLiteral("No se pudo crear el backup %1.").arg(backup);
        return false;
    }

    QFile file(container.path);
    if (!file.open(QIODevice::ReadWrite)) {
        if (error) *error = QStringLiteral("No se pudo escribir %1: %2")
                                .arg(container.path, file.errorString());
        return false;
    }
    const QByteArray idBytes = littleEndianId(newId);
    auto writeAt = [&](qint64 offset) {
        return file.seek(offset) && file.write(idBytes) == idBytes.size();
    };
    if (!writeAt(ContainerIdOffset)) {
        if (error) *error = QStringLiteral("Falló la escritura del Container_Id.");
        return false;
    }
    for (qint64 offset : container.containerChunkOffsets) {
        if (!writeAt(offset)) {
            if (error) *error = QStringLiteral("Falló la escritura del chunk de cabecera.");
            return false;
        }
    }
    file.flush();
    file.close();

    Container verified;
    QString verifyError;
    if (!readContainer(container.path, &verified, &verifyError)
        || verified.containerId != newId
        || verified.containerChunkOffsets.isEmpty()) {
        QFile::remove(container.path);
        QFile::copy(backup, container.path);
        if (error) *error = QStringLiteral("La verificación falló; se restauró el backup: %1")
                                .arg(verifyError);
        return false;
    }
    return true;
}

CnsIdFixerService::Result CnsIdFixerService::run(const QString &directory,
                                                  bool fixDuplicates,
                                                  bool verifyContents,
                                                  const QString &vanillaPaksDirectory) const {
    Result result;
    const QFileInfo root(directory);
    if (!root.isDir()) {
        result.error = QStringLiteral("Elegí una carpeta válida.");
        return result;
    }

    QList<Container> containers;
    QStringList warnings;
    QSet<QString> integrityFailurePaths;
    QDirIterator it(directory, {QStringLiteral("*.utoc")}, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        if (path.endsWith(QLatin1String(".cnsidfixer.bak"), Qt::CaseInsensitive))
            continue;
        const QString relativePath = QDir::fromNativeSeparators(
            QDir(directory).relativeFilePath(path));
        const QStringList pathParts = relativePath.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        if (std::any_of(pathParts.cbegin(), pathParts.cend(), [](const QString &part) {
                return part.compare(QLatin1String("disabled"), Qt::CaseInsensitive) == 0;
            }))
            continue;
        Container container;
        QString error;
        if (!readContainer(path, &container, &error)) {
            warnings << error;
            if (verifyContents) {
                result.integrityIssues << IntegrityIssue{path, error, false};
                integrityFailurePaths.insert(QDir::cleanPath(
                    QFileInfo(path).absoluteFilePath()).toCaseFolded());
            }
            continue;
        }
        container.outfitIdentities = readCnsOutfitIdentities(path);
        if (verifyContents && m_pak) {
            QString verifyError;
            if (!m_pak->verifyIoStoreContainer(path, &verifyError)) {
                IntegrityIssue issue;
                issue.path = path;
                issue.detail = verifyError;
                issue.repairableHeaderHash = !container.containerChunkOffsets.isEmpty()
                    && verifyError.contains(QStringLiteral("hash mismatch for chunk #0"),
                                            Qt::CaseInsensitive);
                integrityFailurePaths.insert(QDir::cleanPath(
                    QFileInfo(path).absoluteFilePath()).toCaseFolded());
                if (issue.repairableHeaderHash)
                    result.repairableHeaderHashPaths << path;
                result.integrityIssues << issue;
            }
        }
        containers << container;
    }
    result.containers = containers.size();
    if (containers.isEmpty()) {
        if (warnings.isEmpty()) {
            result.ok = true;
            result.report = tr_(QStringLiteral("cns_id_empty"));
        } else if (verifyContents && !result.integrityIssues.isEmpty()) {
            result.ok = true;
            result.report = warnings.join(QLatin1Char('\n'));
        } else {
            result.error = warnings.join(QLatin1Char('\n'));
        }
        return result;
    }

    // Package_Id is the hash of an asset package path. Comparing only the
    // installed mods misses the common case of one mod replacing a vanilla
    // texture/material used by another CNS outfit. Build the original-game ID
    // set from Paks itself, excluding the active mod/install staging folders.
    QSet<quint64> vanillaPackageIds;
    QList<Container> vanillaContainerFiles;
    int vanillaContainers = 0;
    const QString vanillaRoot = vanillaPaksDirectory.isEmpty()
        ? GamePaths::paksDir() : vanillaPaksDirectory;
    const QFileInfo vanillaRootInfo(vanillaRoot);
    if ((m_pak || !vanillaPaksDirectory.isEmpty()) && vanillaRootInfo.isDir()) {
        const QString modsRootAbs = QDir::cleanPath(root.canonicalFilePath());
        QDirIterator vanillaIt(vanillaRoot, {QStringLiteral("*.utoc")}, QDir::Files,
                               QDirIterator::Subdirectories);
        while (vanillaIt.hasNext()) {
            const QString path = vanillaIt.next();
            const QString relative = QDir::fromNativeSeparators(
                QDir(vanillaRoot).relativeFilePath(path));
            const QStringList parts = relative.split(QLatin1Char('/'), Qt::SkipEmptyParts);
            const bool stagedOrMod = std::any_of(parts.cbegin(), parts.cend(),
                [](const QString &part) {
                    const QString folded = part.toCaseFolded();
                    return folded == QLatin1String("~mods")
                        || folded == QLatin1String("logicmods")
                        || folded == QLatin1String("disabled")
                        || folded == QLatin1String("backup")
                        || folded == QLatin1String("backups")
                        || folded.startsWith(QLatin1String("backup_"))
                        || folded.endsWith(QLatin1String("_backup"))
                        || folded.startsWith(QLatin1String(".stellartool_"))
                        || folded.startsWith(QLatin1String("~st_builder_"));
                });
            if (stagedOrMod) continue;

            const QString absolute = QDir::cleanPath(QFileInfo(path).canonicalFilePath());
            const QString modsPrefix = modsRootAbs + QLatin1Char('/');
            if (!modsRootAbs.isEmpty()
                && (absolute.compare(modsRootAbs, Qt::CaseInsensitive) == 0
                    || absolute.startsWith(modsPrefix, Qt::CaseInsensitive)))
                continue;

            Container vanilla;
            QString readError;
            if (!readContainer(path, &vanilla, &readError)) {
                warnings << tr_(QStringLiteral("cns_id_vanilla_read_failed"))
                                .arg(relative, readError);
                continue;
            }
            ++vanillaContainers;
            for (quint64 packageId : vanilla.packageIds)
                vanillaPackageIds.insert(packageId);
            vanillaContainerFiles << vanilla;
        }
    }

    QHash<quint64, QList<int>> byContainerId;
    QHash<quint64, QSet<int>> byPackageId;
    QSet<quint64> usedIds;
    for (int i = 0; i < containers.size(); ++i) {
        byContainerId[containers[i].containerId] << i;
        usedIds.insert(containers[i].containerId);
        for (quint64 packageId : containers[i].packageIds)
            byPackageId[packageId].insert(i);
    }

    QStringList lines;
    const QDir rootDir(directory);
    const auto displayPath = [&rootDir](const Container &container) {
        return QDir::fromNativeSeparators(rootDir.relativeFilePath(container.path));
    };
    lines << QStringLiteral("CNS ID Fixer — %1 contenedores escaneados")
                 .arg(containers.size());
    lines << QString();
    lines << QStringLiteral("Container_Id");
    bool foundContainerConflict = false;
    for (auto group = byContainerId.cbegin(); group != byContainerId.cend(); ++group) {
        if (group.value().size() < 2) continue;
        foundContainerConflict = true;
        result.duplicateContainers += group.value().size() - 1;
        lines << QStringLiteral("Duplicado %1:").arg(idText(group.key()));
        for (int pos = 0; pos < group.value().size(); ++pos) {
            const int index = group.value().at(pos);
            const Container &container = containers[index];
            if (!fixDuplicates || pos == 0) {
                lines << QStringLiteral("  %1%2")
                    .arg(displayPath(container),
                         pos == 0 ? QStringLiteral(" (se conserva)") : QString());
                continue;
            }
            if (integrityFailurePaths.contains(QDir::cleanPath(
                    QFileInfo(container.path).absoluteFilePath()).toCaseFolded())) {
                const QString reason = tr_(QStringLiteral("cns_id_skip_integrity_failure"));
                warnings << QStringLiteral("%1: %2").arg(displayPath(container), reason);
                lines << QStringLiteral("  %1: %2").arg(displayPath(container), reason);
                result.unfixedDuplicateContainerPaths << container.path;
                continue;
            }
            QByteArray seed = QFileInfo(container.path).canonicalFilePath().toUtf8();
            seed += QByteArray::number(group.key());
            quint64 newId = 0;
            quint32 salt = 0;
            do {
                QByteArray salted = seed + QByteArray::number(salt++);
                const QByteArray hash = QCryptographicHash::hash(
                    salted, QCryptographicHash::Sha256);
                newId = qFromLittleEndian<quint64>(
                    reinterpret_cast<const uchar *>(hash.constData()));
            } while (newId == 0 || usedIds.contains(newId));
            QString error;
            if (patchContainer(container, newId, &error)) {
                usedIds.insert(newId);
                ++result.fixedContainers;
                lines << QStringLiteral("  %1: %2 -> %3")
                    .arg(displayPath(container),
                         idText(container.containerId), idText(newId));
            } else {
                warnings << error;
                lines << QStringLiteral("  %1: NO MODIFICADO — %2")
                    .arg(displayPath(container), error);
                result.unfixedDuplicateContainerPaths << container.path;
            }
        }
    }
    if (!foundContainerConflict)
        lines << QStringLiteral("Sin IDs duplicados.");

    lines << QString() << QStringLiteral("Package_Id")
          << tr_(QStringLiteral("cns_id_shared_section"));
    QSet<int> sharedPackageContainers;
    QList<quint64> sharedPackageIds;
    for (auto group = byPackageId.cbegin(); group != byPackageId.cend(); ++group) {
        if (group.value().size() < 2) continue;
        sharedPackageIds << group.key();
        for (int index : group.value()) sharedPackageContainers.insert(index);
    }
    std::sort(sharedPackageIds.begin(), sharedPackageIds.end());

    QHash<int, QMap<quint64, QString>> modPackagePaths;
    QList<int> sharedContainerIndices = sharedPackageContainers.values();
    std::sort(sharedContainerIndices.begin(), sharedContainerIndices.end(),
              [&containers, &displayPath](int lhs, int rhs) {
                  return displayPath(containers.at(lhs)).compare(
                      displayPath(containers.at(rhs)), Qt::CaseInsensitive) < 0;
              });
    if (m_pak) {
        for (int index : sharedContainerIndices) {
            QMap<quint64, QString> paths;
            QString pathError;
            if (!m_pak->listIoStorePackagePaths(containers.at(index).path, directory,
                                                &paths, &pathError)) {
                warnings << tr_(QStringLiteral("cns_id_shared_list_failed"))
                                .arg(displayPath(containers.at(index)), pathError);
                continue;
            }
            modPackagePaths.insert(index, paths);
        }
    }

    const auto sourceLines = [&containers, &displayPath](int index) {
        QStringList resultLines;
        const Container &container = containers.at(index);
        resultLines << QStringLiteral("  ")
                           + tr_(QStringLiteral("cns_id_shared_source"))
                                 .arg(displayPath(container));
        if (container.outfitIdentities.isEmpty()) {
            resultLines << QStringLiteral("    ")
                           + tr_(QStringLiteral("cns_id_shared_no_cns_descriptor"));
            return resultLines;
        }
        for (const CnsOutfitIdentity &identity : container.outfitIdentities) {
            resultLines << QStringLiteral("    ")
                           + tr_(QStringLiteral("cns_id_shared_fit_detail"))
                                 .arg(identity.displayName.isEmpty()
                                          ? tr_(QStringLiteral("cns_id_shared_name_unknown"))
                                          : identity.displayName,
                                      identity.variants.isEmpty()
                                          ? tr_(QStringLiteral("cns_id_shared_variants_unknown"))
                                          : identity.variants.join(QStringLiteral(", ")),
                                      identity.uniqueFitId.isEmpty()
                                          ? tr_(QStringLiteral("cns_id_shared_fit_id_unknown"))
                                          : identity.uniqueFitId);
        }
        return resultLines;
    };

    for (quint64 packageId : sharedPackageIds) {
        ++result.packageConflicts;
        const QSet<int> owners = byPackageId.value(packageId);
        result.sharedPackageReferences += owners.size() - 1;
        QStringList assetPaths;
        QList<int> sortedOwners = owners.values();
        std::sort(sortedOwners.begin(), sortedOwners.end(),
                  [&containers, &displayPath](int lhs, int rhs) {
                      return displayPath(containers.at(lhs)).compare(
                          displayPath(containers.at(rhs)), Qt::CaseInsensitive) < 0;
                  });
        for (int index : sortedOwners) {
            const QString assetPath = modPackagePaths.value(index).value(packageId);
            if (!assetPath.isEmpty() && !assetPaths.contains(assetPath, Qt::CaseInsensitive))
                assetPaths << assetPath;
        }
        assetPaths.sort(Qt::CaseInsensitive);
        lines << tr_(QStringLiteral("cns_id_shared_conflict_line"))
                     .arg(idText(packageId), assetPaths.isEmpty()
                              ? tr_(QStringLiteral("cns_id_shared_path_unknown"))
                              : assetPaths.join(QStringLiteral(" ↔ ")));
        for (int index : sortedOwners) lines << sourceLines(index);
    }
    for (int index : sharedPackageContainers)
        result.sharedPackageContainerPaths << containers.at(index).path;
    result.sharedPackageContainerPaths.sort(Qt::CaseInsensitive);
    if (result.packageConflicts == 0)
        lines << tr_(QStringLiteral("cns_id_shared_none"));
    else
        lines << tr_(QStringLiteral("cns_id_shared_note"));

    lines << QString() << tr_(QStringLiteral("cns_id_vanilla_section"));
    QHash<quint64, QSet<int>> vanillaOverrides;
    for (auto group = byPackageId.cbegin(); group != byPackageId.cend(); ++group) {
        if (!vanillaPackageIds.contains(group.key())) continue;
        result.vanillaPackageReferences += group.value().size();
        vanillaOverrides.insert(group.key(), group.value());
        for (int index : group.value()) sharedPackageContainers.insert(index);
    }

    QSet<int> vanillaOverrideContainers;
    for (auto group = vanillaOverrides.cbegin(); group != vanillaOverrides.cend(); ++group)
        vanillaOverrideContainers.unite(group.value());
    for (int index : vanillaOverrideContainers)
        result.vanillaPackageContainerPaths << containers.at(index).path;
    result.vanillaPackageContainerPaths.sort(Qt::CaseInsensitive);

    lines << tr_(QStringLiteral("cns_id_vanilla_scan_info")).arg(vanillaContainers);
    if (vanillaOverrides.isEmpty()) {
        lines << tr_(QStringLiteral("cns_id_vanilla_none"));
    } else {
        QMap<quint64, QString> pathByPackageId;
        if (m_pak) {
            for (const Container &vanilla : vanillaContainerFiles) {
                const bool containsConflict = std::any_of(
                    vanilla.packageIds.cbegin(), vanilla.packageIds.cend(),
                    [&vanillaOverrides](quint64 packageId) {
                        return vanillaOverrides.contains(packageId);
                    });
                if (!containsConflict) continue;

                QMap<quint64, QString> packagePaths;
                QString pathError;
                if (!m_pak->listIoStorePackagePaths(vanilla.path,
                                                    vanillaRoot,
                                                    &packagePaths, &pathError)) {
                    warnings << tr_(QStringLiteral("cns_id_vanilla_list_failed"))
                                    .arg(QDir::fromNativeSeparators(
                                             QDir(vanillaRoot).relativeFilePath(vanilla.path)),
                                         pathError);
                    continue;
                }
                for (auto it = packagePaths.cbegin(); it != packagePaths.cend(); ++it)
                    if (vanillaOverrides.contains(it.key()))
                        pathByPackageId.insert(it.key(), it.value());
            }
        }
        QList<quint64> packageIds = vanillaOverrides.keys();
        std::sort(packageIds.begin(), packageIds.end());
        for (quint64 packageId : packageIds) {
            QStringList names;
            for (int index : vanillaOverrides.value(packageId))
                names << displayPath(containers.at(index));
            names.sort(Qt::CaseInsensitive);
            const QString assetPath = pathByPackageId.value(packageId);
            lines << tr_(QStringLiteral("cns_id_vanilla_conflict_line"))
                         .arg(idText(packageId),
                              assetPath.isEmpty()
                                  ? tr_(QStringLiteral("cns_id_vanilla_path_unknown"))
                                  : assetPath,
                              names.join(QStringLiteral(" ↔ ")));
            QList<int> owners = vanillaOverrides.value(packageId).values();
            std::sort(owners.begin(), owners.end(),
                      [&containers, &displayPath](int lhs, int rhs) {
                          return displayPath(containers.at(lhs)).compare(
                              displayPath(containers.at(rhs)), Qt::CaseInsensitive) < 0;
                      });
            for (int index : owners) lines << sourceLines(index);
        }
        lines << tr_(QStringLiteral("cns_id_vanilla_conversion_note"));
    }
    result.vanillaPackageConflicts = vanillaOverrides.size();

    if (!warnings.isEmpty())
        lines << QString() << QStringLiteral("Advertencias:") << warnings;
    result.ok = true;
    result.report = lines.join(QLatin1Char('\n'));
    return result;
}

bool CnsIdFixerService::packageIdsMatch(const QString &firstUtoc,
                                        const QString &secondUtoc,
                                        QString *error) const {
    Container first;
    Container second;
    if (!readContainer(firstUtoc, &first, error)
        || !readContainer(secondUtoc, &second, error))
        return false;
    std::sort(first.packageIds.begin(), first.packageIds.end());
    std::sort(second.packageIds.begin(), second.packageIds.end());
    if (first.packageIds != second.packageIds) {
        if (error) *error = tr_(QStringLiteral("cns_id_repair_package_ids_changed"));
        return false;
    }
    return true;
}

CnsIdFixerService::RepairResult CnsIdFixerService::repairHeaderHashMismatches(
    const QString &directory, const QStringList &containerPaths) const {
    RepairResult result;
    if (!m_pak) {
        for (const QString &path : containerPaths)
            result.outcomes << HeaderHashRepair{
                path, false, tr_(QStringLiteral("cns_id_repair_no_pak"))};
        return result;
    }

    const QDir root(directory);
    QString backupRoot;
    for (const QString &path : containerPaths) {
        HeaderHashRepair outcome;
        outcome.path = path;

        QString currentIntegrity;
        if (m_pak->verifyIoStoreContainer(path, &currentIntegrity)
            || !currentIntegrity.contains(QStringLiteral("hash mismatch for chunk #0"),
                                          Qt::CaseInsensitive)) {
            outcome.detail = currentIntegrity.isEmpty()
                ? tr_(QStringLiteral("cns_id_repair_not_repairable"))
                : currentIntegrity;
            result.outcomes << outcome;
            continue;
        }

        Container original;
        QString error;
        if (!readContainer(path, &original, &error)
            || original.containerChunkOffsets.isEmpty()) {
            outcome.detail = error.isEmpty()
                ? tr_(QStringLiteral("cns_id_repair_missing_header")) : error;
            result.outcomes << outcome;
            continue;
        }

        QTemporaryDir temp(QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                           + QStringLiteral("/StellarToolHeaderHash-XXXXXX"));
        if (!temp.isValid()) {
            outcome.detail = QStringLiteral("No se pudo crear la carpeta temporal.");
            result.outcomes << outcome;
            continue;
        }
        const QString stagedDir = QDir(temp.path()).filePath(QStringLiteral("repacked"));
        QStringList stagedFiles;
        if (!m_pak->repackIoStoreContainer(path, stagedDir, &stagedFiles, &error)) {
            outcome.detail = error;
            result.outcomes << outcome;
            continue;
        }

        const QString stagedUtoc = QDir(stagedDir).filePath(QFileInfo(path).fileName());
        if (!packageIdsMatch(path, stagedUtoc, &error)) {
            outcome.detail = error;
            result.outcomes << outcome;
            continue;
        }
        if (!m_pak->verifyIoStoreContainer(stagedUtoc, &error)) {
            outcome.detail = error;
            result.outcomes << outcome;
            continue;
        }

        const QString relativeUtoc = QDir::fromNativeSeparators(root.relativeFilePath(path));
        if (relativeUtoc == QLatin1String("..")
            || relativeUtoc.startsWith(QLatin1String("../"))
            || QDir::isAbsolutePath(relativeUtoc)) {
            outcome.detail = tr_(QStringLiteral("cns_id_repair_outside_mod_dir"));
            result.outcomes << outcome;
            continue;
        }

        const QString runStamp = QDateTime::currentDateTimeUtc().toString(
            QStringLiteral("yyyyMMdd_HHmmss_zzz"));
        if (backupRoot.isEmpty()) {
            backupRoot = root.filePath(QStringLiteral("disabled/StellarTool_IoStoreRepair_")
                                       + runStamp);
            int suffix = 1;
            const QString base = backupRoot;
            while (QFileInfo::exists(backupRoot))
                backupRoot = base + QLatin1Char('_') + QString::number(suffix++);
        }

        const QFileInfo sourceInfo(path);
        const QString stem = sourceInfo.completeBaseName();
        QStringList originalFiles;
        for (const QString &ext : {QStringLiteral("utoc"), QStringLiteral("ucas"),
                                   QStringLiteral("pak")}) {
            const QString source = sourceInfo.dir().filePath(stem + QLatin1Char('.') + ext);
            if (QFileInfo::exists(source)) originalFiles << source;
        }
        if (!originalFiles.contains(path, Qt::CaseInsensitive)) {
            outcome.detail = tr_(QStringLiteral("cns_id_repair_missing_original"));
            result.outcomes << outcome;
            continue;
        }

        QList<QPair<QString, QString>> moves;
        QSet<QString> sourceSet;
        bool destinationError = false;
        for (const QString &source : originalFiles) {
            const QString relative = QDir::fromNativeSeparators(root.relativeFilePath(source));
            const QString backup = QDir(backupRoot).filePath(relative + QStringLiteral(".disabled"));
            if (relative == QLatin1String("..") || relative.startsWith(QLatin1String("../"))
                || QDir::isAbsolutePath(relative) || QFileInfo::exists(backup)) {
                destinationError = true;
                break;
            }
            sourceSet.insert(QDir::cleanPath(QFileInfo(source).absoluteFilePath()).toCaseFolded());
            moves.append(qMakePair(source, backup));
        }
        if (destinationError) {
            outcome.detail = tr_(QStringLiteral("cns_id_repair_backup_invalid"));
            result.outcomes << outcome;
            continue;
        }

        QStringList finalFiles;
        for (const QString &staged : stagedFiles) {
            const QString destination = sourceInfo.dir().filePath(QFileInfo(staged).fileName());
            const QString normalized = QDir::cleanPath(
                QFileInfo(destination).absoluteFilePath()).toCaseFolded();
            if (QFileInfo::exists(destination) && !sourceSet.contains(normalized)) {
                destinationError = true;
                break;
            }
            finalFiles << destination;
        }
        if (destinationError) {
            outcome.detail = tr_(QStringLiteral("cns_id_repair_output_conflict"));
            result.outcomes << outcome;
            continue;
        }

        if (!QDir().mkpath(backupRoot)) {
            outcome.detail = tr_(QStringLiteral("cns_id_repair_backup_create_failed"))
                                 .arg(backupRoot);
            result.outcomes << outcome;
            continue;
        }

        QList<QPair<QString, QString>> moved;
        QStringList installedFiles;
        auto restore = [&moved, &installedFiles, &backupRoot] {
            for (const QString &installed : installedFiles) QFile::remove(installed);
            bool restored = true;
            for (auto it = moved.crbegin(); it != moved.crend(); ++it) {
                QDir().mkpath(QFileInfo(it->first).absolutePath());
                if (QFileInfo::exists(it->second) && !QFile::rename(it->second, it->first))
                    restored = false;
            }
            if (restored && QDir(backupRoot).exists()
                && QDir(backupRoot).entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty())
                QDir().rmdir(backupRoot);
            return restored;
        };

        bool transactionOk = true;
        for (const auto &move : moves) {
            if (!QDir().mkpath(QFileInfo(move.second).absolutePath())
                || !QFile::rename(move.first, move.second)) {
                error = tr_(QStringLiteral("cns_id_repair_backup_move_failed"))
                            .arg(QFileInfo(move.first).fileName());
                transactionOk = false;
                break;
            }
            moved << move;
        }
        if (transactionOk) {
            for (int i = 0; i < stagedFiles.size(); ++i) {
                if (!QFile::copy(stagedFiles.at(i), finalFiles.at(i))) {
                    QFile::remove(finalFiles.at(i));
                    error = tr_(QStringLiteral("cns_id_repair_install_failed"))
                                .arg(QFileInfo(finalFiles.at(i)).fileName());
                    transactionOk = false;
                    break;
                }
                installedFiles << finalFiles.at(i);
            }
        }
        if (transactionOk
            && !m_pak->verifyIoStoreContainer(path, &error))
            transactionOk = false;
        if (!transactionOk) {
            const bool restored = restore();
            outcome.detail = error;
            if (!restored)
                outcome.detail += QLatin1Char(' ')
                    + tr_(QStringLiteral("cns_id_repair_rollback_incomplete"))
                          .arg(backupRoot);
            result.outcomes << outcome;
            continue;
        }

        outcome.repaired = true;
        outcome.detail = backupRoot;
        ++result.repairedContainers;
        result.outcomes << outcome;
    }
    if (result.repairedContainers > 0) result.backupDirectory = backupRoot;
    return result;
}

} // namespace st
