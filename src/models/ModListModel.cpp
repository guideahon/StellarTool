#include "ModListModel.h"

namespace st {

ModListModel::ModListModel(QObject *parent) : QAbstractListModel(parent) {}

int ModListModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : m_mods.size();
}

QVariant ModListModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_mods.size()) return {};
    const ModPackage &m = m_mods.at(index.row());
    switch (role) {
    case NameRole: return m.name;
    case SourceRole: return m.sourcePath;
    case TableCountRole: return m.tableCount();
    case OtherCountRole: return m.otherCount();
    case UnreadableCountRole: return m.unreadableCount();
    case ModIdRole: return m.id;
    }
    return {};
}

QHash<int, QByteArray> ModListModel::roleNames() const {
    return {
        {NameRole, "name"},
        {SourceRole, "source"},
        {TableCountRole, "tableCount"},
        {OtherCountRole, "otherCount"},
        {UnreadableCountRole, "unreadableCount"},
        {ModIdRole, "modId"},
    };
}

void ModListModel::setMods(const QList<ModPackage> &mods) {
    beginResetModel();
    m_mods = mods;
    endResetModel();
}

} // namespace st
