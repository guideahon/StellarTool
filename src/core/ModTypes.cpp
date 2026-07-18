#include "ModTypes.h"

#include <QCryptographicHash>
#include <cmath>

namespace st {

int ModPackage::tableCount() const {
    int n = 0;
    for (const auto &a : assets) if (a.kind == ModAsset::DataTable) ++n;
    return n;
}
int ModPackage::otherCount() const {
    int n = 0;
    for (const auto &a : assets) if (a.kind == ModAsset::Other) ++n;
    return n;
}
int ModPackage::unreadableCount() const {
    int n = 0;
    for (const auto &a : assets) if (a.kind == ModAsset::Unreadable) ++n;
    return n;
}

QString ChangeItem::key() const {
    return tablePath.toLower() + QLatin1Char('|') + rowName + QLatin1Char('|') + propPath.join(QLatin1Char('/'));
}

QString ChangeItem::displayPath() const {
    QStringList parts;
    for (const QString &seg : propPath) {
        if (seg.startsWith(QLatin1String("K:"))) {
            // "Value" es envoltorio repetitivo de UAssetAPI; se omite para legibilidad.
            if (seg != QLatin1String("K:Value"))
                parts << seg.mid(2);
        } else if (seg.startsWith(QLatin1String("N:"))) {
            QString s = seg.mid(2);
            const int hash = s.lastIndexOf(QLatin1Char('#'));
            if (hash > 0 && s.mid(hash + 1) == QLatin1String("0"))
                s = s.left(hash);
            parts << s;
        } else if (seg.startsWith(QLatin1String("I:"))) {
            if (!parts.isEmpty())
                parts.last() += QStringLiteral("[%1]").arg(seg.mid(2));
            else
                parts << QStringLiteral("[%1]").arg(seg.mid(2));
        }
    }
    return parts.join(QLatin1Char('.'));
}

static QString valueToText(const QJsonValue &v) {
    if (v.isUndefined()) return QStringLiteral("—");
    if (v.isNull())      return QStringLiteral("null");
    if (v.isBool())      return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (v.isDouble()) {
        const double d = v.toDouble();
        if (d == std::floor(d) && std::abs(d) < 1e15)
            return QString::number(static_cast<qint64>(d));
        return QString::number(d, 'g', 7);
    }
    if (v.isString())    return v.toString();
    const QByteArray b = v.isArray()
        ? QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact)
        : QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact);
    if (b.size() > 60) return QString::fromUtf8(b.left(57)) + QStringLiteral("...");
    return QString::fromUtf8(b);
}

QString ChangeItem::summary() const {
    const QString table = tablePath.section(QLatin1Char('/'), -1).section(QLatin1Char('.'), 0, 0);
    switch (type) {
    case RowAdded:
        return QStringLiteral("%1 · %2 · fila nueva").arg(table, rowName);
    case RowRemoved:
        return QStringLiteral("%1 · %2 · fila eliminada").arg(table, rowName);
    case AssetReplaced:
        return QStringLiteral("%1 · asset reemplazado").arg(tablePath.section(QLatin1Char('/'), -1));
    case Modified:
    default: {
        QString s = QStringLiteral("%1 · %2 · %3: ").arg(table, rowName, displayPath());
        if (!baseValue.isUndefined()) {
            s += valueToText(baseValue) + QStringLiteral(" → ") + valueToText(newValue);
            if (baseValue.isDouble() && newValue.isDouble() && baseValue.toDouble() != 0.0) {
                const double pct = (newValue.toDouble() - baseValue.toDouble()) / std::abs(baseValue.toDouble()) * 100.0;
                if (std::abs(pct) >= 0.5)
                    s += QStringLiteral(" (%1%2%)").arg(pct > 0 ? QStringLiteral("+") : QString()).arg(QString::number(pct, 'f', 0));
            }
        } else {
            s += valueToText(newValue);
        }
        return s;
    }
    }
}

