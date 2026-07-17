#pragma once

#include <QJsonArray>
#include <QJsonObject>

// Fixtures sintéticas con la forma del JSON de UAssetGUI/UAssetAPI:
// root.Exports[0].Table.Data = [ {Name: <row>, Value: [props]} ]
namespace fixtures {

inline QJsonObject prop(const QString &name, const QJsonValue &value,
                        const QString &type = QStringLiteral("FloatPropertyData")) {
    return QJsonObject{
        {QStringLiteral("$type"), QStringLiteral("UAssetAPI.PropertyTypes.%1, UAssetAPI").arg(type)},
        {QStringLiteral("Name"), name},
        {QStringLiteral("Value"), value},
    };
}

inline QJsonObject row(const QString &name, const QJsonArray &props) {
    return QJsonObject{
        {QStringLiteral("$type"), QStringLiteral("UAssetAPI.PropertyTypes.StructPropertyData, UAssetAPI")},
        {QStringLiteral("Name"), name},
        {QStringLiteral("Value"), props},
    };
}

inline QJsonObject table(const QJsonArray &rows) {
    return QJsonObject{
        {QStringLiteral("Exports"), QJsonArray{QJsonObject{
            {QStringLiteral("$type"), QStringLiteral("UAssetAPI.ExportTypes.DataTableExport, UAssetAPI")},
            {QStringLiteral("Table"), QJsonObject{{QStringLiteral("Data"), rows}}},
        }}},
    };
}

} // namespace fixtures
