#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace st {

class MovesetService {
public:
    struct Variant {
        QString id;
        QString family;
        QString tier;
        bool aggro = false;
        QString sourceDir;
        QStringList files;
    };

    static QList<Variant> scan(const QString &root, QString *error = nullptr);
    static QString describe(const Variant &variant);
    static bool copyVariant(const Variant &variant, const QString &outDir, QString *error = nullptr);
    static bool installVariant(const Variant &variant, const QString &gameRoot, QString *error = nullptr);
    static bool uninstall(QString *error = nullptr);
    static QString installedStatus();
};

} // namespace st
