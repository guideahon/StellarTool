#include "ProjectStore.h"

#include <QFile>
#include <QJsonDocument>

namespace st {

ProjectStore::ProjectStore(QObject *parent) : QObject(parent) {}

bool ProjectStore::save(const QString &path, const ProjectState &state, QString *error) {
    QJsonObject root;
    root.insert(QLatin1String("version"), 1);
    QJsonArray mods;
    for (const QString &m : state.modSources) mods.append(m);
    root.insert(QLatin1String("mods"), mods);
    QJsonObject sel;
    for (auto it = state.selections.begin(); it != state.selections.end(); ++it)
        sel.insert(it.key(), it.value());
    root.insert(QLatin1String("selections"), sel);
    QJsonObject res;
    for (auto it = state.resolutions.begin(); it != state.resolutions.end(); ++it)
        res.insert(it.key(), it.value());
    root.insert(QLatin1String("resolutions"), res);

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = tr("No se pudo escribir %1").arg(path);
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

bool ProjectStore::load(const QString &path, ProjectState *state, QString *error) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = tr("No se pudo abrir %1").arg(path);
        return false;
    }
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    if (root.isEmpty()) {
        if (error) *error = tr("Proyecto inválido: %1").arg(path);
        return false;
    }
    state->modSources.clear();
    for (const QJsonValue &v : root.value(QLatin1String("mods")).toArray())
        state->modSources << v.toString();
    state->selections.clear();
    const QJsonObject sel = root.value(QLatin1String("selections")).toObject();
    for (auto it = sel.begin(); it != sel.end(); ++it)
        state->selections.insert(it.key(), it.value().toBool());
    state->resolutions.clear();
    const QJsonObject res = root.value(QLatin1String("resolutions")).toObject();
    for (auto it = res.begin(); it != res.end(); ++it)
        state->resolutions.insert(it.key(), it.value().toString());
    return true;
}

} // namespace st
