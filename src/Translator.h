#pragma once

#include <QObject>
#include <QVariantMap>
#include <QVariantList>
#include <QHash>

namespace st {

// i18n en runtime para la UI QML. Expone un mapa de strings reactivo:
// al cambiar el idioma, la propiedad `s` cambia y todos los bindings QML
// (`I18n.s.<clave>`) se reevalúan. Persiste la elección con QSettings.
class Translator : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap s READ strings NOTIFY changed)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY changed)
    Q_PROPERTY(QVariantList languages READ languages CONSTANT)
    Q_PROPERTY(bool chosen READ chosen NOTIFY changed)  // false = primer arranque
public:
    explicit Translator(QObject *parent = nullptr);

    QVariantMap strings() const { return m_current; }
    QString language() const { return m_lang; }
    void setLanguage(const QString &code);
    QVariantList languages() const { return m_languages; }
    bool chosen() const { return m_chosen; }

    // Traducción puntual desde C++ (fallback a inglés y luego a la clave).
    QString t(const QString &key) const;

signals:
    void changed();

private:
    void load(const QString &code);
    QVariantMap readDict(const QString &code) const;
    QString guessFromLocale() const;

    QVariantList m_languages;
    QVariantMap m_current;
    QVariantMap m_fallback;   // inglés
    QString m_lang;
    bool m_chosen = false;
};

} // namespace st
