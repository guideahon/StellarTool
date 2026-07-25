#pragma once

#include "core/ModTypes.h"
#include <QAbstractListModel>

namespace st {

class Translator;

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

    void setTranslator(const Translator *tr) { m_i18n = tr; }

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setSource(QList<ChangeItem> *items, QList<ConflictGroup> *groups);
    // Reset completo: solo cuando cambia la LISTA de conflictos (tras analizar).
    // Destruye los delegates, así que la vista vuelve al principio.
    void refresh();
    // Resolver un conflicto no cambia la lista, solo el contenido de las filas:
    // se notifica con dataChanged para no perder la posición del scroll.
    void refreshResolutions();
    int pendingCount() const;

signals:
    void changed();

private:
    QList<ChangeItem> *m_items = nullptr;
    QList<ConflictGroup> *m_groups = nullptr;
    const Translator *m_i18n = nullptr;
};

} // namespace st
