#include "CnsIdFixerService.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QtEndian>

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
}

struct CnsIdFixerService::Container {
    QString path;
    quint8 version = 0;
    quint64 containerId = 0;
    quint32 entryCount = 0;
    QList<quint64> packageIds;
    QList<qint64> containerChunkOffsets;
};

CnsIdFixerService::CnsIdFixerService(QObject *parent) : QObject(parent) {}

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
                                                  bool fixDuplicates) const {
    Result result;
    const QFileInfo root(directory);
    if (!root.isDir()) {
        result.error = QStringLiteral("Elegí una carpeta válida.");
        return result;
    }

    QList<Container> containers;
    QStringList warnings;
    QDirIterator it(directory, {QStringLiteral("*.utoc")}, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        if (path.endsWith(QLatin1String(".cnsidfixer.bak"), Qt::CaseInsensitive))
            continue;
        Container container;
        QString error;
        if (readContainer(path, &container, &error)) containers << container;
        else warnings << error;
    }
    result.containers = containers.size();
    if (containers.isEmpty()) {
        result.error = warnings.isEmpty()
            ? QStringLiteral("No se encontraron archivos .utoc en la carpeta.")
            : warnings.join(QLatin1Char('\n'));
        return result;
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
                    .arg(QFileInfo(container.path).fileName(),
                         pos == 0 ? QStringLiteral(" (se conserva)") : QString());
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
                    .arg(QFileInfo(container.path).fileName(),
                         idText(container.containerId), idText(newId));
            } else {
                warnings << error;
                lines << QStringLiteral("  %1: NO MODIFICADO — %2")
                    .arg(QFileInfo(container.path).fileName(), error);
            }
        }
    }
    if (!foundContainerConflict)
        lines << QStringLiteral("Sin IDs duplicados.");

    lines << QString() << QStringLiteral("Package_Id");
    for (auto group = byPackageId.cbegin(); group != byPackageId.cend(); ++group) {
        if (group.value().size() < 2) continue;
        ++result.packageConflicts;
        QStringList names;
        for (int index : group.value())
            names << QFileInfo(containers[index].path).fileName();
        names.sort(Qt::CaseInsensitive);
        lines << QStringLiteral("%1: %2").arg(idText(group.key()), names.join(QStringLiteral(" ↔ ")));
    }
    if (result.packageConflicts == 0)
        lines << QStringLiteral("Sin recursos compartidos.");
    else
        lines << QStringLiteral("No se modifican: esos IDs dependen de la ruta del recurso.");

    if (!warnings.isEmpty())
        lines << QString() << QStringLiteral("Advertencias:") << warnings;
    result.ok = true;
    result.report = lines.join(QLatin1Char('\n'));
    return result;
}

} // namespace st
