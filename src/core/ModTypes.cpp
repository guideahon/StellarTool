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
