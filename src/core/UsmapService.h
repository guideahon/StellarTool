#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

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

    // Lee la tabla de enums del .usmap: nombre del enum -> valores EN ORDEN
    // (el índice es el valor numérico). Solo soporta usmap v0 sin comprimir,
    // que es el formato del archivo de la comunidad; con cualquier otro
    // devuelve vacío y quien llama sigue sin el dato.
    static QHash<QString, QStringList> loadEnums(const QString &usmapPath);

    // Lee el tipo de elemento de las propiedades ArrayProperty del mapping:
    // nombre de propiedad -> tipo UProperty (por ejemplo NameProperty).
    // Es necesario para arrays vacíos, cuyo JSON no contiene ningún elemento
    // del que UAssetAPI pueda inferir el tipo.
    static QHash<QString, QString> loadArrayTypes(const QString &usmapPath);

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
