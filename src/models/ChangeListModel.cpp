#include "ChangeListModel.h"

namespace st {

ChangeListModel::ChangeListModel(QObject *parent) : QAbstractListModel(parent) {}

QString ChangeListModel::tableNameOf(const ChangeItem &c) {
    return c.tablePath.section(QLatin1Char('/'), -1).section(QLatin1Char('.'), 0, 0);
}

int ChangeListModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : m_visible.size();
}

QVariant ChangeListModel::data(const QModelIndex &index, int role) const {
    if (!m_items || !index.isValid() || index.row() >= m_visible.size()) return {};
    const ChangeItem &c = m_items->at(m_visible.at(index.row()));
    switch (role) {
    case SummaryRole: return c.summary();
    case CheckedRole: return c.selected;
    case ConflictRole: return c.conflictGroup;
    case TableNameRole: return tableNameOf(c);
    case ModNameRole: return c.modName;
    case RowNameRole: return c.rowName;
    case TypeRole: return int(c.type);
    case GlobalIndexRole: return m_visible.at(index.row());
    }
    return {};
}

bool ChangeListModel::setData(const QModelIndex &index, const QVariant &value, int role) {
    if (!m_items || role != CheckedRole || !index.isValid() || index.row() >= m_visible.size())
        return false;
    (*m_items)[m_visible.at(index.row())].selected = value.toBool();
    emit dataChanged(index, index, {CheckedRole});
    emit selectionChanged();
    return true;
}

QHash<int, QByteArray> ChangeListModel::roleNames() const {
    return {
        {SummaryRole, "summary"},
        {CheckedRole, "checked"},
        {ConflictRole, "conflictId"},
        {TableNameRole, "tableName"},
        {ModNameRole, "modName"},
        {RowNameRole, "rowName"},
        {TypeRole, "changeType"},
        {GlobalIndexRole, "globalIndex"},
    };
}

void ChangeListModel::setItems(QList<ChangeItem> *items) {
    m_items = items;
    refresh();
}

void ChangeListModel::refresh() {
    beginResetModel();
    rebuildVisible();
    endResetModel();
    emit itemsChanged();
}

void ChangeListModel::setFilterText(const QString &t) {
    if (m_filterText == t) return;
    m_filterText = t;
    emit filterChanged();
    refresh();
}

void ChangeListModel::setOnlyConflicts(bool v) {
    if (m_onlyConflicts == v) return;
    m_onlyConflicts = v;
    emit filterChanged();
    refresh();
}

void ChangeListModel::rebuildVisible() {
    m_visible.clear();
    if (!m_items) return;
    for (int i = 0; i < m_items->size(); ++i) {
        const ChangeItem &c = m_items->at(i);
        if (m_onlyConflicts && c.conflictGroup < 0) continue;
        if (!m_filterText.isEmpty()
            && !c.summary().contains(m_filterText, Qt::CaseInsensitive))
            continue;
        m_visible << i;
    }
    // Orden estable: por tabla, luego fila.
    std::stable_sort(m_visible.begin(), m_visible.end(), [this](int a, int b) {
        const ChangeItem &ca = m_items->at(a), &cb = m_items->at(b);
        const int t = ca.tablePath.compare(cb.tablePath, Qt::CaseInsensitive);
        if (t != 0) return t < 0;
        return ca.rowName.compare(cb.rowName, Qt::CaseInsensitive) < 0;
    });
}

void ChangeListModel::setChecked(int visibleRow, bool checked) {
    setData(index(visibleRow, 0), checked, CheckedRole);
}

void ChangeListModel::setTableChecked(const QString &tableName, bool checked) {
    if (!m_items) return;
    for (int i = 0; i < m_items->size(); ++i) {
        if (tableNameOf(m_items->at(i)) == tableName)
            (*m_items)[i].selected = checked;
    }
    refresh();
    emit selectionChanged();
}

} // namespace st
