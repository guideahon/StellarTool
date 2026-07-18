#pragma once

#include <QString>
#include <QStringList>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QList>
#include <QMap>

namespace st {

// Un asset dentro de un mod (ruta relativa dentro del pak = identidad in-game).
struct ModAsset {
    enum Kind { DataTable, Other, Unreadable };
    QString gamePath;      // ruta relativa normalizada (separador '/', case preservado)
    QString localPath;     // ruta absoluta del archivo extraído (.uasset)
    QString jsonPath;      // ruta del JSON generado por UAssetGUI (si DataTable)
    Kind kind = Other;
    QString error;         // motivo si Unreadable
    bool cleanJson = false; // jsonPath viene de CUE4Parse (valores "limpios", no
                            // serializables a uasset para tipos complejos)

    // Clave case-insensitive para comparar rutas entre mods (Windows/UE).
    QString pathKey() const { return gamePath.toLower(); }
};

struct ModPackage {
    QString id;            // hash corto de sourcePath
    QString name;          // nombre visible (archivo/carpeta)
    QString sourcePath;    // zip/carpeta/pak original
    QString extractDir;    // dir temporal con el contenido extraído
    int loadOrder = 0;     // menor = mayor prioridad por defecto
    QList<ModAsset> assets;

    int tableCount() const;
    int otherCount() const;
    int unreadableCount() const;
};

// Segmentos de propertyPath (identidad estable de un cambio dentro de una fila):
//   "K:<key>"        clave de objeto JSON
//   "N:<name>#<i>"   elemento de array de propiedades UAssetAPI matcheado por Name
//                    (i = ocurrencia, para Names duplicados)
//   "I:<idx>"        índice de array puro
struct ChangeItem {
    enum Type { Modified, RowAdded, RowRemoved, AssetReplaced };

    QString modId;
    QString modName;
    QString tablePath;     // gamePath del uasset (o del asset, si AssetReplaced)
    QString rowName;       // vacío para AssetReplaced
    QStringList propPath;  // vacío para RowAdded/RowRemoved/AssetReplaced
    QJsonValue baseValue{QJsonValue::Undefined}; // Undefined si no hay baseline
    QJsonValue newValue{QJsonValue::Undefined};  // fila completa para RowAdded; Undefined para RowRemoved
    Type type = Modified;
    bool selected = true;
    bool clean = false;     // valor leído con CUE4Parse (write-back solo escalares)
    int conflictGroup = -1; // -1 = sin conflicto

    QString key() const;          // tablePath|rowName|propPath (case-insensitive en tablePath)
    QString displayPath() const;  // "Stats.MaxHP" legible
    QString summary() const;      // resumen humano
};

struct ConflictGroup {
    int id = -1;
    QString key;
    QList<int> itemIndexes;   // índices dentro del vector global de ChangeItem
    QString resolvedModId;    // vacío = sin resolver
};

// ---- Helpers de JSON UAssetAPI ----

// Devuelve puntero lógico al array de filas ("Exports[i].Table.Data") de un
// JSON de UAssetGUI. Retorna array vacío si no es una DataTable.
QJsonArray dataTableRows(const QJsonObject &root);
// Reemplaza el array de filas y devuelve el documento completo actualizado.
QJsonObject withDataTableRows(const QJsonObject &root, const QJsonArray &rows);
bool isDataTableJson(const QJsonObject &root);

// Convierte el JSON de una DataTable exportado por CUE4Parse (array con
// [0].Rows = { fila: { prop: valor } }) al shape UAssetAPI que consume el diff
// (Exports[0].Table.Data = [ {Name, Value:[props]} ]). Devuelve objeto vacío si
// el JSON no tiene la forma esperada.
QJsonObject cue4ToDataTableDoc(const QByteArray &cue4Json);

// Normaliza una DataTable (UAssetAPI/UAssetGUI o ya convertida de CUE4Parse) a
// una forma canónica limpia: cada propiedad = {Name, Value}, descartando la
// metadata de serialización de UAssetGUI ($type, ArrayIndex, PropertyGuid,
// IsZero, StructGUID, etc.). Así el diff compara valores reales sin importar
// con qué parser se leyó cada lado. Los propPath (por Name) siguen siendo
// válidos para aplicar el merge sobre el JSON real de UAssetGUI.
QJsonObject normalizeDataTableDoc(const QJsonObject &root);

// Comparación de valores con tolerancia float (epsilon relativo 1e-6).
bool jsonValueEquals(const QJsonValue &a, const QJsonValue &b);

QString shortHash(const QString &s);

} // namespace st
