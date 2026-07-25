#include "ChangeListModel.h"

#include <QRegularExpression>
#include <algorithm>

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
    case SummaryRole: {
        QString s = c.summaryCache.isEmpty() ? c.summary() : c.summaryCache;
        if (c.edited) s += QStringLiteral(" ✏");
        return s;
    }
    case CheckedRole: return c.selected;
    case ConflictRole: return c.conflictGroup;
    case TableNameRole: return tableNameOf(c);
    case ModNameRole:
        return c.dupCount > 0 ? c.modName + QStringLiteral(" +%1").arg(c.dupCount)
                              : c.modName;
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

void ChangeListModel::refreshSelections() {
    if (!m_visible.isEmpty())
        emit dataChanged(index(0, 0), index(m_visible.size() - 1, 0),
                         {CheckedRole, SummaryRole});
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
        if (c.dup) continue; // coincidencias colapsadas en su representante
        if (m_onlyConflicts && c.conflictGroup < 0) continue;
        if (!m_filterText.isEmpty()) {
            const QString &s = c.summaryCache.isEmpty() ? c.summary() : c.summaryCache;
            if (!s.contains(m_filterText, Qt::CaseInsensitive)) continue;
        }
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

bool ChangeListModel::canEdit(int visibleRow) const {
    if (!m_items || visibleRow < 0 || visibleRow >= m_visible.size()) return false;
    const ChangeItem &c = m_items->at(m_visible.at(visibleRow));
    if (c.type != ChangeItem::Modified) return false;
    const QJsonValue &v = c.newValue;
    return v.isDouble() || v.isBool() || v.isString();
}

QString ChangeListModel::valueText(int visibleRow) const {
    if (!canEdit(visibleRow)) return {};
    const QJsonValue &v = m_items->at(m_visible.at(visibleRow)).newValue;
    if (v.isBool()) return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (v.isDouble()) return QString::number(v.toDouble(), 'g', 10);
    return v.toString();
}

bool ChangeListModel::setEditedValue(int visibleRow, const QString &text) {
    if (!canEdit(visibleRow)) return false;
    ChangeItem &c = (*m_items)[m_visible.at(visibleRow)];
    const QJsonValue &old = c.newValue;
    QJsonValue nv;
    if (old.isBool()) {
        const QString t = text.trimmed().toLower();
        if (t != QLatin1String("true") && t != QLatin1String("false")) return false;
        nv = (t == QLatin1String("true"));
    } else if (old.isDouble()) {
        bool ok = false;
        const double d = text.trimmed().toDouble(&ok);
        if (!ok) return false;
        nv = d;
    } else {
        nv = text;
    }
    c.newValue = nv;
    c.edited = true;
    c.selected = true;
    c.summaryCache = c.summary();
    const QModelIndex mi = index(visibleRow, 0);
    emit dataChanged(mi, mi, {SummaryRole, CheckedRole});
    emit selectionChanged();
    return true;
}

int ChangeListModel::applyTransform(const QString &op, double a, double b,
                                    const QString &rowRegex) {
    if (!m_items || m_visible.isEmpty()) return 0;
    QRegularExpression re;
    if (!rowRegex.isEmpty()) {
        re = QRegularExpression(rowRegex, QRegularExpression::CaseInsensitiveOption);
        if (!re.isValid()) return -1; // regex inválido
    }
    int n = 0;
    for (int vr = 0; vr < m_visible.size(); ++vr) {
        if (!canEdit(vr)) continue;
        ChangeItem &c = (*m_items)[m_visible.at(vr)];
        if (!c.newValue.isDouble()) continue;
        if (!rowRegex.isEmpty() && !re.match(c.rowName).hasMatch()) continue;
        const double x = c.newValue.toDouble();
        double y = x;
        if (op == QLatin1String("mul")) y = x * a;
        else if (op == QLatin1String("add")) y = x + a;
        else if (op == QLatin1String("sub")) y = x - a;
        else if (op == QLatin1String("div")) { if (a == 0.0) continue; y = x / a; }
        else if (op == QLatin1String("set")) y = a;
        else if (op == QLatin1String("min")) y = std::min(x, a);
        else if (op == QLatin1String("max")) y = std::max(x, a);
        else if (op == QLatin1String("clamp")) y = std::min(std::max(x, a), b);
        else continue;
        c.newValue = QJsonValue(y);
        c.edited = true;
        c.selected = true;
        c.summaryCache = c.summary();
        ++n;
    }
    if (n > 0) {
        emit dataChanged(index(0, 0), index(m_visible.size() - 1, 0),
                         {SummaryRole, CheckedRole});
        emit selectionChanged();
    }
    return n;
}

void ChangeListModel::setTableChecked(const QString &tableName, bool checked) {
    if (!m_items) return;
    for (int i = 0; i < m_items->size(); ++i) {
        if (tableNameOf(m_items->at(i)) == tableName)
            (*m_items)[i].selected = checked;
    }
    // 'selected' no afecta qué filas se ven: no hace falta resetear el modelo.
    refreshSelections();
    emit selectionChanged();
}

} // namespace st
