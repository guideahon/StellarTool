#pragma once

#include <QString>
#include <QMap>
#include <QJsonValue>
#include <QList>

namespace st {

// Parser/serializador mínimo de "patches TOML" estilo automod (jpabscale):
// secciones [Fila], líneas Prop = valor  # comentario. Solo la forma literal
// (números, strings, bools); las formas con script ('=>' / '''...''') se
// ignoran. Las claves con punto (Stats.MaxHP) representan paths anidados.
namespace TomlPatch {

enum class Operation { Set, Add, Multiply, Clamp, Toggle };

struct Rule {
    QString row;
    QString rowRegex;
    QString property;
    Operation operation = Operation::Set;
    QJsonValue value;
    QJsonValue minValue;
    QJsonValue maxValue;
    QJsonValue expected;
    int line = 0;
};

struct Document {
    QString table;
    QString game;
    QString gameVersion;
    QString name;
    QString author;
    QStringList dependencies;
    QStringList incompatibilities;
    QList<Rule> rules;
    QStringList errors;
    QStringList warnings;
    bool valid() const { return errors.isEmpty() && !rules.isEmpty(); }
};

// Parser declarativo compatible con los TOML literales de automod y con la
// extensión Stellar Tool: [meta], filas exactas, [row_regex] y operaciones
// set/add/multiply/clamp/toggle. No ejecuta scripts ni código embebido.
Document parseDocument(const QString &text, const QString &sourceName = {});

QString operationName(Operation op);
bool applyOperation(Operation op, const QJsonValue &oldValue,
                    const QJsonValue &value, const QJsonValue &minValue,
                    const QJsonValue &maxValue, QJsonValue *result,
                    QString *error = nullptr);

// row -> (propDottedPath -> valor). Vacío si el texto no tiene nada parseable.
QMap<QString, QMap<QString, QJsonValue>> parse(const QString &text);

// Serializa un valor escalar JSON a su forma TOML (número/"string"/true/false).
QString valueLiteral(const QJsonValue &v);

} // namespace TomlPatch
} // namespace st
