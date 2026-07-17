#pragma once

#include "ModTypes.h"

namespace st {

// Diff de DataTables (JSON UAssetAPI) y detección de conflictos entre mods.
// Sin estado; funciones puras testeables con fixtures JSON.
class TableDiffEngine {
public:
    // Diff de una tabla de un mod contra baseline. baseRoot puede ser objeto
    // vacío (sin baseline): en ese caso cada fila del mod se reporta como
    // Modified con baseValue Undefined a nivel de propiedades hoja.
    static QList<ChangeItem> diffTable(const QJsonObject &baseRoot,
                                       const QJsonObject &modRoot,
                                       const QString &tablePath,
                                       const QString &modId,
                                       const QString &modName);

    // Agrupa en conflictos los ChangeItem (de mods distintos) con misma key
    // y valores distintos. Muta items (conflictGroup). Cambios idénticos de
    // varios mods no son conflicto. Devuelve los grupos creados.
    static QList<ConflictGroup> findConflicts(QList<ChangeItem> &items);

private:
    static void diffRow(const QJsonValue &baseRow, const QJsonValue &modRow,
                        const QString &tablePath, const QString &rowName,
                        const QString &modId, const QString &modName,
                        QList<ChangeItem> &out);
    static void diffValue(const QJsonValue &base, const QJsonValue &mod,
                          QStringList path, ChangeItem prototype,
                          QList<ChangeItem> &out);
};

} // namespace st
