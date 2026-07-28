#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

namespace st {

// Autoactualización desde GitHub Releases (guideahon/StellarTool).
//
// Flujo: al arrancar consulta la release "latest" de la API pública. Si hay una
// versión más nueva emite updateAvailable() y la UI ofrece tres salidas:
//   * install()          -> descarga el zip, lo extrae y relanza el exe nuevo.
//   * remindNextBoot()   -> no persiste nada: vuelve a preguntar en el próximo arranque.
//   * skipThisVersion()  -> persiste el tag en QSettings; no vuelve a molestar
//                           hasta que salga una versión distinta.
//
// El reemplazo real lo hace un .bat: el exe en ejecución no se puede sobrescribir,
// así que el script espera a que el proceso termine, copia sobre la carpeta de
// instalación y relanza StellarTool.exe.
class UpdateService : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY infoChanged)
    Q_PROPERTY(QString releaseNotes READ releaseNotes NOTIFY infoChanged)
    Q_PROPERTY(QString releaseUrl READ releaseUrl NOTIFY infoChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY stateChanged)
    Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool checkOnStartup READ checkOnStartup WRITE setCheckOnStartup NOTIFY settingsChanged)
public:
    explicit UpdateService(QObject *parent = nullptr);

    // "idle" | "checking" | "available" | "uptodate" | "downloading"
    // | "extracting" | "ready" | "error"
    QString state() const { return m_state; }
    QString currentVersion() const;
    QString latestVersion() const { return m_latestVersion; }
    QString releaseNotes() const { return m_releaseNotes; }
    QString releaseUrl() const { return m_releaseUrl; }
    QString errorText() const { return m_error; }
    qreal progress() const { return m_progress; }
    bool busy() const { return m_state == QLatin1String("checking")
                            || m_state == QLatin1String("downloading")
                            || m_state == QLatin1String("extracting"); }

    bool checkOnStartup() const;
    void setCheckOnStartup(bool on);

    // silent=true (arranque): no avisa "ya estás al día" ni errores de red.
    Q_INVOKABLE void checkForUpdates(bool silent = false);
    // Descarga + extrae + relanza. Pide salir de la app cuando el script arranca.
    Q_INVOKABLE void install();
    Q_INVOKABLE void cancel();
    // No persiste nada: vuelve a preguntar en el próximo arranque.
    Q_INVOKABLE void remindNextBoot();
    // Persiste el tag: no vuelve a ofrecer ESTA versión.
    Q_INVOKABLE void skipThisVersion();
    Q_INVOKABLE void openReleasePage() const;
    // Compara "0.3.13" contra "0.4.0" (acepta el prefijo "v"). >0 = a es mayor.
    static int compareVersions(const QString &a, const QString &b);

signals:
    void stateChanged();
    void infoChanged();
    void progressChanged();
    void settingsChanged();
    // La UI abre el diálogo con las tres opciones.
    void updateAvailable(const QString &version);
    // Chequeo manual sin novedades (silent=false).
    void upToDate();
    // El script de reemplazo ya arrancó: la app debe cerrarse.
    void quitRequested();

private:
    void setState(const QString &state, const QString &error = QString());
    void setProgress(qreal p);
    void startDownload();
    void applyDownloaded(const QString &zipPath);
    bool writeAndLaunchUpdater(const QString &newRoot, QString *error);
    static QString findAppRootIn(const QString &dir);

    QNetworkAccessManager *m_net;
    QNetworkReply *m_reply = nullptr;
    QString m_state = QStringLiteral("idle");
    QString m_error;
    QString m_latestVersion;
    QString m_releaseNotes;
    QString m_releaseUrl;
    QString m_assetUrl;
    QString m_assetName;
    qint64 m_assetSize = 0;
    qreal m_progress = 0.0;
    bool m_silent = false;
};

} // namespace st
