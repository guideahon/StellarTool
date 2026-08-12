#include "ReShadePresetService.h"

#include "GamePaths.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QUrl>
#include <QDesktopServices>
#include <QRegularExpression>
#include <QUuid>

namespace st {

namespace {

QString cleanName(const QString &value) {
    QString name = value.trimmed();
    name.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
    return name.left(80).trimmed();
}

QString valueFromGeneral(const QString &text, const QString &key) {
    bool inGeneral = false;
    for (const QString &raw : text.split('\n')) {
        const QString line = raw.trimmed();
        if (line.startsWith('[') && line.endsWith(']')) {
            inGeneral = line.mid(1, line.size() - 2).trimmed().compare(
                            QStringLiteral("GENERAL"), Qt::CaseInsensitive) == 0;
            continue;
        }
        if (!inGeneral) continue;
        const int equals = line.indexOf('=');
        if (equals <= 0) continue;
        if (line.left(equals).trimmed().compare(key, Qt::CaseInsensitive) == 0)
            return line.mid(equals + 1).trimmed();
    }
    return {};
}

QStringList pathValuesFromGeneral(const QString &text, const QString &key) {
    QStringList values;
    bool inGeneral = false;
    for (const QString &raw : text.split('\n')) {
        const QString line = raw.trimmed();
        if (line.startsWith('[') && line.endsWith(']')) {
            inGeneral = line.mid(1, line.size() - 2).trimmed().compare(
                            QStringLiteral("GENERAL"), Qt::CaseInsensitive) == 0;
            continue;
        }
        const int equals = line.indexOf('=');
        if (!inGeneral || equals <= 0) continue;
        if (line.left(equals).trimmed().compare(key, Qt::CaseInsensitive) == 0)
            values << line.mid(equals + 1).trimmed().split(';', Qt::SkipEmptyParts);
    }
    return values;
}

QString resolvePath(const QString &base, const QString &path) {
    if (path.isEmpty()) return {};
    const QString expanded = QDir::fromNativeSeparators(path);
    return QFileInfo(expanded).isAbsolute()
        ? QDir::cleanPath(expanded)
        : QDir::cleanPath(QDir(base).filePath(expanded));
}

} // namespace

ReShadePresetService::ReShadePresetService(QObject *parent) : QObject(parent) {
    refresh();
}

QString ReShadePresetService::storageDir() const {
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
           + QStringLiteral("/reshade/presets");
}

QString ReShadePresetService::metadataPath() const { return storageDir() + QStringLiteral("/index.json"); }
QString ReShadePresetService::managedDir() const { return m_reshadeDir + QStringLiteral("/StellarTool_ReShade"); }
QString ReShadePresetService::backupDir() const { return storageDir() + QStringLiteral("/../backups"); }

QJsonArray ReShadePresetService::loadMetadata() const {
    QFile file(metadataPath());
    if (!file.open(QIODevice::ReadOnly)) return {};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    return doc.isArray() ? doc.array() : QJsonArray();
}

bool ReShadePresetService::writeMetadata(const QJsonArray &items) const {
    QDir().mkpath(storageDir());
    const QString temporary = metadataPath() + QStringLiteral(".tmp");
    QFile file(temporary);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    file.write(QJsonDocument(items).toJson(QJsonDocument::Indented));
    file.close();
    QFile::remove(metadataPath());
    return QFile::rename(temporary, metadataPath());
}

QString ReShadePresetService::presetFileForName(const QString &name) const {
    for (const QJsonValue &value : loadMetadata()) {
        const QJsonObject item = value.toObject();
        if (item.value(QStringLiteral("name")).toString() == name)
            return storageDir() + QLatin1Char('/') + item.value(QStringLiteral("file")).toString();
    }
    return {};
}

QString ReShadePresetService::resolveActivePreset() const {
    QFile file(m_configPath);
    if (!file.open(QIODevice::ReadOnly)) return {};
    const QString text = QString::fromUtf8(file.readAll());
    QString path = valueFromGeneral(text, QStringLiteral("CurrentPresetPath"));
    if (path.isEmpty()) path = valueFromGeneral(text, QStringLiteral("PresetPath"));
    return resolvePath(m_reshadeDir, path);
}

QStringList ReShadePresetService::missingShaders(const QString &presetPath) const {
    QFile preset(presetPath);
    if (!preset.open(QIODevice::ReadOnly)) return {};
    const QString text = QString::fromUtf8(preset.readAll());
    QStringList techniques;
    for (const QString &key : {QStringLiteral("Techniques"), QStringLiteral("TechniqueSorting")}) {
        const QString value = valueFromGeneral(text, key);
        for (QString technique : value.split(',', Qt::SkipEmptyParts)) {
            technique = technique.trimmed();
            const int at = technique.indexOf('@');
            if (at >= 0) technique = technique.mid(at + 1);
            technique.remove(QRegularExpression(QStringLiteral("\\.fx$"), QRegularExpression::CaseInsensitiveOption));
            if (!technique.isEmpty() && !techniques.contains(technique, Qt::CaseInsensitive)) techniques << technique;
        }
    }
    if (techniques.isEmpty()) return {};

    QFile config(m_configPath);
    QString configText;
    if (config.open(QIODevice::ReadOnly)) configText = QString::fromUtf8(config.readAll());
    QStringList roots;
    for (const QString &path : pathValuesFromGeneral(configText, QStringLiteral("EffectSearchPaths")))
        roots << resolvePath(m_reshadeDir, path);
    if (roots.isEmpty()) roots << QDir(m_reshadeDir).filePath(QStringLiteral("Shaders"));

    QStringList files;
    for (const QString &root : roots) {
        if (!QFileInfo(root).isDir()) continue;
        QDirIterator it(root, {QStringLiteral("*.fx")}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) files << QFileInfo(it.next()).completeBaseName();
    }
    QStringList missing;
    for (const QString &technique : techniques) {
        bool found = false;
        for (const QString &file : files) {
            if (file.compare(technique, Qt::CaseInsensitive) == 0) { found = true; break; }
        }
        if (!found) missing << technique;
    }
    return missing;
}

void ReShadePresetService::rebuildState() {
    m_reshadeDir = GamePaths::gameRoot().isEmpty() ? QString() :
                   QDir::cleanPath(GamePaths::gameRoot() + QStringLiteral("/SB/Binaries/Win64"));
    m_configPath = m_reshadeDir + QStringLiteral("/ReShade.ini");
    m_available = QFileInfo::exists(m_configPath);
    m_activePresetPath = resolveActivePreset();

    QJsonArray output;
    for (const QJsonValue &value : loadMetadata()) {
        QJsonObject item = value.toObject();
        const QString path = storageDir() + QLatin1Char('/') + item.value(QStringLiteral("file")).toString();
        if (!QFileInfo::exists(path)) continue;
        const QStringList missing = missingShaders(path);
        QJsonArray missingJson;
        for (const QString &name : missing) missingJson.append(name);
        item.insert(QStringLiteral("missingShaders"), missingJson);
        item.insert(QStringLiteral("warning"), missing.isEmpty() ? QString() :
                    QStringLiteral("Faltan shaders: ") + missing.join(QStringLiteral(", ")));
        output.append(item);
    }
    m_presetsJson = QString::fromUtf8(QJsonDocument(output).toJson(QJsonDocument::Compact));
    emit changed();
}

void ReShadePresetService::refresh() { rebuildState(); }

bool ReShadePresetService::copyAtomic(const QString &source, const QString &target, QString *error) const {
    QFile input(source);
    if (!input.open(QIODevice::ReadOnly)) { if (error) *error = QStringLiteral("No se pudo leer %1").arg(source); return false; }
    QDir().mkpath(QFileInfo(target).absolutePath());
    const QString temporary = target + QStringLiteral(".tmp");
    QFile output(temporary);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) { if (error) *error = QStringLiteral("No se pudo escribir %1").arg(target); return false; }
    output.write(input.readAll());
    output.close(); input.close();
    QFile::remove(target);
    if (!QFile::rename(temporary, target)) { QFile::remove(temporary); if (error) *error = QStringLiteral("No se pudo publicar %1").arg(target); return false; }
    return true;
}

