#pragma once

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QElapsedTimer>

class QTimer;

namespace st {

// Control en vivo del juego (fase 1: FOV, velocidad, salto).
//
// No inyecta nada ni lee memoria: instala el bridge Lua propio
// (assets/live/StellarToolLive) en la carpeta de mods de UE4SS y se comunica
// con el por archivos de texto atomicos. Si UE4SS no esta instalado o el juego
// no corre, todo queda deshabilitado y la app sigue funcionando igual.
//
// Independiente de AppController a proposito: la pagina Live no comparte estado
// con el pipeline de merge.
class LiveService : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool installed READ installed NOTIFY installedChanged)
    Q_PROPERTY(bool ue4ssPresent READ ue4ssPresent NOTIFY installedChanged)
    Q_PROPERTY(QString bridgeDir READ bridgeDir NOTIFY installedChanged)
    Q_PROPERTY(bool bridgeAlive READ bridgeAlive NOTIFY statusChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY statusChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusChanged)
    Q_PROPERTY(QString fovProperty READ fovProperty NOTIFY statusChanged)
    Q_PROPERTY(qreal fovLive READ fovLive NOTIFY statusChanged)
    Q_PROPERTY(qreal fovBase READ fovBase NOTIFY statusChanged)
    Q_PROPERTY(qreal speedBase READ speedBase NOTIFY statusChanged)
    Q_PROPERTY(qreal jumpBase READ jumpBase NOTIFY statusChanged)
    Q_PROPERTY(bool fovEnabled READ fovEnabled WRITE setFovEnabled NOTIFY requestChanged)
    Q_PROPERTY(qreal fov READ fov WRITE setFov NOTIFY requestChanged)
    Q_PROPERTY(qreal speed READ speed WRITE setSpeed NOTIFY requestChanged)
    Q_PROPERTY(qreal jump READ jump WRITE setJump NOTIFY requestChanged)
public:
    // Limites duros. El bridge Lua recorta de nuevo con los mismos valores: la
    // UI no es la unica barrera.
    static constexpr double kFovMin = 40.0;
    static constexpr double kFovMax = 170.0;
    // Arriba de 100 el juego empieza a mostrar geometria no pensada para verse.
    static constexpr double kFovSafeMax = 100.0;
    static constexpr double kMultMin = 0.1;
    static constexpr double kMultMax = 10.0;

    // Lo ultimo que publico el bridge. valid=false si el archivo no existe o
    // no es de este protocolo.
    struct Status {
        bool valid = false;
        quint64 beat = 0;
        bool ready = false;
        qint64 seq = -1;
        QString fovProperty;
        double fovBase = 0;
        double fovLive = 0;
        double speedBase = 0;
        double jumpBase = 0;
        QString message;
    };

    explicit LiveService(QObject *parent = nullptr);
    ~LiveService() override;

    // --- Logica pura (sin estado ni disco): lo que cubren los tests. ---
    static Status parseStatus(const QByteArray &body);
    // fov <= 0 se serializa como 0 = "no tocar el FOV".
    static QByteArray buildRequest(qint64 seq, double fov, double speed, double jump);
    static double clampFov(double value);
    static double clampMultiplier(double value);
    // <gameRoot>/SB/Binaries/Win64/ue4ss/Mods/StellarToolLive
    static QString bridgeDirFor(const QString &gameRoot);
    // <gameRoot>/SB/Binaries/Win64/ue4ss
    static QString ue4ssDirFor(const QString &gameRoot);
    // Copia el bridge embebido (qrc) a bridgeDir, pisando la version anterior.
    static bool installTo(const QString &bridgeDir, QString *error);
    // Borra la carpeta del bridge. Solo toca lo que instalamos nosotros.
    static bool uninstallFrom(const QString &bridgeDir, QString *error);
    static bool isInstalledAt(const QString &bridgeDir);
    // Escritura atomica (.tmp + rename): el bridge nunca lee un archivo a medio
    // escribir.
    static bool publishAtomic(const QString &path, const QByteArray &body, QString *error);

    // --- Estado de la instancia. ---
    bool installed() const { return m_installed; }
    bool ue4ssPresent() const;
    QString bridgeDir() const;
    bool bridgeAlive() const { return m_alive; }
    bool ready() const { return m_status.ready; }
    QString statusMessage() const { return m_status.message; }
    QString fovProperty() const { return m_status.fovProperty; }
    qreal fovLive() const { return m_status.fovLive; }
    qreal fovBase() const { return m_status.fovBase; }
    qreal speedBase() const { return m_status.speedBase; }
    qreal jumpBase() const { return m_status.jumpBase; }

    bool fovEnabled() const { return m_fovEnabled; }
    qreal fov() const { return m_fov; }
    qreal speed() const { return m_speed; }
    qreal jump() const { return m_jump; }
    void setFovEnabled(bool enabled);
    void setFov(qreal value);
    void setSpeed(qreal value);
    void setJump(qreal value);

    Q_INVOKABLE bool install();
    Q_INVOKABLE bool uninstall();
    Q_INVOKABLE void refresh();
    // Vuelve todo a los valores del juego y deja de pedir cambios.
    Q_INVOKABLE void resetAll();
    Q_INVOKABLE void openBridgeDir();

signals:
    void installedChanged();
    void statusChanged();
    void requestChanged();
    void errorOccurred(const QString &message);

private:
    void publishRequest();
    void pollStatus();
    void setInstalled(bool value);

    QTimer *m_timer;
    Status m_status;
    QElapsedTimer m_sinceBeat;
    quint64 m_lastBeat = 0;
    bool m_alive = false;
    bool m_installed = false;

    bool m_fovEnabled = false;
    double m_fov = 80.0;
    double m_speed = 1.0;
    double m_jump = 1.0;
    qint64 m_seq = 0;
};

} // namespace st
