#include "Translator.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QSettings>

namespace st {

// Idiomas soportados: code, nombre nativo, bandera (emoji).
static const struct { const char *code; const char *name; const char *flag; } kLangs[] = {
    {"en",      "English",    "🇬🇧"},
    {"es",      "Español",    "🇪🇸"},
    {"it",      "Italiano",   "🇮🇹"},
    {"fr",      "Français",   "🇫🇷"},
    {"de",      "Deutsch",    "🇩🇪"},
    {"pt_BR",   "Português",  "🇧🇷"},
    {"ru",      "Русский",    "🇷🇺"},
    {"zh_Hans", "简体中文",     "🇨🇳"},
    {"ja",      "日本語",       "🇯🇵"},
    {"ko",      "한국어",       "🇰🇷"},
};

Translator::Translator(QObject *parent) : QObject(parent) {
    for (const auto &l : kLangs) {
        m_languages << QVariantMap{
            {QStringLiteral("code"), QString::fromLatin1(l.code)},
            {QStringLiteral("name"), QString::fromUtf8(l.name)},
            {QStringLiteral("flag"), QString::fromUtf8(l.flag)},
        };
    }
    m_fallback = readDict(QStringLiteral("en"));

    QSettings settings;
    const QString saved = settings.value(QStringLiteral("language")).toString();
    if (!saved.isEmpty()) {
        m_chosen = true;
        load(saved);
    } else {
        // Sugerir por locale del SO, sin marcar como elegido (dispara el popup).
        load(guessFromLocale());
    }
}

QString Translator::guessFromLocale() const {
    const QString ui = QLocale::system().name(); // ej "es_AR", "zh_CN"
    const QString lang = ui.section(QLatin1Char('_'), 0, 0);
    if (lang == QLatin1String("zh")) return QStringLiteral("zh_Hans");
    if (lang == QLatin1String("pt")) return QStringLiteral("pt_BR");
    for (const auto &l : kLangs)
        if (lang == QString::fromLatin1(l.code)) return lang;
    return QStringLiteral("en");
}

QVariantMap Translator::readDict(const QString &code) const {
    QFile f(QStringLiteral(":/i18n/%1.json").arg(code));
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object().toVariantMap();
}

void Translator::load(const QString &code) {
    m_lang = code;
    const QVariantMap dict = readDict(code);
    // Merge sobre el fallback inglés para tolerar claves faltantes en un idioma.
    m_current = m_fallback;
    for (auto it = dict.begin(); it != dict.end(); ++it)
        m_current.insert(it.key(), it.value());
    emit changed();
}

void Translator::setLanguage(const QString &code) {
    if (m_lang == code && m_chosen) return;
    load(code);
    m_chosen = true;
    QSettings settings;
    settings.setValue(QStringLiteral("language"), code);
    emit changed();
}

QString Translator::t(const QString &key) const {
    if (m_current.contains(key)) return m_current.value(key).toString();
    return m_fallback.value(key, key).toString();
}

} // namespace st