bool ReShadePresetService::backupCurrentConfiguration(QString *backupPath, QString *error) const {
    if (!m_available) return true;
    const QString dir = backupDir() + QLatin1Char('/') + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz"));
    if (!QDir().mkpath(dir)) { if (error) *error = QStringLiteral("No se pudo crear el backup."); return false; }
    if (!copyAtomic(m_configPath, dir + QStringLiteral("/ReShade.ini"), error)) return false;
    if (QFileInfo::exists(m_activePresetPath) && !copyAtomic(m_activePresetPath, dir + QStringLiteral("/active_preset.ini"), error)) return false;
    if (backupPath) *backupPath = dir;
    return true;
}

bool ReShadePresetService::updatePresetPath(const QString &relativePath, QString *error) const {
    QFile file(m_configPath);
    if (!file.open(QIODevice::ReadOnly)) { if (error) *error = QStringLiteral("No se pudo leer ReShade.ini."); return false; }
    QStringList lines = QString::fromUtf8(file.readAll()).split('\n');
    file.close();
    bool inGeneral = false, replaced = false, hasGeneral = false;
    for (int i = 0; i < lines.size(); ++i) {
        const QString trimmed = lines.at(i).trimmed();
        if (trimmed.startsWith('[') && trimmed.endsWith(']')) {
            inGeneral = trimmed.mid(1, trimmed.size() - 2).trimmed().compare(QStringLiteral("GENERAL"), Qt::CaseInsensitive) == 0;
            hasGeneral = hasGeneral || inGeneral;
        } else if (inGeneral) {
            const int equals = trimmed.indexOf('=');
            const QString key = equals > 0 ? trimmed.left(equals).trimmed() : QString();
            if (equals > 0 && (key.compare(QStringLiteral("PresetPath"), Qt::CaseInsensitive) == 0
                               || key.compare(QStringLiteral("CurrentPresetPath"), Qt::CaseInsensitive) == 0)) {
                lines[i] = key + QLatin1Char('=') + relativePath;
                replaced = true;
            }
        }
    }
    if (!hasGeneral) { lines << QStringLiteral("[GENERAL]"); lines << QStringLiteral("PresetPath=") + relativePath; }
    else if (!replaced) {
        for (int i = lines.size() - 1; i >= 0; --i) {
            if (lines.at(i).trimmed().compare(QStringLiteral("[GENERAL]"), Qt::CaseInsensitive) == 0) {
                lines.insert(i + 1, QStringLiteral("PresetPath=") + relativePath); break;
            }
        }
    }
    const QString temporary = m_configPath + QStringLiteral(".tmp");
    QFile out(temporary);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = QStringLiteral("No se pudo escribir ReShade.ini.");
        return false;
    }
    out.write(lines.join('\n').toUtf8());
    out.close();
    QFile::remove(m_configPath);
    if (!QFile::rename(temporary, m_configPath)) {
        QFile::remove(temporary);
        if (error) *error = QStringLiteral("No se pudo publicar ReShade.ini.");
        return false;
    }
    return true;
}

