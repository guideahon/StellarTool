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

// ¿Objeto-propiedad de UAssetGUI? ({Name, Value} + metadata de serialización)
static bool isWrapperObj(const QJsonObject &o) {
    return o.contains(QLatin1String("Name")) && o.contains(QLatin1String("Value"));
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
            out.insert(QLatin1String("Value"),
                       fillTemplate(to.value(QLatin1String("Value")), clean));
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
        if (ta.isEmpty()) return ca.isEmpty() ? tmpl : QJsonValue();
        QJsonArray out;
        for (int i = 0; i < ca.size(); ++i) {
            QJsonValue e = fillTemplate(i < ta.size() ? ta.at(i) : ta.last(), ca.at(i));
            if (e.isUndefined()) return {};
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

// Construye una fila cruda nueva a partir de otra fila de la MISMA tabla como
// plantilla: todas las filas de una DataTable comparten el struct, así que
// cualquiera sirve para obtener $type y metadata. Vacío si no se puede.
static QJsonValue buildRowFromTemplate(const QJsonValue &tmpl, const QJsonValue &cleanRow,
                                       const QString &rowName) {
    if (!tmpl.isObject() || !cleanRow.isObject()) return {};
    QJsonObject out = tmpl.toObject();
    const QJsonValue filled = fillTemplate(out.value(QLatin1String("Value")),
                                           cleanRow.toObject().value(QLatin1String("Value")));
    if (filled.isUndefined()) return {};
    out.insert(QLatin1String("Value"), filled);
    out.insert(QLatin1String("Name"), rowName);
    return out;
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
    QStringList used;
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
                if (!allowCreate && !inner.isUndefined() && !writableLeaf(inner, newValue))
                    return false;
                wrap.insert(QLatin1String("Value"), reconcileLeaf(inner, newValue));
                node = wrap;
                return true;
            }
        }
        // Para valores "clean" (CUE4Parse), solo reemplazar si es escribible
        // sobre el valor real de UAssetGUI (mismo tipo, o string sobre None):
        // evita meter un valor donde el uasset espera otro tipo, lo que rompería
        // fromjson silenciosamente. reconcileLeaf ajusta la forma (enum namespace).
        if (!allowCreate && !node.isUndefined() && !writableLeaf(node, newValue))
            return false;
        node = reconcileLeaf(node, newValue);
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

    // Un valor "clean" (leído con CUE4Parse) solo es escribible sobre el JSON
    // real de UAssetGUI si es escalar; arrays/objetos/filas completas tienen
    // representación distinta y romperían fromjson. Se saltean y se cuentan.
    auto writableClean = [](const ChangeItem &c) {
        if (!c.clean) return true;
        // Filas enteras nuevas o quitadas de un mod Zen: se pueden reconstruir
        // desde una plantilla (buildRowFromTemplate), pero el uasset resultante
        // no sobrevive el round-trip de UAssetGUI, así que no se emiten. Ver
        // ARCHITECTURE.md ("Lo que no round-tripea").
        if (c.type != ChangeItem::Modified) return false;
        // Escalares: numéricos, bool y strings (Name/Enum/Str). Los strings se
        // reconcilian contra el leaf real de UAssetGUI en applyPath (enum
        // namespace, None) + el verify round-trip descarta la tabla si no cuadra.
        // Arrays/objetos clean siguen sin soportarse (representación distinta).
        const QJsonValue &v = c.newValue;
        return v.isBool() || v.isDouble() || v.isString();
    };

    QSet<QString> touchedRows;
    for (const ChangeItem &item : items) {
        if (!item.selected) continue;
        if (!writableClean(item)) { ++res.skipped; continue; }
        touchedRows.insert(item.rowName);
        switch (item.type) {
        case ChangeItem::RowAdded: {
            const QJsonValue row = item.newValue;
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
            rows.replace(at, row);
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