bool isDataTableJson(const QJsonObject &root) {
    return !dataTableRows(root).isEmpty() || [&root] {
        const QJsonArray exports = root.value(QLatin1String("Exports")).toArray();
        for (const QJsonValue &e : exports)
            if (e.toObject().contains(QLatin1String("Table"))) return true;
        return false;
    }();
}

QJsonArray dataTableRows(const QJsonObject &root) {
    const QJsonArray exports = root.value(QLatin1String("Exports")).toArray();
    for (const QJsonValue &e : exports) {
        const QJsonObject exp = e.toObject();
        if (exp.contains(QLatin1String("Table")))
            return exp.value(QLatin1String("Table")).toObject().value(QLatin1String("Data")).toArray();
    }
    return {};
}

QJsonObject withDataTableRows(const QJsonObject &root, const QJsonArray &rows) {
    QJsonObject out = root;
    QJsonArray exports = out.value(QLatin1String("Exports")).toArray();
    for (int i = 0; i < exports.size(); ++i) {
        QJsonObject exp = exports.at(i).toObject();
        if (exp.contains(QLatin1String("Table"))) {
            QJsonObject table = exp.value(QLatin1String("Table")).toObject();
            table.insert(QLatin1String("Data"), rows);
            exp.insert(QLatin1String("Table"), table);
            exports.replace(i, exp);
            break;
        }
    }
    out.insert(QLatin1String("Exports"), exports);
    return out;
}

// value CUE4Parse -> value UAssetAPI (recursivo).
static QJsonValue cue4Value(const QJsonValue &v);

// props object CUE4Parse -> array de {Name, Value} estilo UAssetAPI.
static QJsonArray cue4Props(const QJsonObject &obj) {
    QJsonArray out;
    for (auto it = obj.begin(); it != obj.end(); ++it)
        out.append(QJsonObject{{QStringLiteral("Name"), it.key()},
                               {QStringLiteral("Value"), cue4Value(it.value())}});
    return out;
}

static QJsonValue cue4Value(const QJsonValue &v) {
    if (v.isObject())
        return cue4Props(v.toObject());
    if (v.isArray()) {
        QJsonArray out;
        for (const QJsonValue &e : v.toArray())
            out.append(cue4Value(e));
        return out;
    }
    return v;
}

QJsonObject cue4ToDataTableDoc(const QByteArray &cue4Json) {
    const QJsonDocument doc = QJsonDocument::fromJson(cue4Json);
    // CUE4Parse exporta un array de exports; buscamos el que tenga "Rows".
    QJsonObject tableExport;
    if (doc.isArray()) {
        for (const QJsonValue &e : doc.array()) {
            if (e.isObject() && e.toObject().contains(QLatin1String("Rows"))) {
                tableExport = e.toObject();
                break;
            }
        }
    } else if (doc.isObject() && doc.object().contains(QLatin1String("Rows"))) {
        tableExport = doc.object();
    }
    if (tableExport.isEmpty()) return {};

    const QJsonObject rows = tableExport.value(QLatin1String("Rows")).toObject();
    QJsonArray data;
    for (auto it = rows.begin(); it != rows.end(); ++it) {
        data.append(QJsonObject{
            {QStringLiteral("Name"), it.key()},
            {QStringLiteral("Value"), cue4Props(it.value().toObject())},
        });
    }
    return QJsonObject{{QStringLiteral("Exports"), QJsonArray{QJsonObject{
        {QStringLiteral("$type"), QStringLiteral("UAssetAPI.ExportTypes.DataTableExport, UAssetAPI")},
        {QStringLiteral("Table"), QJsonObject{{QStringLiteral("Data"), data}}},
    }}}};
}

// ---- Normalización a forma canónica limpia ----

// ¿Es un objeto-propiedad de UAssetGUI? (tiene "Name" y "Value" + metadata)
static bool isUAssetProp(const QJsonObject &o) {
    return o.contains(QLatin1String("Name")) && o.contains(QLatin1String("Value"));
}

