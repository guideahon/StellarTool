#pragma once

#include "core/ModTypes.h"
#include <QAbstractListModel>

namespace st {

// Lista plana de ChangeItems con filtro integrado (texto / solo conflictos).
// El vector real vive en AppController; este modelo referencia por índices.
class ChangeListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterChanged)
    Q_PROPERTY(bool onlyConflicts READ onlyConflicts WRITE setOnlyConflicts NOTIFY filterChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY itemsChanged)
public:
    enum Roles {
        SummaryRole = Qt::UserRole + 1,
        CheckedRole,
        ConflictRole,      // id de grupo, -1 si no
        TableNameRole,     // nombre corto de tabla (para secciones)
        ModNameRole,
        RowNameRole,
        TypeRole,
        GlobalIndexRole,
    };

    explicit ChangeListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;
    QHash<int, QByteArray> roleNames() const override;

    void setItems(QList<ChangeItem> *items);
    void refresh();

    QString filterText() const { return m_filterText; }
    void setFilterText(const QString &t);
    bool onlyConflicts() const { return m_onlyConflicts; }
    void setOnlyConflicts(bool v);
    int totalCount() const { return m_items ? m_items->size() : 0; }

    Q_INVOKABLE void setTableChecked(const QString &tableName, bool checked);
    Q_INVOKABLE void setChecked(int visibleRow, bool checked);

signals:
    void filterChanged();
    void itemsChanged();
    void selectionChanged();

private:
    void rebuildVisible();
    static QString tableNameOf(const ChangeItem &c);

    QList<ChangeItem> *m_items = nullptr;
    QList<int> m_visible;
    QString m_filterText;
    bool m_onlyConflicts = false;
};

} // namespace st
