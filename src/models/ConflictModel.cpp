#include "ConflictModel.h"
#include "../Translator.h"

#include <QJsonDocument>

namespace st {

ConflictModel::ConflictModel(QObject *parent) : QAbstractListModel(parent) {}

int ConflictModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() || !m_groups ? 0 : m_groups->size();
}

static QString valueText(const QJsonValue &v, ChangeItem::Type type, const Translator *tr) {
    auto L = [tr](const char *key, const char *en) {
        return tr ? tr->t(QString::fromLatin1(key)) : QString::fromLatin1(en);
    };
    if (type == ChangeItem::RowAdded)   return L("conflict_row_full", "full row (new/replaced)");
    if (type == ChangeItem::RowRemoved) return L("change_row_removed", "row removed");
    if (v.isUndefined()) return QStringLiteral("—");
    if (v.isDouble()) {
        const double d = v.toDouble();
        if (d == static_cast<qint64>(d)) return QString::number(static_cast<qint64>(d));
        return QString::number(d, 'g', 7);
    }
    if (v.isBool())   return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (v.isString()) return v.toString();
    const QByteArray b = v.isArray()
        ? QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact)
        : QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact);
    return b.size() > 80 ? QString::fromUtf8(b.left(77)) + QStringLiteral("...")
                         : QString::fromUtf8(b);
}

QVariant ConflictModel::data(const QModelIndex &index, int role) const {
    if (!m_groups || !m_items || !index.isValid() || index.row() >= m_groups->size()) return {};
    const ConflictGroup &g = m_groups->at(index.row());
    const ChangeItem &first = m_items->at(g.itemIndexes.first());
    switch (role) {
    case TitleRole: {
        const QString table = first.tablePath.section(QLatin1Char('/'), -1).section(QLatin1Char('.'), 0, 0);
        QString t = table + QStringLiteral(" · ") + first.rowName;
        const QString dp = first.displayPath();
        if (!dp.isEmpty()) t += QStringLiteral(" · ") + dp;
        return t;
    }
    case CandidatesRole: {
        QVariantList out;
        for (int idx : g.itemIndexes) {
            const ChangeItem &c = m_items->at(idx);
            out << QVariantMap{
                {QStringLiteral("modId"), c.modId},
                {QStringLiteral("modName"), c.modName},
                {QStringLiteral("valueText"), valueText(c.newValue, c.type, m_i18n)},
                {QStringLiteral("chosen"), g.resolvedModId == c.modId},
            };
        }
        return out;
    }
    case ResolvedModRole: return g.resolvedModId;
    case GroupIdRole: return g.id;
    case BaseTextRole: return valueText(first.baseValue, ChangeItem::Modified, m_i18n);
    }
    return {};
}

QHash<int, QByteArray> ConflictModel::roleNames() const {
    return {
        {TitleRole, "title"},
        {CandidatesRole, "candidates"},
        {ResolvedModRole, "resolvedMod"},
        {GroupIdRole, "groupId"},
        {BaseTextRole, "baseText"},
    };
}

void ConflictModel::setSource(QList<ChangeItem> *items, QList<ConflictGroup> *groups) {
    m_items = items;
    m_groups = groups;
    refresh();
}

void ConflictModel::refresh() {
    beginResetModel();
    endResetModel();
    emit changed();
}

void ConflictModel::refreshResolutions() {
    const int n = rowCount();
    if (n > 0)
        emit dataChanged(index(0, 0), index(n - 1, 0), {CandidatesRole, ResolvedModRole});
    emit changed();
}

int ConflictModel::pendingCount() const {
    if (!m_groups) return 0;
    int n = 0;
    for (const auto &g : *m_groups)
        if (g.resolvedModId.isEmpty()) ++n;
    return n;
}

} // namespace st
