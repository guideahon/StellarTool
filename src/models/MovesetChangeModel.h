#pragma once

#include <QAbstractListModel>
#include <QJsonArray>
#include <QJsonObject>

namespace st {

class MovesetChangeModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role {
        IdRole = Qt::UserRole + 1, SummaryRole, VariantRole, TableRole,
        PropertyRole, BeforeRole, AfterRole, ConflictRole, ConflictGroupRole,
        SelectedRole, SupportRole, SupportMessageRole, KindRole
    };
    Q_ENUM(Role)

    explicit MovesetChangeModel(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setCatalog(const QJsonObject &catalog);
    QJsonObject catalog() const { return m_catalog; }
    Q_INVOKABLE void toggle(const QString &id);
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE QString selectedIdsJson() const;
    Q_INVOKABLE int selectedCount() const;

private:
    QJsonArray m_changes;
    QJsonObject m_catalog;
};

} // namespace st
