#include "core/UpdateService.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>

namespace st {

namespace {

QString repoSlug() {
    const QString env = qEnvironmentVariable("ST_UPDATE_REPO");
    return env.isEmpty() ? QStringLiteral("guideahon/StellarTool") : env;
}

QString apiUrl() {
    return QStringLiteral("https://api.github.com/repos/%1/releases/latest").arg(repoSlug());
}

// Solo se descarga de GitHub: cualquier otro host en el JSON se descarta.
bool isTrustedDownloadUrl(const QUrl &url) {
    if (url.scheme() != QLatin1String("https")) return false;
    const QString host = url.host().toLower();
    return host == QLatin1String("github.com")
        || host.endsWith(QLatin1String(".github.com"))
        || host.endsWith(QLatin1String(".githubusercontent.com"));
}

QString updateWorkDir() {
    return QStandardPaths::writableLocation(QStandardPaths::TempLocation)
           + QStringLiteral("/StellarTool-update");
}

bool dirIsWritable(const QString &dir) {
    const QString probe = dir + QStringLiteral("/.st-update-probe");
    QFile f(probe);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.close();
    QFile::remove(probe);
    return true;
}

} // namespace

UpdateService::UpdateService(QObject *parent)
    : QObject(parent), m_net(new QNetworkAccessManager(this)) {}

QString UpdateService::currentVersion() const {
    return QCoreApplication::applicationVersion();
}

bool UpdateService::checkOnStartup() const {
    QSettings s;
    return s.value(QStringLiteral("update/checkOnStartup"), true).toBool();
}

void UpdateService::setCheckOnStartup(bool on) {
    QSettings s;
    if (s.value(QStringLiteral("update/checkOnStartup"), true).toBool() == on) return;
    s.setValue(QStringLiteral("update/checkOnStartup"), on);
    emit settingsChanged();
}

int UpdateService::compareVersions(const QString &a, const QString &b) {
    auto parts = [](const QString &v) {
        QString clean = v.trimmed();
        if (clean.startsWith(QLatin1Char('v'), Qt::CaseInsensitive)) clean.remove(0, 1);
        // Se corta en el primer sufijo (-alpha, +build, etc.): solo se comparan números.
        const int cut = clean.indexOf(QRegularExpression(QStringLiteral("[^0-9.]")));
        if (cut >= 0) clean = clean.left(cut);
        QList<int> out;
        const QStringList chunks = clean.split(QLatin1Char('.'), Qt::SkipEmptyParts);
        for (const QString &c : chunks) out << c.toInt();
        return out;
    };
    const QList<int> pa = parts(a);
    const QList<int> pb = parts(b);
    const int n = qMax(pa.size(), pb.size());
    for (int i = 0; i < n; ++i) {
        const int va = i < pa.size() ? pa.at(i) : 0;
        const int vb = i < pb.size() ? pb.at(i) : 0;
        if (va != vb) return va > vb ? 1 : -1;
    }
    return 0;
}

void UpdateService::setState(const QString &state, const QString &error) {
    m_state = state;
    m_error = error;
    emit stateChanged();
}

void UpdateService::setProgress(qreal p) {
    m_progress = qBound(qreal(0), p, qreal(1));
    emit progressChanged();
}

void UpdateService::checkForUpdates(bool silent) {
    if (busy()) return;
    m_silent = silent;
    setProgress(0);
    setState(QStringLiteral("checking"));

    QNetworkRequest req{QUrl(apiUrl())};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setRawHeader("User-Agent",
                     QStringLiteral("StellarTool/%1").arg(currentVersion()).toUtf8());

    QNetworkReply *reply = m_net->get(req);
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (m_reply == reply) m_reply = nullptr;
        if (reply->error() != QNetworkReply::NoError) {
            if (m_silent) setState(QStringLiteral("idle"));
            else setState(QStringLiteral("error"), reply->errorString());
            return;
        }
        const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        const QString tag = obj.value(QStringLiteral("tag_name")).toString();
        if (tag.isEmpty()) {
            if (m_silent) setState(QStringLiteral("idle"));
            else setState(QStringLiteral("error"), QStringLiteral("tag_name"));
            return;
        }
        m_latestVersion = tag;
        m_releaseNotes = obj.value(QStringLiteral("body")).toString().left(4000);
        m_releaseUrl = obj.value(QStringLiteral("html_url")).toString();
        m_assetUrl.clear();
        m_assetName.clear();
        m_assetSize = 0;
        // Se busca el zip de la app; se ignoran otros adjuntos de la release.
        const QJsonArray assets = obj.value(QStringLiteral("assets")).toArray();
        for (const QJsonValue &v : assets) {
            const QJsonObject a = v.toObject();
            const QString name = a.value(QStringLiteral("name")).toString();
            if (!name.endsWith(QLatin1String(".zip"), Qt::CaseInsensitive)) continue;
            if (!name.contains(QLatin1String("StellarTool"), Qt::CaseInsensitive)) continue;
            const QUrl url(a.value(QStringLiteral("browser_download_url")).toString());
            if (!isTrustedDownloadUrl(url)) continue;
            m_assetUrl = url.toString();
            m_assetName = name;
            m_assetSize = qint64(a.value(QStringLiteral("size")).toDouble());
            break;
        }
        emit infoChanged();

        if (compareVersions(tag, currentVersion()) <= 0) {
            setState(QStringLiteral("uptodate"));
            if (!m_silent) emit upToDate();
            return;
        }
        QSettings s;
        const QString skipped = s.value(QStringLiteral("update/skippedVersion")).toString();
        if (m_silent && !skipped.isEmpty() && compareVersions(skipped, tag) == 0) {
            setState(QStringLiteral("idle"));
            return;
        }
        setState(QStringLiteral("available"));
        emit updateAvailable(tag);
    });
}

