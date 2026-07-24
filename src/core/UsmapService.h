#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;

namespace st {

// Descarga mappings (.usmap) versionados del juego desde el archivo público de
// la comunidad, para cuando un parche del juego rompe el mapping incluido.
// Fuente: TheNaeem/Unreal-Mappings-Archive (usmap crudo, sin comprimir).
// Créditos: TheNaeem (archivo) y jpabscale/automod (idea de usmaps versionados).
class UsmapService : public QObject {
    Q_OBJECT
public:
    explicit UsmapService(QObject *parent = nullptr);

    // Intenta detectar la versión del juego desde el exe (FileVersion). Vacío
    // si no hay juego o no se pudo leer. Best-effort (para prefilling en la UI).
    static QString detectGameVersion();

    // Descarga async el usmap para 'version' (ej "1.4.1"). Al terminar emite
    // finished(). Guarda en <AppData>/mappings/StellarBlade_<version>.usmap.
    void downloadForVersion(const QString &version);
    bool busy() const { return m_busy; }

signals:
    void progress(const QString &message);
    // ok=true: 'path' es el usmap descargado. ok=false: 'message' explica.
    void finished(bool ok, const QString &message, const QString &path);

private:
    QNetworkAccessManager *m_net;
    bool m_busy = false;
};

} // namespace st
