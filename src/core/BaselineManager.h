#pragma once

#include "ModTypes.h"
#include <QObject>

namespace st {

// Cache de tablas vanilla en %LOCALAPPDATA%/StellarTool/baseline/.
// Sin baseline la app funciona en modo degradado (diff solo entre mods).
class BaselineManager : public QObject {
    Q_OBJECT
public:
    explicit BaselineManager(QObject *parent = nullptr);

    QString baselineDir() const;
    bool hasBaseline() const;

    // JSON de la tabla vanilla para un gamePath; objeto vacío si no está.
    QJsonObject tableFor(const QString &gamePath) const;

    // Importa un dir con JSONs de UAssetGUI (dump manual, ej. vía FModel) a la cache.
    bool importFromDir(const QString &dir, QString *error, int *imported);

private:
    QString keyFor(const QString &gamePath) const;
};

} // namespace st
