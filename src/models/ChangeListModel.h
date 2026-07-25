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
    // Reset completo: solo cuando cambia QUÉ filas se ven (analizar, filtros).
    void refresh();
    // Marcar/desmarcar o reeditar valores no cambia la lista visible: se notifica
    // con dataChanged para no perder la posición del scroll.
    void refreshSelections();

    QString filterText() const { return m_filterText; }
    void setFilterText(const QString &t);
    bool onlyConflicts() const { return m_onlyConflicts; }
    void setOnlyConflicts(bool v);
    int totalCount() const { return m_items ? m_items->size() : 0; }

    Q_INVOKABLE void setTableChecked(const QString &tableName, bool checked);
    Q_INVOKABLE void setChecked(int visibleRow, bool checked);

    // Edición manual del valor final (solo cambios Modified con valor escalar).
    Q_INVOKABLE bool canEdit(int visibleRow) const;
    Q_INVOKABLE QString valueText(int visibleRow) const;
    Q_INVOKABLE bool setEditedValue(int visibleRow, const QString &text);

    // Transformación masiva sobre los cambios visibles numéricos cuyo rowName
    // matchee 'rowRegex' (vacío = todos los visibles). op: mul|add|sub|div|set|
    // clamp|min|max. a,b = operandos (clamp usa a=min,b=max). Devuelve cuántos
    // aplicó. Habilita "hard mode ×N", "×2 drop rates", etc. sin editar 1x1.
    Q_INVOKABLE int applyTransform(const QString &op, double a, double b,
                                   const QString &rowRegex);

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
