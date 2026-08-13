#pragma once

#include <QObject>
#include <QString>

namespace st {

class SaveConverterService final : public QObject {
    Q_OBJECT
public:
    struct Result {
        bool ok = false;
        QString message;
        QString outputPath;
    };

    explicit SaveConverterService(QObject *parent = nullptr) : QObject(parent) {}

    Result toJson(const QString &input, const QString &output, int indent = 2) const;
    Result fromJson(const QString &input, const QString &output) const;
    Result fix(const QString &input) const;
    static QString scriptPath();
    static QString pythonPath();
};

} // namespace st
