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

    // Reescribe enums conocidos como ByteProperty numérica (ordinal). Útil
    // para diagnóstico y pruebas de round-trip; no se aplica globalmente.
    // 'enums' viene de UsmapService::loadEnums; vacío = no se toca nada.
    // Devuelve cuántas propiedades se reescribieron.
    static int rewriteNumberedEnums(QJsonObject &root,
                                    const QHash<QString, QStringList> &enums);
    // Reintento acotado para un EnumProperty que UAssetGUI exige como byte.
    // Solo se usa tras el error exacto y siempre se valida con round-trip.
    static int rewriteEnumPropertyAsByte(QJsonObject &root, const QString &propertyName,
                                         const QHash<QString, QStringList> &enums);
    // Canonicaliza enums releídos por UAssetGUI desde ByteProperty a ordinal
    // para comparar semánticamente con el JSON de entrada.
    static int canonicalizeEnumOrdinals(QJsonObject &root,
                                        const QHash<QString, QStringList> &enums);
    // UAssetGUI puede descubrir un FName nuevo durante fromjson. Registrar
    // exactamente el nombre denunciado y reintentar, sin alterar los assets.
    static bool registerMissingFName(QJsonObject &root, const QString &name);

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