void ReShadePresetService::fail(const QString &message) { emit errorOccurred(message); }

bool ReShadePresetService::saveCurrentPreset(const QString &name) {
    const QString clean = cleanName(name);
    if (clean.isEmpty() || !m_available || !QFileInfo::exists(m_activePresetPath)) { fail(QStringLiteral("No se encontró un preset activo de ReShade.")); return false; }
    QJsonArray items = loadMetadata();
    for (const QJsonValue &value : items) if (value.toObject().value(QStringLiteral("name")).toString() == clean) { fail(QStringLiteral("Ya existe un preset con ese nombre.")); return false; }
    const QString file = QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".ini");
    QString error;
    if (!copyAtomic(m_activePresetPath, storageDir() + QLatin1Char('/') + file, &error)) { fail(error); return false; }
    items.append(QJsonObject{{QStringLiteral("name"), clean}, {QStringLiteral("file"), file}, {QStringLiteral("created"), QDateTime::currentDateTime().toString(Qt::ISODate)}});
    if (!writeMetadata(items)) { fail(QStringLiteral("No se pudo guardar el índice de presets.")); return false; }
    rebuildState(); emit operationFinished(QStringLiteral("Preset guardado: %1").arg(clean)); return true;
}

bool ReShadePresetService::restorePreset(const QString &name) {
    const QString source = presetFileForName(name);
    if (source.isEmpty() || !QFileInfo::exists(source)) { fail(QStringLiteral("No se encontró el preset seleccionado.")); return false; }
    if (!m_available) { fail(QStringLiteral("ReShade.ini no está instalado en la carpeta del juego.")); return false; }
    QString error, backup;
    if (!backupCurrentConfiguration(&backup, &error)) { fail(error); return false; }
    const QString managedName = QStringLiteral("StellarTool_") + QFileInfo(source).completeBaseName() + QStringLiteral(".ini");
    const QString target = managedDir() + QLatin1Char('/') + managedName;
    if (!copyAtomic(source, target, &error)) { fail(error); return false; }
    const QString relative = QDir(m_reshadeDir).relativeFilePath(target).replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (!updatePresetPath(relative, &error)) { fail(error); return false; }
    m_lastBackupPath = backup; rebuildState(); emit changed(); emit operationFinished(QStringLiteral("Preset restaurado: %1").arg(name)); return true;
}

