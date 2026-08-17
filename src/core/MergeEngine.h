#pragma once

#include "ModTypes.h"

#include <QHash>

namespace st {

// Aplica ChangeItems seleccionados sobre el JSON base de cada tabla.
// Round-trip fiel: parte del JSON existente y solo toca los paths elegidos.
class MergeEngine {
public:
    struct Result {
        bool ok = false;
        QString error;
        int applied = 0;
        int skipped = 0;   // cambios clean sin plantilla/layout escribible
    };

    // baseRoot: JSON completo de la tabla de partida (baseline, o tabla del
    // mod de mayor prioridad si no hay baseline). items: solo los de esta tabla.
    static Result applyToTable(QJsonObject &root, const QList<ChangeItem> &items);

    // UE guarda "Valor_3" como el FName "Valor" con número, y UAssetGUI lo lee
    // expandido ("Valor_3") pero no sabe volver a escribirlo: el uasset no se
    // genera y la tabla entera queda fuera del merge. Se detectan por no estar
    // en el NameMap del asset y se reescriben como ByteProperty numérica (el
    // índice dentro del enum), forma que sí round-tripea al mismo valor.
    // 'enums' viene de UsmapService::loadEnums; vacío = no se toca nada.
    // Devuelve cuántas propiedades se reescribieron.
    static int rewriteNumberedEnums(QJsonObject &root,
                                    const QHash<QString, QStringList> &enums);

    // Completa ArrayType en arrays vacíos a partir del schema del .usmap.
    // UAssetGUI deja ArrayType=null cuando no hay elementos y UAssetAPI no
    // puede serializar ese asset aunque el array no se modifique.
    static int fillMissingArrayTypes(QJsonObject &root,
                                     const QHash<QString, QString> &arrayTypes);

    // Aplica un path sobre una fila; expuesto para tests. allowCreate=false
    // impide crear propiedades/entradas inexistentes (para valores "clean" de
    // CUE4Parse, que no deben inyectar props ausentes en el JSON de UAssetGUI).
    static bool applyPath(QJsonValue &node, const QStringList &path, int depth,
                          const QJsonValue &newValue, bool allowCreate = true);
};

} // namespace st
