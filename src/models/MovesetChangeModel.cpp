#include "MovesetChangeModel.h"

#include <QJsonDocument>
#include <QJsonObject>

namespace st {

MovesetChangeModel::MovesetChangeModel(QObject *parent) : QAbstractListModel(parent) {}

int MovesetChangeModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : m_changes.size();
}

QVariant MovesetChangeModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_changes.size()) return {};
    const QJsonObject c = m_changes.at(index.row()).toObject();
    if (role == IdRole) return c.value(QStringLiteral("id")).toString();
    if (role == SummaryRole) return c.value(QStringLiteral("summary")).toString();
    if (role == VariantRole) return c.value(QStringLiteral("variant")).toString();
    if (role == TableRole) return c.value(QStringLiteral("table")).toString();
    if (role == PropertyRole) return c.value(QStringLiteral("propertyPath")).toString();
    if (role == BeforeRole) return c.value(QStringLiteral("before")).toVariant();
    if (role == AfterRole) return c.value(QStringLiteral("after")).toVariant();
    if (role == ConflictRole) return c.value(QStringLiteral("conflict")).toBool();
    if (role == ConflictGroupRole) return c.value(QStringLiteral("conflictGroup")).toString();
    if (role == SelectedRole) return c.value(QStringLiteral("selected")).toBool();
    if (role == SupportRole) return c.value(QStringLiteral("support")).toString();
    if (role == SupportMessageRole) return c.value(QStringLiteral("supportMessage")).toString();
    if (role == KindRole) return c.value(QStringLiteral("kind")).toString();
    return {};
}

QHash<int, QByteArray> MovesetChangeModel::roleNames() const {
    return {{IdRole, "changeId"}, {SummaryRole, "summary"}, {VariantRole, "variant"},
            {TableRole, "tableName"}, {PropertyRole, "propertyPath"},
            {BeforeRole, "beforeValue"}, {AfterRole, "afterValue"},
            {ConflictRole, "conflict"}, {ConflictGroupRole, "conflictGroup"},
            {SelectedRole, "selected"}, {SupportRole, "support"},
            {SupportMessageRole, "supportMessage"}, {KindRole, "kind"}};
}

void MovesetChangeModel::setCatalog(const QJsonObject &catalog) {
    beginResetModel();
    m_catalog = catalog;
    m_changes = catalog.value(QStringLiteral("changes")).toArray();
    endResetModel();
}

void MovesetChangeModel::toggle(const QString &id) {
    int target = -1;
    for (int i = 0; i < m_changes.size(); ++i)
        if (m_changes.at(i).toObject().value(QStringLiteral("id")).toString() == id) { target = i; break; }
    if (target < 0) return;
    QJsonObject selected = m_changes.at(target).toObject();
    const bool enable = !selected.value(QStringLiteral("selected")).toBool();
    if (enable && selected.value(QStringLiteral("support")).toString() == QLatin1String("unsupported")) return;
    const QString group = selected.value(QStringLiteral("conflictGroup")).toString();
    if (enable && !group.isEmpty()) {
        for (int i = 0; i < m_changes.size(); ++i) {
            QJsonObject item = m_changes.at(i).toObject();
            if (item.value(QStringLiteral("conflictGroup")).toString() == group) {
                item.insert(QStringLiteral("selected"), false);
                m_changes.replace(i, item);
            }
        }
    }
    selected.insert(QStringLiteral("selected"), enable);
    m_changes.replace(target, selected);
    emit dataChanged(index(0), index(m_changes.size() - 1), {SelectedRole});
}

void MovesetChangeModel::clearSelection() {
    for (int i = 0; i < m_changes.size(); ++i) {
        QJsonObject item = m_changes.at(i).toObject();
        item.insert(QStringLiteral("selected"), false);
        m_changes.replace(i, item);
    }
    if (!m_changes.isEmpty()) emit dataChanged(index(0), index(m_changes.size() - 1), {SelectedRole});
}

QString MovesetChangeModel::selectedIdsJson() const {
    QJsonArray selected;
    for (const QJsonValue &v : m_changes)
        if (v.toObject().value(QStringLiteral("selected")).toBool())
            selected.append(v.toObject().value(QStringLiteral("id")));
    return QString::fromUtf8(QJsonDocument(selected).toJson(QJsonDocument::Compact));
}

int MovesetChangeModel::selectedCount() const {
    int count = 0;
    for (const QJsonValue &v : m_changes)
        count += v.toObject().value(QStringLiteral("selected")).toBool() ? 1 : 0;
    return count;
}

} // namespace st
