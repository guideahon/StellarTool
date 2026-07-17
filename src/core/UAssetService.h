#pragma once

#include <QObject>
#include <QString>

namespace st {

// Wrapper de UAssetGUI.exe (tojson/fromjson). Sincrónico; usar desde worker thread.
class UAssetService : public QObject {
    Q_OBJECT
public:
    explicit UAssetService(QObject *parent = nullptr);

    static QString uassetGuiPath();   // tools/UAssetGUI.exe (o env ST_UASSETGUI)
    // Mappings (.usmap) para juegos con nombres mapeados (Stellar Blade los requiere
    // para obtener JSON con nombres de propiedades reales).
    static QString usmapPath();       // tools/StellarBlade.usmap (o env ST_USMAP)
    static QString mappingName();     // "StellarBlade"
    bool available() const;
    // Copia el usmap a %APPDATA%/UAssetGUI/Mappings para que fromjson resuelva
    // el mapping por nombre. Idempotente.
    static void ensureMappingInstalled();

    bool toJson(const QString &uassetPath, const QString &jsonPath, QString *error = nullptr);
    bool fromJson(const QString &jsonPath, const QString &uassetPath, QString *error = nullptr);

    static QString engineVersion();   // "VER_UE4_26"

private:
    bool run(const QStringList &args, QString *error);
};

} // namespace st