static QJsonValue cleanValue(const QJsonValue &v);

// Limpia un array que representa las propiedades de una fila/struct: cada
// elemento es {Name, Value, ...metadata} -> {Name, Value(limpio)}.
static QJsonArray cleanPropArray(const QJsonArray &arr) {
    QJsonArray out;
    for (const QJsonValue &e : arr) {
        if (e.isObject() && isUAssetProp(e.toObject())) {
            const QJsonObject p = e.toObject();
            out.append(QJsonObject{{QStringLiteral("Name"), p.value(QLatin1String("Name"))},
                                   {QStringLiteral("Value"), cleanValue(p.value(QLatin1String("Value")))}});
        } else {
            out.append(cleanValue(e)); // array de escalares u otros
        }
    }
    return out;
}

static QJsonValue cleanValue(const QJsonValue &v) {
    // UAssetGUI serializa el float cero como "+0"/"-0" (string). Normalizar a 0
    // para que compare igual contra el 0 numérico de CUE4Parse.
    if (v.isString()) {
        const QString s = v.toString();
        if (s == QLatin1String("+0") || s == QLatin1String("-0"))
            return QJsonValue(0);
    }
    if (v.isArray())
        return cleanPropArray(v.toArray());
    if (v.isObject()) {
        // Objeto no-propiedad (ej. referencia): conservar solo pares simples.
        const QJsonObject o = v.toObject();
        QJsonObject out;
        for (auto it = o.begin(); it != o.end(); ++it) {
            const QString &k = it.key();
            if (k == QLatin1String("$type") || k == QLatin1String("ArrayIndex")
                || k == QLatin1String("PropertyGuid") || k == QLatin1String("IsZero")
                || k == QLatin1String("DuplicationIndex") || k == QLatin1String("Name"))
                continue;
            out.insert(k, cleanValue(it.value()));
        }
        return out;
    }
    return v;
}

QJsonObject normalizeDataTableDoc(const QJsonObject &root) {
    const QJsonArray rows = dataTableRows(root);
    if (rows.isEmpty()) return root;
    QJsonArray outRows;
    for (const QJsonValue &r : rows) {
        const QJsonObject ro = r.toObject();
        outRows.append(QJsonObject{
            {QStringLiteral("Name"), ro.value(QLatin1String("Name"))},
            {QStringLiteral("Value"), cleanValue(ro.value(QLatin1String("Value")))},
        });
    }
    return withDataTableRows(root, outRows);
}

bool jsonValueEquals(const QJsonValue &a, const QJsonValue &b) {
    if (a.isDouble() && b.isDouble()) {
        const double x = a.toDouble(), y = b.toDouble();
        if (x == y) return true;
        const double scale = std::max(std::abs(x), std::abs(y));
        return std::abs(x - y) <= scale * 1e-6;
    }
    if (a.isArray() && b.isArray()) {
        const QJsonArray aa = a.toArray(), ba = b.toArray();
        if (aa.size() != ba.size()) return false;
        for (int i = 0; i < aa.size(); ++i)
            if (!jsonValueEquals(aa.at(i), ba.at(i))) return false;
        return true;
    }
    if (a.isObject() && b.isObject()) {
        const QJsonObject ao = a.toObject(), bo = b.toObject();
        if (ao.size() != bo.size()) return false;
        for (auto it = ao.begin(); it != ao.end(); ++it) {
            if (!bo.contains(it.key())) return false;
            if (!jsonValueEquals(it.value(), bo.value(it.key()))) return false;
        }
        return true;
    }
    return a == b;
}

QString shortHash(const QString &s) {
    return QString::fromLatin1(
        QCryptographicHash::hash(s.toUtf8(), QCryptographicHash::Sha1).toHex().left(8));
}

} // namespace st
