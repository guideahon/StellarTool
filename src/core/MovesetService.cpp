#include "MovesetService.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QSaveFile>
#include <QSet>

#include <algorithm>

namespace st {
namespace {
QString manifestPath() {
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
           + QStringLiteral("/moveset_install.json");
}

QString normalizedId(QString value) {
    value = value.trimmed().toLower();
    value.replace(QLatin1Char(' '), QLatin1Char('-'));
    return value;
}

bool validFamily(const QString &family) {
    return family == QLatin1String("fusion") || family == QLatin1String("scarlet")
           || family == QLatin1String("raven");
}

bool fileSet(const QDir &dir, QStringList *files) {
    const QStringList names = dir.entryList({QStringLiteral("*.pak"), QStringLiteral("*.ucas"),
                                             QStringLiteral("*.utoc")}, QDir::Files);
    if (names.isEmpty()) return false;
    bool pak = false, ucas = false, utoc = false;
    for (const QString &name : names) {
        const QString ext = QFileInfo(name).suffix().toLower();
        pak |= ext == QLatin1String("pak");
        ucas |= ext == QLatin1String("ucas");
        utoc |= ext == QLatin1String("utoc");
    }
    if (!pak || !ucas || !utoc) return false;
    if (files) *files = names;
    return true;
}
}

QList<MovesetService::Variant> MovesetService::scan(const QString &root, QString *error) {
    QList<Variant> result;
    const QFileInfo rootInfo(root);
    if (!rootInfo.isDir()) {
        if (error) *error = QStringLiteral("La carpeta de variantes no existe: %1").arg(root);
        return result;
    }
    QDirIterator it(root, {QStringLiteral("*.pak")}, QDir::Files, QDirIterator::Subdirectories);
    QSet<QString> seen;
    while (it.hasNext()) {
        const QFileInfo pak(it.next());
        const QDir dir = pak.dir();
        QStringList files;
        if (!fileSet(dir, &files)) continue;
        const QString id = normalizedId(dir.dirName());
        if (seen.contains(id)) continue;
        const QStringList parts = id.split(QLatin1Char('-'), Qt::SkipEmptyParts);
        if (parts.isEmpty()) continue;
        const bool aggro = parts.first() == QLatin1String("aggro");
        const int familyIndex = aggro ? 1 : 0;
        if (familyIndex >= parts.size()) continue;
        const QString family = parts.at(familyIndex);
        if (!validFamily(family)) continue;
        QString tier = QStringLiteral("default");
        for (int i = familyIndex + 1; i < parts.size(); ++i) {
            if (parts.at(i) == QLatin1String("queen") || parts.at(i) == QLatin1String("goddess")
                     || parts.at(i) == QLatin1String("godqueen") || parts.at(i) == QLatin1String("godempress"))
                tier = parts.at(i);
        }
        Variant v;
        v.id = id; v.family = family; v.tier = tier; v.aggro = aggro;
        v.sourceDir = dir.absolutePath(); v.files = files;
        result << v;
        seen.insert(id);
    }
    std::sort(result.begin(), result.end(), [](const Variant &a, const Variant &b) { return a.id < b.id; });
    if (result.isEmpty() && error) *error = QStringLiteral("No se encontraron variantes completas (.pak + .ucas + .utoc).");
    return result;
}

QString MovesetService::describe(const Variant &v) {
    return QStringLiteral("%1 | familia=%2 | tier=%3 | aggro=%4 | archivos=%5")
        .arg(v.id, v.family, v.tier, v.aggro ? QStringLiteral("si") : QStringLiteral("no"))
        .arg(v.files.size());
}

bool MovesetService::copyVariant(const Variant &v, const QString &outDir, QString *error) {
    if (outDir.isEmpty()) { if (error) *error = QStringLiteral("Falta la carpeta de salida."); return false; }
    QDir().mkpath(outDir);
    for (const QString &name : v.files) {
        const QString dst = QDir(outDir).filePath(name);
        if (QFileInfo::exists(dst) && !QFile::remove(dst)) {
            if (error) *error = QStringLiteral("No se puede reemplazar %1").arg(dst);
            return false;
        }
        if (!QFile::copy(QDir(v.sourceDir).filePath(name), dst)) {
            if (error) *error = QStringLiteral("No se pudo copiar %1").arg(name);
            return false;
        }
    }
    return true;
}

bool MovesetService::installVariant(const Variant &v, const QString &gameRoot, QString *error) {
    const QString target = QDir(gameRoot).filePath(QStringLiteral("SB/Content/Paks/~mods"));
    for (const QString &name : v.files) {
        if (QFileInfo::exists(QDir(target).filePath(name))) {
            if (error) *error = QStringLiteral("La instalación se detuvo: ya existe %1; no se sobrescriben mods ajenos.")
                .arg(QDir(target).filePath(name));
            return false;
        }
    }
    if (!copyVariant(v, target, error)) return false;
    QDir().mkpath(QFileInfo(manifestPath()).absolutePath());
    QJsonObject doc{{QStringLiteral("variant"), v.id}, {QStringLiteral("target"), target}};
    QJsonArray files;
    for (const QString &name : v.files) files.append(QDir(target).filePath(name));
    doc.insert(QStringLiteral("files"), files);
    QSaveFile f(manifestPath());
    if (!f.open(QIODevice::WriteOnly) || !f.write(QJsonDocument(doc).toJson(QJsonDocument::Indented)) || !f.commit()) {
        if (error) *error = QStringLiteral("No se pudo guardar el registro de instalación.");
        return false;
    }
    return true;
}

bool MovesetService::uninstall(QString *error) {
    QFile f(manifestPath());
    if (!f.exists()) return true;
    if (!f.open(QIODevice::ReadOnly)) { if (error) *error = QStringLiteral("No se pudo leer el registro de instalación."); return false; }
    const QJsonObject doc = QJsonDocument::fromJson(f.readAll()).object();
    for (const QJsonValue &v : doc.value(QStringLiteral("files")).toArray()) {
        const QString path = v.toString();
        if (QFileInfo::exists(path) && !QFile::remove(path)) {
            if (error) *error = QStringLiteral("No se pudo quitar %1").arg(path);
            return false;
        }
    }
    f.close();
    return f.remove();
}

QString MovesetService::installedStatus() {
    QFile f(manifestPath());
    if (!f.open(QIODevice::ReadOnly)) return QStringLiteral("{}");
    return QString::fromUtf8(f.readAll());
}
} // namespace st
