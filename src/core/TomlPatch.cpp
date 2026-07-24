#include "TomlPatch.h"

namespace st {
namespace TomlPatch {

static QString stripQuotes(QString s) {
    s = s.trimmed();
    if (s.size() >= 2
        && ((s.startsWith(QLatin1Char('"')) && s.endsWith(QLatin1Char('"')))
            || (s.startsWith(QLatin1Char('\'')) && s.endsWith(QLatin1Char('\'')))))
        return s.mid(1, s.size() - 2);
    return s;
}

// Separa el valor del comentario ` # ...`, respetando '#' dentro de comillas.
static QString stripComment(const QString &raw) {
    bool inStr = false;
    QChar quote;
    for (int i = 0; i < raw.size(); ++i) {
        const QChar ch = raw.at(i);
        if (inStr) {
            if (ch == quote) inStr = false;
        } else if (ch == QLatin1Char('"') || ch == QLatin1Char('\'')) {
            inStr = true; quote = ch;
        } else if (ch == QLatin1Char('#')) {
            return raw.left(i);
        }
    }
    return raw;
}

static QJsonValue parseValue(const QString &rawIn, bool *ok) {
    *ok = true;
    const QString raw = stripComment(rawIn).trimmed();
    if (raw.isEmpty()) { *ok = false; return {}; }
    // Formas con script / multilínea de automod: no soportadas (solo literal).
    if (raw.startsWith(QLatin1String("'''")) || raw.startsWith(QLatin1String("\"\"\""))
        || raw.contains(QLatin1String("=>"))) {
        *ok = false; return {};
    }
    if (raw.startsWith(QLatin1Char('"')) || raw.startsWith(QLatin1Char('\'')))
        return QJsonValue(stripQuotes(raw));
    if (raw == QLatin1String("true")) return QJsonValue(true);
    if (raw == QLatin1String("false")) return QJsonValue(false);
    bool numOk = false;
    const double d = raw.toDouble(&numOk);
    if (numOk) return QJsonValue(d);
    // Sin comillas y no numérico/bool: tratar como string desnudo.
    return QJsonValue(raw);
}

QMap<QString, QMap<QString, QJsonValue>> parse(const QString &text) {
    QMap<QString, QMap<QString, QJsonValue>> out;
    QString row;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &lineRaw : lines) {
        QString line = lineRaw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
        if (line.startsWith(QLatin1Char('['))) {
            const int end = line.indexOf(QLatin1Char(']'));
            if (end < 0) continue;
            QString sec = line.mid(1, end - 1).trimmed();
            // Selección por regex de automod (['.*: ^Foo.*$']) no soportada en
            // import literal: se omite (sin fila destino unívoca).
            if (sec.contains(QLatin1String("=>")) || sec.contains(QLatin1String(": ^")))
                { row.clear(); continue; }
            row = stripQuotes(sec);
            continue;
        }
        if (row.isEmpty()) continue;
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0) continue;
        const QString key = line.left(eq).trimmed();
        if (key == QLatin1String("'=>'") || key == QLatin1String("=>")) continue;
        bool ok = false;
        const QJsonValue v = parseValue(line.mid(eq + 1), &ok);
        if (!ok) continue;
        out[row].insert(stripQuotes(key), v);
    }
    return out;
}

QString valueLiteral(const QJsonValue &v) {
    if (v.isBool()) return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (v.isDouble()) {
        const double d = v.toDouble();
        // Entero exacto sin decimales; si no, hasta 10 cifras significativas.
        if (d == static_cast<double>(static_cast<qint64>(d)))
            return QString::number(static_cast<qint64>(d));
        return QString::number(d, 'g', 10);
    }
    if (v.isString()) return QLatin1Char('"') + v.toString() + QLatin1Char('"');
    return QStringLiteral("\"\""); // arrays/objetos no se exportan como literal
}

} // namespace TomlPatch
} // namespace st
