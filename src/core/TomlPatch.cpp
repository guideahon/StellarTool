#include "TomlPatch.h"

namespace st { namespace TomlPatch {

static QString stripQuotes(QString s) {
    s = s.trimmed();
    if (s.size() >= 2 && ((s.startsWith('"') && s.endsWith('"'))
        || (s.startsWith('\'') && s.endsWith('\'')))) return s.mid(1, s.size() - 2);
    return s;
}

static QString stripComment(const QString &raw) {
    bool quoted = false; QChar quote;
    for (int i = 0; i < raw.size(); ++i) {
        const QChar c = raw.at(i);
        if (quoted) { if (c == quote) quoted = false; }
        else if (c == '"' || c == '\'') { quoted = true; quote = c; }
        else if (c == '#') return raw.left(i);
    }
    return raw;
}

static QJsonValue scalar(const QString &raw, bool *ok) {
    *ok = true;
    const QString s = stripComment(raw).trimmed();
    if (s.isEmpty() || s.startsWith("'''") || s.startsWith("\"\"\"")) { *ok = false; return {}; }
    if (s.startsWith('"') || s.startsWith('\'')) return stripQuotes(s);
    if (s == "true") return true;
    if (s == "false") return false;
    bool number = false; const double d = s.toDouble(&number);
    if (number) return d;
    return s;
}

static QMap<QString, QString> inlineFields(const QString &raw, bool *ok) {
    QMap<QString, QString> out; QString s = stripComment(raw).trimmed();
    if (!s.startsWith('{') || !s.endsWith('}')) { *ok = false; return out; }
    s = s.mid(1, s.size() - 2); bool quoted = false; QChar quote; int start = 0; QStringList fields;
    for (int i = 0; i < s.size(); ++i) {
        const QChar c = s.at(i);
        if (quoted) { if (c == quote) quoted = false; }
        else if (c == '"' || c == '\'') { quoted = true; quote = c; }
        else if (c == ',') { fields << s.mid(start, i - start); start = i + 1; }
    }
    fields << s.mid(start);
    for (const QString &field : fields) { const int eq = field.indexOf('='); if (eq <= 0) { *ok = false; return {}; } out.insert(field.left(eq).trimmed(), field.mid(eq + 1).trimmed()); }
    *ok = true; return out;
}

static bool parseRuleValue(const QString &raw, Rule *rule, QString *error) {
    const QString s = stripComment(raw).trimmed();
    if (s.startsWith('{')) {
        bool fieldsOk = false; const auto fields = inlineFields(s, &fieldsOk);
        if (!fieldsOk || !fields.contains("op")) { if (error) *error = "objeto de operación inválido"; return false; }
        const QString op = stripQuotes(fields.value("op")).toLower();
        if (op == "set") rule->operation = Operation::Set; else if (op == "add") rule->operation = Operation::Add;
        else if (op == "multiply" || op == "mul") rule->operation = Operation::Multiply; else if (op == "clamp") rule->operation = Operation::Clamp;
        else if (op == "toggle") rule->operation = Operation::Toggle; else { if (error) *error = "operación desconocida: " + op; return false; }
        bool ok = false;
        if (fields.contains("value")) { rule->value = scalar(fields.value("value"), &ok); if (!ok) return false; }
        if (fields.contains("min")) { rule->minValue = scalar(fields.value("min"), &ok); if (!ok) return false; }
        if (fields.contains("max")) { rule->maxValue = scalar(fields.value("max"), &ok); if (!ok) return false; }
        if (fields.contains("expect")) { rule->expected = scalar(fields.value("expect"), &ok); if (!ok) return false; }
        if (!fields.contains("value") && rule->operation != Operation::Clamp) { if (error) *error = "falta value"; return false; }
        return true;
    }
    bool ok = false; rule->operation = Operation::Set; rule->value = scalar(s, &ok);
    if (!ok && error) *error = "valor no literal o script no soportado";
    return ok;
}

QString operationName(Operation op) {
    switch (op) { case Operation::Add: return "add"; case Operation::Multiply: return "multiply"; case Operation::Clamp: return "clamp"; case Operation::Toggle: return "toggle"; default: return "set"; }
}

bool applyOperation(Operation op, const QJsonValue &oldValue, const QJsonValue &value, const QJsonValue &minValue, const QJsonValue &maxValue, QJsonValue *result, QString *error) {
    if (!result) return false;
    if (op == Operation::Set) { *result = value; return true; }
    if (op == Operation::Toggle) { if (!oldValue.isBool()) { if (error) *error = "toggle requiere un booleano"; return false; } *result = !oldValue.toBool(); return true; }
    if (!oldValue.isDouble()) { if (error) *error = "la operación requiere un valor numérico"; return false; }
    if ((op == Operation::Add || op == Operation::Multiply) && !value.isDouble()) { if (error) *error = "value debe ser numérico"; return false; }
    double out = oldValue.toDouble();
    if (op == Operation::Add) out += value.toDouble(); else if (op == Operation::Multiply) out *= value.toDouble();
    else { if (minValue.isDouble()) out = qMax(out, minValue.toDouble()); if (maxValue.isDouble()) out = qMin(out, maxValue.toDouble()); }
    *result = out; return true;
}

Document parseDocument(const QString &text, const QString &sourceName) {
    Document doc; QString section; QString currentRow; bool regexRow = false;
    const QStringList lines = text.split('\n');
    for (int i = 0; i < lines.size(); ++i) {
        const QString line = lines.at(i).trimmed(); if (line.isEmpty() || line.startsWith('#')) continue;
        if (line.startsWith('[')) {
            const int end = line.indexOf(']'); if (end < 0) { doc.errors << QString("%1:%2: sección sin ]").arg(sourceName).arg(i + 1); continue; }
            section = stripQuotes(line.mid(1, end - 1).trimmed()); regexRow = false; currentRow.clear();
            if (section.compare("meta", Qt::CaseInsensitive) == 0) continue;
            if (section.startsWith("row_regex:", Qt::CaseInsensitive)) { regexRow = true; currentRow = section.mid(10).trimmed(); }
            else if (section.startsWith("regex:", Qt::CaseInsensitive)) { regexRow = true; currentRow = section.mid(6).trimmed(); }
            else if (section.contains(": ^")) { regexRow = true; currentRow = section.section(':', 1).trimmed(); }
            else currentRow = section;
            continue;
        }
        const int eq = line.indexOf('='); if (eq <= 0) { doc.warnings << QString("%1:%2: línea ignorada").arg(sourceName).arg(i + 1); continue; }
        const QString key = stripQuotes(line.left(eq).trimmed()); const QString value = line.mid(eq + 1).trimmed();
        if (section.compare("meta", Qt::CaseInsensitive) == 0) {
            bool ok = false; const QJsonValue v = scalar(value, &ok); if (!ok) { doc.errors << QString("%1:%2: metadata inválida").arg(sourceName).arg(i + 1); continue; }
            if (key == "table") doc.table = v.toString(); else if (key == "game") doc.game = v.toString(); else if (key == "game_version" || key == "requires_game_version") doc.gameVersion = v.toString(); else if (key == "name") doc.name = v.toString(); else if (key == "author") doc.author = v.toString();
            continue;
        }
        if (currentRow.isEmpty()) { doc.warnings << QString("%1:%2: propiedad fuera de una fila").arg(sourceName).arg(i + 1); continue; }
        Rule rule; rule.row = regexRow ? QString() : stripQuotes(currentRow); rule.rowRegex = regexRow ? stripQuotes(currentRow) : QString(); rule.property = key; rule.line = i + 1;
        QString err; if (!parseRuleValue(value, &rule, &err)) { doc.errors << QString("%1:%2: %3").arg(sourceName).arg(i + 1).arg(err); continue; }
        doc.rules << rule;
    }
    if (doc.rules.isEmpty() && doc.errors.isEmpty()) doc.errors << QString("%1: no hay reglas").arg(sourceName);
    return doc;
}

QMap<QString, QMap<QString, QJsonValue>> parse(const QString &text) {
    QMap<QString, QMap<QString, QJsonValue>> out; const Document doc = parseDocument(text);
    for (const Rule &r : doc.rules) if (!r.row.isEmpty() && r.operation == Operation::Set) out[r.row].insert(r.property, r.value);
    return out;
}

QString valueLiteral(const QJsonValue &v) {
    if (v.isBool()) return v.toBool() ? "true" : "false";
    if (v.isDouble()) return QString::number(v.toDouble(), 'g', 15);
    if (v.isString()) return '"' + v.toString().replace('"', "\\\"") + '"';
    return "\"\"";
}

}} // namespace st::TomlPatch
