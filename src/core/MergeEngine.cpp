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
static bool writableLeaf(const QJsonValue &base, const QJsonValue &nv) {
    return base.isUndefined() || sameLeafType(base, nv)
        || (base.isNull() && nv.isString());
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
        if (c.type != ChangeItem::Modified) return false; // RowAdded/Removed clean: no
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
            const int at = findRow(item.rowName);
            if (at >= 0) rows.replace(at, item.newValue);
            else rows.append(item.newValue);
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
                if (item.clean) { ++res.skipped; break; } // prop inexistente en base UAssetGUI
                res.error = QStringLiteral("No se pudo aplicar %1 en %2/%3")
                                .arg(item.displayPath(), item.tablePath, item.rowName);
                return res;
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