void UpdateService::remindNextBoot() {
    // Nada persistido a propósito: el próximo arranque vuelve a ofrecerla.
    if (!busy()) setState(QStringLiteral("idle"));
}

void UpdateService::skipThisVersion() {
    if (m_latestVersion.isEmpty()) return;
    QSettings s;
    s.setValue(QStringLiteral("update/skippedVersion"), m_latestVersion);
    setState(QStringLiteral("idle"));
}

void UpdateService::openReleasePage() const {
    const QString url = m_releaseUrl.isEmpty()
        ? QStringLiteral("https://github.com/%1/releases/latest").arg(repoSlug())
        : m_releaseUrl;
    QDesktopServices::openUrl(QUrl(url));
}

void UpdateService::cancel() {
    if (m_reply) {
        QNetworkReply *r = m_reply;
        m_reply = nullptr;
        r->abort();
    }
    setProgress(0);
    setState(QStringLiteral("idle"));
}

void UpdateService::install() {
    if (busy()) return;
    if (m_assetUrl.isEmpty()) {
        setState(QStringLiteral("error"), QStringLiteral("no-asset"));
        return;
    }
    const QString appDir = QCoreApplication::applicationDirPath();
    if (!dirIsWritable(appDir)) {
        // Típico si está instalada en Program Files: mejor decirlo que fallar a medias.
        setState(QStringLiteral("error"), QStringLiteral("not-writable: %1").arg(appDir));
        return;
    }
    startDownload();
}

void UpdateService::startDownload() {
    const QString workDir = updateWorkDir();
    QDir(workDir).removeRecursively();
    QDir().mkpath(workDir);
    const QString zipPath = workDir + QLatin1Char('/')
                            + (m_assetName.isEmpty() ? QStringLiteral("update.zip") : m_assetName);

    setProgress(0);
    setState(QStringLiteral("downloading"));

    QNetworkRequest req{QUrl(m_assetUrl)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setRawHeader("User-Agent",
                     QStringLiteral("StellarTool/%1").arg(currentVersion()).toUtf8());

    QNetworkReply *reply = m_net->get(req);
    m_reply = reply;
    connect(reply, &QNetworkReply::downloadProgress, this,
            [this](qint64 got, qint64 total) {
                if (total > 0) setProgress(qreal(got) / qreal(total));
            });
    connect(reply, &QNetworkReply::finished, this, [this, reply, zipPath] {
        reply->deleteLater();
        const bool aborted = m_reply != reply;
        if (m_reply == reply) m_reply = nullptr;
        if (aborted) return;
        if (reply->error() != QNetworkReply::NoError) {
            setState(QStringLiteral("error"), reply->errorString());
            return;
        }
        const QByteArray data = reply->readAll();
        if (data.size() < 1024 || !data.startsWith("PK")) {
            setState(QStringLiteral("error"), QStringLiteral("bad-zip"));
            return;
        }
        if (m_assetSize > 0 && data.size() != m_assetSize) {
            setState(QStringLiteral("error"),
                     QStringLiteral("size-mismatch: %1 != %2").arg(data.size()).arg(m_assetSize));
            return;
        }
        QFile f(zipPath);
        if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size()) {
            setState(QStringLiteral("error"), QStringLiteral("write-failed: %1").arg(zipPath));
            return;
        }
        f.close();
        applyDownloaded(zipPath);
    });
}

