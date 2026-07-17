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
    bool available() const;

    bool toJson(const QString &uassetPath, const QString &jsonPath, QString *error = nullptr);
    bool fromJson(const QString &jsonPath, const QString &uassetPath, QString *error = nullptr);

    static QString engineVersion();   // "VER_UE4_26"

private:
    bool run(const QStringList &args, QString *error);
};

} // namespace st
