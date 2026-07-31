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

    // Igual, pero con varios patrones (-p es repetible). Permite exportar
    // exactamente las tablas que hacen falta en vez de barrer el juego entero.
    QMap<QString, QString> exportPackages(const QString &inputDir,
                                          const QString &outDir,
                                          const QString &mappings,
                                          const QStringList &patterns,
                                          QString *error = nullptr);

    // Lista (sin exportar) las rutas de paquete que matchean el patrón. Barato:
    // monta los contenedores y no escribe nada.
    QStringList listPackages(const QString &inputDir, const QString &mappings,
                             const QString &pattern, QString *error = nullptr);

    // Igual, con varios patrones en una sola corrida (-p es repetible): saber
    // qué tablas existen en vanilla cuesta un montaje, no una exportación.
    QStringList listPackages(const QString &inputDir, const QString &mappings,
                             const QStringList &patterns, QString *error = nullptr);

    // Patrón -p que matchea una tabla por nombre en cualquier carpeta.
    static QString patternForTable(const QString &tableName);

signals:
    void progress(const QString &message);

private:
    // idleTimeoutMs: se mata el proceso solo si deja de emitir salida ese
    // tiempo. Un tope de reloj mataba corridas lentas pero sanas.
    bool run(const QStringList &args, QString *error, int idleTimeoutMs, QString *output = nullptr);
};

} // namespace st
