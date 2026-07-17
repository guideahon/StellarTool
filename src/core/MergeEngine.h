#pragma once

#include "ModTypes.h"

namespace st {

// Aplica ChangeItems seleccionados sobre el JSON base de cada tabla.
// Round-trip fiel: parte del JSON existente y solo toca los paths elegidos.
class MergeEngine {
public:
    struct Result {
        bool ok = false;
        QString error;
        int applied = 0;
    };

    // baseRoot: JSON completo de la tabla de partida (baseline, o tabla del
    // mod de mayor prioridad si no hay baseline). items: solo los de esta tabla.
    static Result applyToTable(QJsonObject &root, const QList<ChangeItem> &items);

    // Aplica un path sobre una fila; expuesto para tests.
    static bool applyPath(QJsonValue &node, const QStringList &path, int depth,
                          const QJsonValue &newValue);
};

} // namespace st