bool ReShadePresetService::renamePreset(const QString &oldName, const QString &newName) {
    const QString clean = cleanName(newName);
    if (clean.isEmpty() || presetFileForName(oldName).isEmpty()) return false;
    QJsonArray items = loadMetadata();
    if (clean != oldName && !presetFileForName(clean).isEmpty()) return false;
    for (int i = 0; i < items.size(); ++i) {
        if (items.at(i).toObject().value(QStringLiteral("name")).toString() == oldName) {
            QJsonObject item = items.at(i).toObject();
            item.insert(QStringLiteral("name"), clean);
            items.replace(i, item);
        }
    }
    if (!writeMetadata(items)) return false; rebuildState(); return true;
}

bool ReShadePresetService::deletePreset(const QString &name) {
    const QString path = presetFileForName(name);
    if (path.isEmpty()) return false;
    QFile::remove(path); QJsonArray items = loadMetadata();
    for (int i = items.size() - 1; i >= 0; --i) if (items.at(i).toObject().value(QStringLiteral("name")).toString() == name) items.removeAt(i);
    if (!writeMetadata(items)) return false; rebuildState(); return true;
}

bool ReShadePresetService::importPreset(const QUrl &fileUrl) {
    const QString source = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    if (!QFileInfo(source).isFile() || QFileInfo(source).suffix().compare(QStringLiteral("ini"), Qt::CaseInsensitive) != 0) { fail(QStringLiteral("Elegí un archivo .ini de ReShade.")); return false; }
    QString name = cleanName(QFileInfo(source).completeBaseName());
    QJsonArray items = loadMetadata(); QString base = name; int index = 2;
    while (!presetFileForName(name).isEmpty()) name = base + QStringLiteral(" (%1)").arg(index++);
    const QString file = QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".ini"); QString error;
    if (!copyAtomic(source, storageDir() + QLatin1Char('/') + file, &error)) { fail(error); return false; }
    items.append(QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("file"), file}, {QStringLiteral("created"), QDateTime::currentDateTime().toString(Qt::ISODate)}});
    if (!writeMetadata(items)) { fail(QStringLiteral("No se pudo guardar el índice de presets.")); return false; }
    rebuildState(); emit operationFinished(QStringLiteral("Preset importado: %1").arg(name)); return true;
}

bool ReShadePresetService::exportPreset(const QString &name, const QUrl &fileUrl) {
    QString target = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    if (target.isEmpty()) return false;
    if (!target.endsWith(QStringLiteral(".ini"), Qt::CaseInsensitive)) target += QStringLiteral(".ini");
    const QString source = presetFileForName(name); QString error;
    if (source.isEmpty() || !copyAtomic(source, target, &error)) { fail(error.isEmpty() ? QStringLiteral("No se encontró el preset seleccionado.") : error); return false; }
    emit operationFinished(QStringLiteral("Preset exportado.")); return true;
}

void ReShadePresetService::openReshadeDir() {
    if (!m_reshadeDir.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(m_reshadeDir));
}

} // namespace st
