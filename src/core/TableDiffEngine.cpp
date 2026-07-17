#include "TableDiffEngine.h"

#include <QHash>

namespace st {

static QString rowNameOf(const QJsonValue &row) {
    return row.toObject().value(QLatin1String("Name")).toString();
}

// Array "de propiedades": todos los elementos son objetos con "Name".
static bool isPropertyArray(const QJsonArray &arr) {
    if (arr.isEmpty()) return false;
    for (const QJsonValue &v : arr) {
        if (!v.isObject() || !v.toObject().contains(QLatin1String("Name")))
            return false;
    }
    return true;
}

void TableDiffEngine::diffValue(const QJsonValue &base, const QJsonValue &mod,
                                QStringList path, ChangeItem prototype,
                                QList<ChangeItem> &out) {
    if (jsonValueEquals(base, mod))
        return;

    if (base.isObject() && mod.isObject()) {
        const QJsonObject bo = base.toObject(), mo = mod.toObject();
        QStringList keys;
        for (auto it = bo.begin(); it != bo.end(); ++it) keys << it.key();
        for (auto it = mo.begin(); it != mo.end(); ++it)
            if (!bo.contains(it.key())) keys << it.key();
        for (const QString &k : keys) {
            const QJsonValue bv = bo.contains(k) ? bo.value(k) : QJsonValue(QJsonValue::Undefined);
            const QJsonValue mv = mo.contains(k) ? mo.value(k) : QJsonValue(QJsonValue::Undefined);
            QStringList p = path; p << (QStringLiteral("K:") + k);
            diffValue(bv, mv, p, prototype, out);
        }
        return;
    }

    if (base.isArray() && mod.isArray()) {
        const QJsonArray ba = base.toArray(), ma = mod.toArray();
        if (isPropertyArray(ba) && isPropertyArray(ma)) {
            // Match por Name + ocurrencia (estable ante reordenamiento).
            auto occurrenceMap = [](const QJsonArray &arr) {
                QHash<QString, QList<QJsonValue>> m;
                for (const QJsonValue &v : arr)
                    m[rowNameOf(v)] << v;
                return m;
            };
            const auto bm = occurrenceMap(ba), mm = occurrenceMap(ma);
            QStringList names;
            for (const QJsonValue &v : ba)
                if (!names.contains(rowNameOf(v))) names << rowNameOf(v);
            for (const QJsonValue &v : ma)
                if (!names.contains(rowNameOf(v))) names << rowNameOf(v);
            for (const QString &name : names) {
                const auto bl = bm.value(name), ml = mm.value(name);
                const int n = qMax(bl.size(), ml.size());
                for (int i = 0; i < n; ++i) {
                    const QJsonValue bv = i < bl.size() ? bl.at(i) : QJsonValue(QJsonValue::Undefined);
                    const QJsonValue mv = i < ml.size() ? ml.at(i) : QJsonValue(QJsonValue::Undefined);
                    QStringList p = path;
                    p << QStringLiteral("N:%1#%2").arg(name).arg(i);
                    diffValue(bv, mv, p, prototype, out);
                }
            }
            return;
        }
        // Array puro: por índice. Tamaños distintos = cambio atómico del array
        // (evita paths de índice inválidos al aplicar el merge).
        if (ba.size() != ma.size()) {
            ChangeItem c = prototype;
            c.propPath = path;
            c.baseValue = base;
            c.newValue = mod;
            out << c;
            return;
        }
        for (int i = 0; i < ba.size(); ++i) {
            QStringList p = path; p << QStringLiteral("I:%1").arg(i);
            diffValue(ba.at(i), ma.at(i), p, prototype, out);
        }
        return;
    }

    // Hoja (escalares, o tipos distintos, o presente/ausente).
    ChangeItem c = prototype;
    c.propPath = path;
    c.baseValue = base;
    c.newValue = mod;
    out << c;
}

void TableDiffEngine::diffRow(const QJsonValue &baseRow, const QJsonValue &modRow,
                              const QString &tablePath, const QString &rowName,
                              const QString &modId, const QString &modName,
                              QList<ChangeItem> &out) {
    ChangeItem proto;
    proto.modId = modId;
    proto.modName = modName;
    proto.tablePath = tablePath;
    proto.rowName = rowName;
    proto.type = ChangeItem::Modified;
    diffValue(baseRow, modRow, {}, proto, out);
}

QList<ChangeItem> TableDiffEngine::diffTable(const QJsonObject &baseRoot,
                                             const QJsonObject &modRoot,
                                             const QString &tablePath,
                                             const QString &modId,
                                             const QString &modName) {
    QList<ChangeItem> out;
    const QJsonArray modRows = dataTableRows(modRoot);
    const bool hasBaseline = !baseRoot.isEmpty();
    const QJsonArray baseRows = hasBaseline ? dataTableRows(baseRoot) : QJsonArray();

    QHash<QString, QJsonValue> baseByName;
    for (const QJsonValue &r : baseRows)
        baseByName.insert(rowNameOf(r), r);
    QHash<QString, QJsonValue> modByName;
    for (const QJsonValue &r : modRows)
        modByName.insert(rowNameOf(r), r);

    for (const QJsonValue &r : modRows) {
        const QString name = rowNameOf(r);
        if (!hasBaseline || !baseByName.contains(name)) {
            ChangeItem c;
            c.modId = modId; c.modName = modName;
            c.tablePath = tablePath; c.rowName = name;
            c.type = ChangeItem::RowAdded;
            c.newValue = r;
            out << c;
        } else {
            diffRow(baseByName.value(name), r, tablePath, name, modId, modName, out);
        }
    }
    if (hasBaseline) {
        for (const QJsonValue &r : baseRows) {
            const QString name = rowNameOf(r);
            if (!modByName.contains(name)) {
                ChangeItem c;
                c.modId = modId; c.modName = modName;
                c.tablePath = tablePath; c.rowName = name;
                c.type = ChangeItem::RowRemoved;
                c.baseValue = r;
                out << c;
            }
        }
    }
    return out;
}

QList<ConflictGroup> TableDiffEngine::findConflicts(QList<ChangeItem> &items) {
    QHash<QString, QList<int>> byKey;
    for (int i = 0; i < items.size(); ++i) {
        items[i].conflictGroup = -1;
        byKey[items.at(i).key()] << i;
    }
    QList<ConflictGroup> groups;
    for (auto it = byKey.begin(); it != byKey.end(); ++it) {
        const QList<int> &idx = it.value();
        if (idx.size() < 2) continue;
        // ¿Hay al menos dos mods distintos con valores distintos?
        bool differs = false;
        QStringList mods;
        for (int i : idx) {
            if (!mods.contains(items.at(i).modId)) mods << items.at(i).modId;
            if (!jsonValueEquals(items.at(idx.first()).newValue, items.at(i).newValue)
                || items.at(idx.first()).type != items.at(i).type)
                differs = true;
        }
        if (mods.size() < 2 || !differs) continue;
        ConflictGroup g;
        g.id = groups.size();
        g.key = it.key();
        g.itemIndexes = idx;
        for (int i : idx) items[i].conflictGroup = g.id;
        groups << g;
    }
    return groups;
}

} // namespace st
