#include "ModListModel.h"

#include <QFileInfo>

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
    // Qué tablas trae el mod, cambien o no respecto de vanilla: la lista de
    // cambios solo muestra las que difieren, así que sin esto no hay forma de
    // ver el contenido real del pak.
    case TableNamesRole: {
        QStringList names;
        for (const ModAsset &a : m.assets)
            if (a.kind == ModAsset::DataTable)
                names << QFileInfo(a.gamePath).completeBaseName();
        names.sort(Qt::CaseInsensitive);
        return names;
    }
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
        {TableNamesRole, "tableNames"},
    };
}

void ModListModel::setMods(const QList<ModPackage> &mods) {
    const int before = m_mods.size();
    beginResetModel();
    m_mods = mods;
    endResetModel();
    if (before != m_mods.size()) emit countChanged();
}

} // namespace st
