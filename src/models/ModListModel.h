#pragma once

#include "core/ModTypes.h"
#include <QAbstractListModel>

namespace st {

class ModListModel : public QAbstractListModel {
    Q_OBJECT
    // rowCount() es un método: usado en un binding QML no se reevalúa cuando
    // cambia la lista. 'count' sí notifica, así que la UI se actualiza sola.
    Q_PROPERTY(int count READ count NOTIFY countChanged)
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
    int count() const { return m_mods.size(); }

signals:
    void countChanged();

private:
    QList<ModPackage> m_mods;
};

} // namespace st
