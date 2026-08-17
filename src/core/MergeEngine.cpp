#include "MergeEngine.h"

#include <QSet>

namespace st {

static QString segName(const QString &seg, int *occurrence) {
    // "N:<name>#<i>"
    const QString body = seg.mid(2);
    const int hash = body.lastIndexOf(QLatin1Char('#'));
    *occurrence = body.mid(hash + 1).toInt();
    return body.left(hash);
}

// ¿Mismo tipo JSON de hoja? (int y double cuentan igual: ambos isDouble)
static bool sameLeafType(const QJsonValue &a, const QJsonValue &b) {
    if (a.isDouble() && b.isDouble()) return true;
    if (a.isBool() && b.isBool()) return true;
    if (a.isString() && b.isString()) return true;
    if (a.isArray() && b.isArray()) return true;
    if (a.isObject() && b.isObject()) return true;
    return a.isNull() && b.isNull();
}

// ¿Se puede escribir 'nv' (valor clean normalizado) sobre el leaf real 'base'
// de UAssetGUI? Igual tipo, o un string sobre un FName None (null en UAssetGUI):
// ej. NextStepAlias null -> "P_Eve_...". base Undefined = sin base (no chequear).
// UAssetGUI serializa el float cero como el STRING "+0"/"-0". El diff lo
// normaliza a 0, así que al escribir un número sobre él los tipos no coinciden
// aunque el cambio sea perfectamente válido (y es el caso más común: activar
// algo que en vanilla vale cero).
static bool isFloatZeroString(const QJsonValue &v) {
    if (!v.isString()) return false;
    const QString s = v.toString();
    return s == QLatin1String("+0") || s == QLatin1String("-0");
}

static bool writableLeaf(const QJsonValue &base, const QJsonValue &nv) {
    return base.isUndefined() || sameLeafType(base, nv)
        || (base.isNull() && nv.isString())
        || (isFloatZeroString(base) && nv.isDouble());
}

// Reconciliar un string clean (normalizado por el diff) a la forma real de
// UAssetGUI del leaf base, para escribirlo sin corromperlo:
//  - Enums: el diff quita el namespace ("ESBTipo::Valor" -> "Valor"). Si la base
//    lo tenía, re-prefijarlo ("Valor" -> "ESBTipo::Valor") con el nuevo valor.
// Números/bool y strings ya en forma UAssetGUI pasan sin cambios.
static QJsonValue reconcileLeaf(const QJsonValue &base, const QJsonValue &nv) {
    if (nv.isString() && base.isString()) {
        const QString b = base.toString();
        const int sep = b.lastIndexOf(QLatin1String("::"));
        const QString s = nv.toString();
        if (sep >= 0 && !s.contains(QLatin1String("::")))
            return QJsonValue(b.left(sep + 2) + s);
    }
    return nv;
}

// Cómo se escribe un FName vacío (FName None) en el JSON de UAssetGUI.
// Las tres formas posibles NO son equivalentes al generar el uasset:
//   ""     -> UAssetAPI tira "Cannot add an empty FString to the name map",
//             muere sin escribir nada y sin imprimir el error.
//   null   -> escribe, pero al releer vuelve como " " (un espacio): la tabla
//             deja de round-tripear y queda excluida del merge.
//   "None" -> única forma que sobrevive fromjson + tojson intacta.
static const QLatin1String kEmptyFName("None");

// ¿Es un NameProperty? (su $type es "...NamePropertyData, UAssetAPI")
static bool isNameProperty(const QJsonObject &o) {
    return o.value(QLatin1String("$type")).toString().contains(QLatin1String("NameProperty"));
}

// ¿Objeto-propiedad de UAssetGUI? ({Name, Value} + metadata de serialización)
static bool isWrapperObj(const QJsonObject &o) {
    return o.contains(QLatin1String("Name")) && o.contains(QLatin1String("Value"));
}

static QString propertyDataType(const QString &propertyType) {
    static const QSet<QString> supported{
        QStringLiteral("BoolProperty"), QStringLiteral("ByteProperty"),
        QStringLiteral("DoubleProperty"), QStringLiteral("EnumProperty"),
        QStringLiteral("FloatProperty"), QStringLiteral("Int16Property"),
        QStringLiteral("Int64Property"), QStringLiteral("Int8Property"),
        QStringLiteral("IntProperty"), QStringLiteral("NameProperty"),
        QStringLiteral("ObjectProperty"), QStringLiteral("SoftObjectProperty"),
        QStringLiteral("StrProperty"), QStringLiteral("TextProperty"),
        QStringLiteral("UInt16Property"), QStringLiteral("UInt32Property"),
        QStringLiteral("UInt64Property")
    };
    if (!supported.contains(propertyType)) return {};
    return QStringLiteral("UAssetAPI.PropertyTypes.Objects.%1Data, UAssetAPI").arg(propertyType);
}

// Un ArrayPropertyData vacío no ofrece un elemento para usar de molde. Para
// tipos escalares, ArrayType alcanza para construir el wrapper estándar.
// StructProperty queda fuera: además del tipo necesita el layout del struct.
static QJsonValue fillEmptyArray(const QJsonObject &arrayProperty,
                                 const QJsonArray &clean) {
    const QString dataType =
        propertyDataType(arrayProperty.value(QLatin1String("ArrayType")).toString());
    if (dataType.isEmpty()) return QJsonValue(QJsonValue::Undefined);

    QJsonArray out;
    for (int i = 0; i < clean.size(); ++i) {
        QJsonValue value = clean.at(i);
        if (dataType.contains(QLatin1String("NameProperty"))
            && value.isString() && value.toString().isEmpty())
            value = QJsonValue(kEmptyFName);
        out.append(QJsonObject{
            {QStringLiteral("$type"), dataType},
            {QStringLiteral("ArrayIndex"), i},
            {QStringLiteral("PropertyGuid"), QJsonValue(QJsonValue::Null)},
            {QStringLiteral("IsZero"), false},
            {QStringLiteral("PropertyTagFlags"), QStringLiteral("None")},
            {QStringLiteral("PropertyTagExtensions"), QStringLiteral("NoExtension")},
            {QStringLiteral("Value"), value},
        });
    }
    return out;
}

// Copia los valores normalizados de 'clean' sobre la ESTRUCTURA de 'tmpl' (una
// fila real de la tabla, con su $type y metadata). Las propiedades que 'clean'
// no traiga conservan el valor de la plantilla; las que la plantilla no tenga
// se ignoran (no hay forma de inventar su forma cruda).
static QJsonValue fillTemplate(const QJsonValue &tmpl, const QJsonValue &clean) {
    if (tmpl.isObject()) {
        const QJsonObject to = tmpl.toObject();
        // Wrapper {Name,Value,...}: el clean trae el valor pelado.
        if (isWrapperObj(to)
            && !(clean.isObject() && clean.toObject().contains(QLatin1String("Name")))) {
            QJsonObject out = to;
            QJsonValue v;
            const QJsonValue templateValue = to.value(QLatin1String("Value"));
            if (to.value(QLatin1String("$type")).toString()
                    .contains(QLatin1String("ArrayPropertyData"))
                && templateValue.isArray() && templateValue.toArray().isEmpty()
                && clean.isArray() && !clean.toArray().isEmpty())
                v = fillEmptyArray(to, clean.toArray());
            else
                v = fillTemplate(templateValue, clean);
            // El diff canoniza el FName None como "": ver kEmptyFName.
            if (v.isString() && v.toString().isEmpty() && isNameProperty(to))
                v = QJsonValue(kEmptyFName);
            out.insert(QLatin1String("Value"), v);
            return out;
        }
    }
    if (tmpl.isArray() && clean.isArray()) {
        const QJsonArray ta = tmpl.toArray(), ca = clean.toArray();
        // Array de propiedades con nombre: matchear por Name.
        bool named = !ta.isEmpty();
        for (const QJsonValue &e : ta)
            if (!e.isObject() || !isWrapperObj(e.toObject())) { named = false; break; }
        if (named) {
            QHash<QString, QJsonValue> byName;
            for (const QJsonValue &e : ca) {
                const QJsonObject co = e.toObject();
                if (co.contains(QLatin1String("Name")))
                    byName.insert(co.value(QLatin1String("Name")).toString(),
                                  co.value(QLatin1String("Value")));
            }
            QJsonArray out;
            for (const QJsonValue &e : ta) {
                QJsonObject po = e.toObject();
                const QString nm = po.value(QLatin1String("Name")).toString();
                if (byName.contains(nm))
                    po.insert(QLatin1String("Value"),
                              fillTemplate(po.value(QLatin1String("Value")), byName.value(nm)));
                out.append(po);
            }
            return out;
        }
        // Array indexado: clonar el último elemento como molde y reindexar.
        if (ta.isEmpty()) return ca.isEmpty() ? tmpl : QJsonValue(QJsonValue::Undefined);
        QJsonArray out;
        for (int i = 0; i < ca.size(); ++i) {
            QJsonValue e = fillTemplate(i < ta.size() ? ta.at(i) : ta.last(), ca.at(i));
            if (e.isUndefined()) return QJsonValue(QJsonValue::Undefined);
            if (e.isObject()) {
                QJsonObject eo = e.toObject();
                const QString nm = eo.value(QLatin1String("Name")).toString();
                bool numeric = !nm.isEmpty();
                for (const QChar ch : nm) if (!ch.isDigit()) { numeric = false; break; }
                if (numeric) {
                    eo.insert(QLatin1String("Name"), QString::number(i));
                    if (eo.contains(QLatin1String("ArrayIndex")))
                        eo.insert(QLatin1String("ArrayIndex"), i);
                    e = eo;
                }
            }
            out.append(e);
        }
        return out;
    }
    if (clean.isString()) return reconcileLeaf(tmpl, clean);
    if (clean.isUndefined()) return tmpl;
    return clean;
}

// Defensa final: cualquier NameProperty (anidado o no) cuyo valor normalizado
// sea "" representa FName None y hay que escribirlo como kEmptyFName. Los que
// ya venían null de vanilla se dejan como están: round-tripean bien.
static QJsonValue normalizeEmptyFNames(const QJsonValue &value) {
    if (value.isArray()) {
        QJsonArray out;
        for (const QJsonValue &element : value.toArray())
            out.append(normalizeEmptyFNames(element));
        return out;
    }
    if (!value.isObject()) return value;

    QJsonObject out = value.toObject();
    for (auto it = out.begin(); it != out.end(); ++it)
        it.value() = normalizeEmptyFNames(it.value());
    const QJsonValue v = out.value(QLatin1String("Value"));
    if (isNameProperty(out) && v.isString() && v.toString().isEmpty())
        out.insert(QLatin1String("Value"), QJsonValue(kEmptyFName));
    return out;
}

// Construye una fila cruda nueva a partir de otra fila de la MISMA tabla como
// plantilla: todas las filas de una DataTable comparten el struct, así que
// cualquiera sirve para obtener $type y metadata. Vacío si no se puede.
static QJsonValue buildRowFromTemplate(const QJsonValue &tmpl, const QJsonValue &cleanRow,
                                       const QString &rowName) {
    if (!tmpl.isObject() || !cleanRow.isObject()) return QJsonValue(QJsonValue::Undefined);
    QJsonObject out = tmpl.toObject();
    const QJsonValue filled = fillTemplate(out.value(QLatin1String("Value")),
                                           cleanRow.toObject().value(QLatin1String("Value")));
    if (filled.isUndefined()) return QJsonValue(QJsonValue::Undefined);
    out.insert(QLatin1String("Value"), filled);
    out.insert(QLatin1String("Name"), rowName);
    return normalizeEmptyFNames(out);
}

// Vanilla no serializa las propiedades que valen el default (0, vacío...), así
// que un mod que las activa apunta a una propiedad que no existe en la fila
// base. Se busca la misma propiedad en cualquier otra fila de la tabla (mismo
// struct) para usarla de plantilla, se le pone el valor del mod y se agrega.
// Devuelve false si ninguna fila la tiene (no hay forma de deducir su forma).
static bool addPropFromTemplate(QJsonValue &row, const QStringList &path,
                                const QJsonValue &newValue, const QJsonArray &rows) {
    // Propiedad de primer nivel de la fila: el diff la referencia como
    // ["K:Value", "N:<prop>#0"] (o solo ["N:<prop>#0"]). Los paths más
    // profundos (dentro de structs anidados) no se reconstruyen.
    if (path.isEmpty() || path.size() > 2) return false;
    if (path.size() == 2 && path.first() != QLatin1String("K:Value")) return false;
    const QString last = path.last();
    if (!last.startsWith(QLatin1String("N:"))) return false;
    int occurrence = 0;
    const QString name = segName(last, &occurrence);
    if (occurrence != 0) return false;

    QJsonObject tmplProp;
    for (const QJsonValue &r : rows) {
        for (const QJsonValue &p : r.toObject().value(QLatin1String("Value")).toArray()) {
            const QJsonObject po = p.toObject();
            if (po.value(QLatin1String("Name")).toString() == name) { tmplProp = po; break; }
        }
        if (!tmplProp.isEmpty()) break;
    }
    if (tmplProp.isEmpty()) return false;

    tmplProp.insert(QLatin1String("Value"),
                    fillTemplate(tmplProp.value(QLatin1String("Value")), newValue));
    QJsonObject ro = row.toObject();
    QJsonArray props = ro.value(QLatin1String("Value")).toArray();
    props.append(tmplProp);
    ro.insert(QLatin1String("Value"), props);
    row = ro;
    return true;
}

// Recolecta recursivamente los FName usados por 'v': el Value (string) de las
// propiedades Name/Enum del JSON de UAssetGUI.
static void collectFNames(const QJsonValue &v, QStringList &out) {
    if (v.isObject()) {
        const QJsonObject o = v.toObject();
        const QString type = o.value(QLatin1String("$type")).toString();
        const QJsonValue val = o.value(QLatin1String("Value"));
        if (val.isString()
            && (type.contains(QLatin1String("NameProperty"))
                || type.contains(QLatin1String("EnumProperty"))))
            out << val.toString();
        for (auto it = o.begin(); it != o.end(); ++it) collectFNames(it.value(), out);
        return;
    }
    if (v.isArray())
        for (const QJsonValue &e : v.toArray()) collectFNames(e, out);
}

// Agrega al NameMap del asset los FName de las filas tocadas que aún no estén.
// Sin esto UAssetAPI los trata como "dummy FName" y al escribir tira
// DummyFNameSerializationException: UAssetGUI muere sin generar el uasset y sin
// imprimir nada (su error va al portapapeles), lo que se veía como el error
// "UAssetGUI no produjo el uasset esperado". Agregar nombres de más es inocuo:
// UAssetGUI recalcula NamesReferencedFromExportDataCount al escribir.
static void registerFNames(QJsonObject &root, const QJsonArray &rows,
                           const QSet<QString> &touchedRows) {
    if (touchedRows.isEmpty()) return;
    QStringList used = touchedRows.values();
    for (const QJsonValue &r : rows) {
        if (touchedRows.contains(r.toObject().value(QLatin1String("Name")).toString()))
            collectFNames(r, used);
    }
    if (used.isEmpty()) return;
    QJsonArray nameMap = root.value(QLatin1String("NameMap")).toArray();
    QSet<QString> have;
    have.reserve(nameMap.size());
    for (const QJsonValue &n : nameMap) have.insert(n.toString());
    bool added = false;
    for (const QString &n : used) {
        if (n.isEmpty() || have.contains(n)) continue;
        nameMap.append(n);
        have.insert(n);
        added = true;
    }
    if (added) root.insert(QLatin1String("NameMap"), nameMap);
}

// Reescribe in-place las EnumProperty con FName numerado. Devuelve cuántas.
static int rewriteEnumsIn(QJsonValue &value, const QSet<QString> &nameMap,
                          const QHash<QString, QStringList> &enums) {
    if (value.isArray()) {
        QJsonArray arr = value.toArray();
        int n = 0;
        for (int i = 0; i < arr.size(); ++i) {
            QJsonValue e = arr.at(i);
            n += rewriteEnumsIn(e, nameMap, enums);
            arr.replace(i, e);
        }
        value = arr;
        return n;
    }
    if (!value.isObject()) return 0;

    QJsonObject obj = value.toObject();
    int n = 0;
    QJsonValue inner = obj.value(QLatin1String("Value"));
    n += rewriteEnumsIn(inner, nameMap, enums);
    obj.insert(QLatin1String("Value"), inner);

    const QJsonValue v = obj.value(QLatin1String("Value"));
    if (obj.value(QLatin1String("$type")).toString().contains(QLatin1String("EnumPropertyData"))
        && v.isString()) {
        const QStringList values = enums.value(obj.value(QLatin1String("EnumType")).toString());
        const int ordinal = values.indexOf(v.toString());
        if (ordinal >= 0) {
            obj.insert(QLatin1String("$type"),
                       QStringLiteral("UAssetAPI.PropertyTypes.Objects.BytePropertyData, UAssetAPI"));
            obj.insert(QLatin1String("ByteType"), QStringLiteral("Byte"));
            obj.insert(QLatin1String("Value"), ordinal);
            obj.remove(QLatin1String("EnumType"));
            obj.remove(QLatin1String("InnerType"));
            ++n;
        }
    }
    value = obj;
    return n;
}

int MergeEngine::rewriteNumberedEnums(QJsonObject &root,
                                      const QHash<QString, QStringList> &enums) {
    if (enums.isEmpty()) return 0;
    QSet<QString> nameMap;
    for (const QJsonValue &n : root.value(QLatin1String("NameMap")).toArray())
        nameMap.insert(n.toString());
    QJsonValue rows = dataTableRows(root);
    const int n = rewriteEnumsIn(rows, nameMap, enums);
    if (n > 0) root = withDataTableRows(root, rows.toArray());
    return n;
}

bool MergeEngine::applyPath(QJsonValue &node, const QStringList &path, int depth,
                            const QJsonValue &newValue, bool allowCreate) {
    if (depth == path.size()) {
        // "" normalizado equivale a null/None del JSON real: no tocar.
        if (newValue.isString() && newValue.toString().isEmpty()
            && (node.isNull() || (node.isString()
                && (node.toString().isEmpty() || node.toString() == QLatin1String("None")))))
            return true;
        // Elemento de array del JSON real: wrapper {Name,Value}. El diff opera
        // sobre la forma normalizada (valor pelado): actualizar solo Value.
        if (node.isObject() && !newValue.isObject()) {
            QJsonObject wrap = node.toObject();
            if (wrap.contains(QLatin1String("Name")) && wrap.contains(QLatin1String("Value"))) {
                const QJsonValue inner = wrap.value(QLatin1String("Value"));
                if (!allowCreate && !inner.isUndefined() && !newValue.isArray()
                    && !writableLeaf(inner, newValue))
                    return false;
                // fillTemplate reconstruye la forma cruda que UAssetAPI espera
                // (wrappers con $type, índices). Escribir el valor normalizado
                // tal cual mete strings pelados donde va un PropertyData.
                const QJsonValue filled = fillTemplate(inner, newValue);
                if (filled.isUndefined()) return false;
                wrap.insert(QLatin1String("Value"), filled);
                node = wrap;
                return true;
            }
        }
        // Para valores "clean" (CUE4Parse), solo reemplazar si es escribible
        // sobre el valor real de UAssetGUI (mismo tipo, o string sobre None):
        // evita meter un valor donde el uasset espera otro tipo, lo que rompería
        // fromjson silenciosamente. reconcileLeaf ajusta la forma (enum namespace).
        if (!allowCreate && !node.isUndefined() && !newValue.isArray() && !newValue.isObject()
            && !writableLeaf(node, newValue))
            return false;
        const QJsonValue filled = fillTemplate(node, newValue);
        if (filled.isUndefined()) return false;
        node = filled;
        return true;
    }
    const QString &seg = path.at(depth);

    if (seg.startsWith(QLatin1String("K:"))) {
        if (!node.isObject()) return false;
        QJsonObject obj = node.toObject();
        const QString key = seg.mid(2);
        if (!obj.contains(key) && !allowCreate) return false;
        QJsonValue child = obj.contains(key) ? obj.value(key) : QJsonValue(QJsonValue::Undefined);
        if (depth + 1 == path.size() && newValue.isUndefined()) {
            obj.remove(key);
        } else if (depth + 1 == path.size()
                   && key == QLatin1String("Value")
                   && child.isArray() && child.toArray().isEmpty()
                   && newValue.isArray() && !newValue.toArray().isEmpty()
                   && obj.value(QLatin1String("$type")).toString()
                          .contains(QLatin1String("ArrayPropertyData"))) {
            child = fillEmptyArray(obj, newValue.toArray());
            if (child.isUndefined()) return false;
            obj.insert(key, child);
        } else {
            if (!applyPath(child, path, depth + 1, newValue, allowCreate)) return false;
            obj.insert(key, child);
        }
        node = obj;
        return true;
    }

    if (seg.startsWith(QLatin1String("I:"))) {
        if (!node.isArray()) return false;
        QJsonArray arr = node.toArray();
        const int idx = seg.mid(2).toInt();
        if (idx < 0 || idx >= arr.size()) return false;
        QJsonValue child = arr.at(idx);
        if (!applyPath(child, path, depth + 1, newValue, allowCreate)) return false;
        arr.replace(idx, child);
        node = arr;
        return true;
    }

    if (seg.startsWith(QLatin1String("N:"))) {
        if (!node.isArray()) return false;
        QJsonArray arr = node.toArray();
        int occurrence = 0;
        const QString name = segName(seg, &occurrence);
        int seen = 0, foundAt = -1;
        for (int i = 0; i < arr.size(); ++i) {
            if (arr.at(i).toObject().value(QLatin1String("Name")).toString() == name) {
                if (seen == occurrence) { foundAt = i; break; }
                ++seen;
            }
        }
        const bool isLast = (depth + 1 == path.size());
        if (foundAt < 0) {
            // Propiedad ausente en la base. No crear si allowCreate=false
            // (evita inyectar props CUE4Parse inexistentes en el JSON UAssetGUI).
            if (!allowCreate || !isLast || newValue.isUndefined()) return false;
            arr.append(newValue);
        } else if (isLast && newValue.isUndefined()) {
            arr.removeAt(foundAt);
        } else {
            QJsonValue child = arr.at(foundAt);
            if (!applyPath(child, path, depth + 1, newValue, allowCreate)) return false;
            arr.replace(foundAt, child);
        }
        node = arr;
        return true;
    }
    return false;
}

MergeEngine::Result MergeEngine::applyToTable(QJsonObject &root, const QList<ChangeItem> &items) {
    Result res;
    QJsonArray rows = dataTableRows(root);

    auto findRow = [&rows](const QString &name) {
        for (int i = 0; i < rows.size(); ++i)
            if (rows.at(i).toObject().value(QLatin1String("Name")).toString() == name)
                return i;
        return -1;
    };

    // Un export clean al que le falte más de 25% de las filas de vanilla es
    // sospechoso: podría ser una exportación CUE4Parse truncada. Bloquear sus
    // RowRemoved evita convertir ese fallo de lectura en borrados reales.
    QHash<QString, int> cleanRemovalsByMod;
    for (const ChangeItem &item : items)
        if (item.clean && item.type == ChangeItem::RowRemoved)
            ++cleanRemovalsByMod[item.modId];
    QSet<QString> suspiciousRemovalMods;
    for (auto it = cleanRemovalsByMod.cbegin(); it != cleanRemovalsByMod.cend(); ++it)
        if (!rows.isEmpty() && it.value() * 4 > rows.size())
            suspiciousRemovalMods.insert(it.key());

    // RowAdded se reconstruye desde una fila cruda de la misma tabla.
    auto writableClean = [](const ChangeItem &c) {
        if (!c.clean) return true;
        return c.type != ChangeItem::AssetReplaced;
    };

    QSet<QString> touchedRows;
    for (const ChangeItem &item : items) {
        if (!item.selected) continue;
        if (!writableClean(item)) { ++res.skipped; continue; }
        if (item.clean && item.type == ChangeItem::RowRemoved
            && suspiciousRemovalMods.contains(item.modId)) {
            ++res.skipped;
            continue;
        }
        touchedRows.insert(item.rowName);
        switch (item.type) {
        case ChangeItem::RowAdded: {
            QJsonValue row = item.newValue;
            if (item.clean) {
                QJsonValue tmpl(QJsonValue::Undefined);
                for (const QJsonValue &candidate : rows) {
                    if (candidate.toObject().value(QLatin1String("Name")).toString()
                        != item.rowName) {
                        tmpl = candidate;
                        break;
                    }
                }
                row = buildRowFromTemplate(tmpl, item.newValue, item.rowName);
                if (row.isUndefined()) {
                    ++res.skipped;
                    break;
                }
            }
            const int at = findRow(item.rowName);
            if (at >= 0) rows.replace(at, row);
            else rows.append(row);
            ++res.applied;
            break;
        }
        case ChangeItem::RowRemoved: {
            const int at = findRow(item.rowName);
            if (at >= 0) rows.removeAt(at);
            ++res.applied;
            break;
        }
        case ChangeItem::Modified: {
            const int at = findRow(item.rowName);
            if (at < 0) {
                res.error = QStringLiteral("Fila '%1' no existe en la tabla base %2")
                                .arg(item.rowName, item.tablePath);
                return res;
            }
            QJsonValue row = rows.at(at);
            if (!applyPath(row, item.propPath, 0, item.newValue, !item.clean)) {
                // Prop inexistente en la fila base de UAssetGUI: vanilla no la
                // serializa por valer el default. Intentar agregarla copiando su
                // forma de otra fila antes de darla por perdida.
                if (item.clean) {
                    if (!addPropFromTemplate(row, item.propPath, item.newValue, rows)) {
                        ++res.skipped;
                        break;
                    }
                } else {
                    res.error = QStringLiteral("No se pudo aplicar %1 en %2/%3")
                                    .arg(item.displayPath(), item.tablePath, item.rowName);
                    return res;
                }
            }
            // El diff canoniza el FName None como "": si el mod vacía un
            // NameProperty que en vanilla tenía valor, ese "" llega hasta el
            // JSON y UAssetAPI muere al escribirlo ("Cannot add an empty
            // FString to the name map"), sin uasset y sin mensaje.
            rows.replace(at, normalizeEmptyFNames(row));
            ++res.applied;
            break;
        }
        case ChangeItem::AssetReplaced:
            // Se maneja a nivel de archivos, no acá.
            break;
        }
    }
    // Registrar los FName nuevos ANTES de escribir el JSON (ver registerFNames).
    registerFNames(root, rows, touchedRows);
    root = withDataTableRows(root, rows);
    res.ok = true;
    return res;
}

} // namespace st
