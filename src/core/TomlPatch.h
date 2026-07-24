#pragma once

#include <QString>
#include <QMap>
#include <QJsonValue>

namespace st {

// Parser/serializador mínimo de "patches TOML" estilo automod (jpabscale):
// secciones [Fila], líneas Prop = valor  # comentario. Solo la forma literal
// (números, strings, bools); las formas con script ('=>' / '''...''') se
// ignoran. Las claves con punto (Stats.MaxHP) representan paths anidados.
namespace TomlPatch {

// row -> (propDottedPath -> valor). Vacío si el texto no tiene nada parseable.
QMap<QString, QMap<QString, QJsonValue>> parse(const QString &text);

// Serializa un valor escalar JSON a su forma TOML (número/"string"/true/false).
QString valueLiteral(const QJsonValue &v);

} // namespace TomlPatch
} // namespace st
