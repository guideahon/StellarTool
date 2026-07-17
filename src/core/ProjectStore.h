#pragma once

#include "ModTypes.h"
#include <QObject>

namespace st {

// Persistencia de sesión (.stproj): rutas de mods, selecciones y resoluciones.
class ProjectStore : public QObject {
    Q_OBJECT
public:
    explicit ProjectStore(QObject *parent = nullptr);

    struct ProjectState {
        QStringList modSources;                 // en orden de prioridad
        QMap<QString, bool> selections;         // ChangeItem::key + "|" + modId -> selected
        QMap<QString, QString> resolutions;     // ConflictGroup::key -> modId ganador
    };

    bool save(const QString &path, const ProjectState &state, QString *error);
    bool load(const QString &path, ProjectState *state, QString *error);
};

} // namespace st
