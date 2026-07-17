#pragma once

#include "core/ModTypes.h"
#include <QAbstractListModel>

namespace st {

// Un item por ConflictGroup; candidatos expuestos como lista de variantes.
class ConflictModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int pendingCount READ pendingCount NOTIFY changed)
public:
    enum Roles {
        TitleRole = Qt::UserRole + 1,   // "CharacterTable · EVE · MaxHP"
        CandidatesRole,                 // [{modId, modName, valueText, chosen}]
        ResolvedModRole,
        GroupIdRole,
        BaseTextRole,                   // valor baseline como texto ("—" si no hay)
    };

    explicit ConflictModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setSource(QList<ChangeItem> *items, QList<ConflictGroup> *groups);
    void refresh();
    int pendingCount() const;

signals:
    void changed();

private:
    QList<ChangeItem> *m_items = nullptr;
    QList<ConflictGroup> *m_groups = nullptr;
};

} // namespace st
