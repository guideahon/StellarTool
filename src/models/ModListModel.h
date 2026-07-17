#pragma once

#include "core/ModTypes.h"
#include <QAbstractListModel>

namespace st {

class ModListModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        SourceRole,
        TableCountRole,
        OtherCountRole,
        UnreadableCountRole,
        ModIdRole,
    };

    explicit ModListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setMods(const QList<ModPackage> &mods);
    const QList<ModPackage> &mods() const { return m_mods; }

private:
    QList<ModPackage> m_mods;
};

} // namespace st
