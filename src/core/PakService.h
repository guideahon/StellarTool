#pragma once

#include <QObject>
#include <QString>

namespace st {

// Wrapper de repak.exe (unpack/pack) y extracción de zips (tar.exe de Windows).
// Todas las operaciones son sincrónicas; llamarlas desde un hilo de trabajo.
class PakService : public QObject {
    Q_OBJECT
public:
    explicit PakService(QObject *parent = nullptr);

    static QString repakPath();       // tools/repak.exe junto al exe (o override por env ST_REPAK)
    bool available() const;

    bool unpack(const QString &pakPath, const QString &outDir, QString *error = nullptr);
    bool pack(const QString &contentDir, const QString &outPak, QString *error = nullptr);
    bool extractZip(const QString &zipPath, const QString &outDir, QString *error = nullptr);

private:
    bool runProcess(const QString &exe, const QStringList &args, QString *error, int timeoutMs = 300000);
};

} // namespace st