QString UpdateService::findAppRootIn(const QString &dir) {
    if (QFileInfo::exists(dir + QStringLiteral("/StellarTool.exe"))) return dir;
    // El zip puede traer una carpeta contenedora; se busca poco profundo.
    QDirIterator it(dir, QStringList{QStringLiteral("StellarTool.exe")}, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString exe = it.next();
        const QString root = QFileInfo(exe).absolutePath();
        if (QDir(dir).relativeFilePath(root).count(QLatin1Char('/')) <= 3) return root;
    }
    return {};
}

void UpdateService::applyDownloaded(const QString &zipPath) {
    setState(QStringLiteral("extracting"));
    const QString outDir = updateWorkDir() + QStringLiteral("/new");
    QDir(outDir).removeRecursively();
    QDir().mkpath(outDir);

    // tar.exe (bsdtar) viene con Windows 10+ y lee zip; mismo criterio que PakService.
    QProcess tar;
    tar.start(QStringLiteral("tar"),
              {QStringLiteral("-xf"), QDir::toNativeSeparators(zipPath),
               QStringLiteral("-C"), QDir::toNativeSeparators(outDir)});
    if (!tar.waitForStarted(15000) || !tar.waitForFinished(300000)
        || tar.exitStatus() != QProcess::NormalExit || tar.exitCode() != 0) {
        setState(QStringLiteral("error"),
                 QStringLiteral("extract-failed: %1")
                     .arg(QString::fromLocal8Bit(tar.readAllStandardError()).trimmed()));
        return;
    }

    const QString newRoot = findAppRootIn(outDir);
    if (newRoot.isEmpty()) {
        setState(QStringLiteral("error"), QStringLiteral("no-exe-in-zip"));
        return;
    }

    QString error;
    if (!writeAndLaunchUpdater(newRoot, &error)) {
        setState(QStringLiteral("error"), error);
        return;
    }
    setState(QStringLiteral("ready"));
    emit quitRequested();
}

bool UpdateService::writeAndLaunchUpdater(const QString &newRoot, QString *error) {
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString exePath = QCoreApplication::applicationFilePath();
    const QString batPath = updateWorkDir() + QStringLiteral("/apply-update.bat");
    const qint64 pid = QCoreApplication::applicationPid();

    // El exe en ejecución no se puede sobrescribir: el script espera a que el
    // proceso muera, copia encima (sin /MIR: no borra archivos del usuario),
    // relanza la app y se borra a sí mismo.
    const QString bat = QStringLiteral(
        "@echo off\r\n"
        "setlocal\r\n"
        "set \"SRC=%1\"\r\n"
        "set \"DST=%2\"\r\n"
        "set \"EXE=%3\"\r\n"
        "set PID=%4\r\n"
        ":waitloop\r\n"
        "tasklist /FI \"PID eq %PID%\" 2>nul | find \"%PID%\" >nul\r\n"
        "if not errorlevel 1 (\r\n"
        "  ping -n 2 127.0.0.1 >nul\r\n"
        "  goto waitloop\r\n"
        ")\r\n"
        "robocopy \"%SRC%\" \"%DST%\" /E /NFL /NDL /NJH /NJS /NP /R:2 /W:1 >nul\r\n"
        "if errorlevel 8 (\r\n"
        "  echo Update failed: could not copy files to \"%DST%\".\r\n"
        "  pause\r\n"
        "  exit /b 1\r\n"
        ")\r\n"
        "start \"\" \"%EXE%\"\r\n"
        "endlocal\r\n"
        "(goto) 2>nul & del \"%~f0\"\r\n")
        .arg(QDir::toNativeSeparators(newRoot),
             QDir::toNativeSeparators(appDir),
             QDir::toNativeSeparators(exePath),
             QString::number(pid));

    QFile f(batPath);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = QStringLiteral("bat-write-failed: %1").arg(batPath);
        return false;
    }
    f.write(bat.toLocal8Bit());
    f.close();

    const bool started = QProcess::startDetached(
        QStringLiteral("cmd.exe"),
        {QStringLiteral("/c"), QDir::toNativeSeparators(batPath)},
        updateWorkDir());
    if (!started) {
        if (error) *error = QStringLiteral("bat-launch-failed");
        return false;
    }
    return true;
}

} // namespace st
