#pragma once

#include <QObject>
#include <QString>
#include <QMap>

namespace st {

// Wrapper de cue4parse.exe (CLI de CUE4Parse). Lee contenedores Zen/IoStore
// que retoc/UAssetGUI no pueden revertir, exportando DataTables a JSON.
// Sincrónico; usar desde worker thread.
class Cue4Service : public QObject {
    Q_OBJECT
public:
    explicit Cue4Service(QObject *parent = nullptr);

    static QString cue4Path();      // tools/cue4parse.exe (o env ST_CUE4PARSE)
    bool available() const;
    static QString gameVersion();   // "GAME_UE4_26"

    // Exporta a outDir los JSON de las DataTables que matcheen packageWildcard
    // (ej "*Table*"), leyendo desde inputDir (que debe contener el/los
    // contenedores + el global del juego para resolver tipos). mappings = usmap.
    // Devuelve mapa tablaNombre -> rutaJson. Vacío si falla.
    QMap<QString, QString> exportTables(const QString &inputDir,
                                        const QString &outDir,
                                        const QString &mappings,
                                        const QString &packageWildcard,
                                        QString *error = nullptr);

signals:
    void progress(const QString &message);

private:
    bool run(const QStringList &args, QString *error, int timeoutMs);
};

} // namespace st
