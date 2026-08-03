#include "core/LiveService.h"
#include "core/GamePaths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <QUrl>
#include <QDesktopServices>

namespace st {

namespace {

// Cada archivo del bridge: recurso embebido -> ruta relativa dentro del mod.
struct BridgeFile {
    const char *resource;
    const char *relative;
};
const BridgeFile kBridgeFiles[] = {
    {":/assets/live/StellarToolLive/Scripts/main.lua", "Scripts/main.lua"},
    {":/assets/live/StellarToolLive/enabled.txt", "enabled.txt"},
    {":/assets/live/StellarToolLive/README.txt", "README.txt"},
};

QString statusPath(const QString &bridgeDir) {
    return bridgeDir + QStringLiteral("/live_status.txt");
}
QString requestPath(const QString &bridgeDir) {
    return bridgeDir + QStringLiteral("/live_request.txt");
}

} // namespace

LiveService::LiveService(QObject *parent)
    : QObject(parent), m_timer(new QTimer(this)) {
    m_installed = isInstalledAt(bridgeDir());
    m_sinceBeat.start();
    // 400 ms: el bridge publica cada 250, asi que siempre vemos el beat nuevo
    // sin machacar el disco.
    m_timer->setInterval(400);
    connect(m_timer, &QTimer::timeout, this, &LiveService::pollStatus);
    m_timer->start();
}

LiveService::~LiveService() = default;

double LiveService::clampFov(double value) {
    if (value <= 0) return 0;
    return qBound(kFovMin, value, kFovMax);
}

double LiveService::clampMultiplier(double value) {
    return qBound(kMultMin, value, kMultMax);
}

QString LiveService::ue4ssDirFor(const QString &gameRoot) {
    if (gameRoot.isEmpty()) return {};
    return QDir::cleanPath(gameRoot + QStringLiteral("/SB/Binaries/Win64/ue4ss"));
}

QString LiveService::bridgeDirFor(const QString &gameRoot) {
    const QString ue4ss = ue4ssDirFor(gameRoot);
    if (ue4ss.isEmpty()) return {};
    return ue4ss + QStringLiteral("/Mods/StellarToolLive");
}

QByteArray LiveService::buildRequest(qint64 seq, double fov, double speed, double jump) {
    QByteArray body;
    body += "api=stellar-tool-live-v1\n";
    body += "seq=" + QByteArray::number(seq) + "\n";
    body += "fov=" + QByteArray::number(clampFov(fov), 'f', 2) + "\n";
    body += "speed=" + QByteArray::number(clampMultiplier(speed), 'f', 3) + "\n";
    body += "jump=" + QByteArray::number(clampMultiplier(jump), 'f', 3) + "\n";
    return body;
}

LiveService::Status LiveService::parseStatus(const QByteArray &body) {
    Status status;
    QHash<QString, QString> values;
    const QList<QByteArray> lines = body.split('\n');
    for (const QByteArray &raw : lines) {
        const int sep = raw.indexOf('=');
        if (sep <= 0) continue;
        const QString key = QString::fromUtf8(raw.left(sep)).trimmed();
        const QString value = QString::fromUtf8(raw.mid(sep + 1)).trimmed();
        if (!key.isEmpty()) values.insert(key, value);
    }
    // Sin el marcador de protocolo no asumimos nada: puede ser un archivo de
    // otra herramienta o una version futura del bridge.
    if (values.value(QStringLiteral("api")) != QStringLiteral("stellar-tool-live-v1"))
        return status;

    status.valid = true;
    status.beat = values.value(QStringLiteral("beat")).toULongLong();
    status.ready = values.value(QStringLiteral("ready")) == QLatin1String("1");
    bool seqOk = false;
    const qint64 seq = values.value(QStringLiteral("seq")).toLongLong(&seqOk);
    status.seq = seqOk ? seq : -1;
    status.fovProperty = values.value(QStringLiteral("fov_prop"));
    status.fovBase = values.value(QStringLiteral("fov_base")).toDouble();
    status.fovLive = values.value(QStringLiteral("fov_live")).toDouble();
    status.speedBase = values.value(QStringLiteral("speed_base")).toDouble();
    status.jumpBase = values.value(QStringLiteral("jump_base")).toDouble();
    status.message = values.value(QStringLiteral("message"));
    return status;
}

bool LiveService::publishAtomic(const QString &path, const QByteArray &body, QString *error) {
    const QString temporary = path + QStringLiteral(".tmp");
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(temporary);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = QObject::tr("No se pudo escribir %1").arg(temporary);
        return false;
    }
    file.write(body);
    file.close();
    QFile::remove(path);
    if (!QFile::rename(temporary, path)) {
        QFile::remove(temporary);
        if (error) *error = QObject::tr("No se pudo publicar %1").arg(path);
        return false;
    }
    return true;
}

bool LiveService::isInstalledAt(const QString &bridgeDir) {
    if (bridgeDir.isEmpty()) return false;
    return QFileInfo::exists(bridgeDir + QStringLiteral("/Scripts/main.lua"));
}

bool LiveService::installTo(const QString &bridgeDir, QString *error) {
    if (bridgeDir.isEmpty()) {
        if (error) *error = QObject::tr("No hay una instalación de Stellar Blade configurada.");
        return false;
    }
    for (const BridgeFile &entry : kBridgeFiles) {
        const QString target = bridgeDir + QLatin1Char('/') + QLatin1String(entry.relative);
        QFile source(QLatin1String(entry.resource));
        if (!source.open(QIODevice::ReadOnly)) {
            if (error) *error = QObject::tr("Falta el recurso embebido %1").arg(QLatin1String(entry.resource));
            return false;
        }
        const QByteArray body = source.readAll();
        source.close();
        // Reinstalar sobre una version anterior tiene que pisar, no fallar.
        if (!publishAtomic(target, body, error)) return false;
    }
    return true;
}

bool LiveService::uninstallFrom(const QString &bridgeDir, QString *error) {
    if (bridgeDir.isEmpty() || !QFileInfo::exists(bridgeDir)) return true;
    // Guarda: solo borramos si adentro esta nuestro script. Si el usuario apunto
    // el juego a otra carpeta, no nos llevamos puesto nada ajeno.
    if (!isInstalledAt(bridgeDir)) {
        if (error) *error = QObject::tr("%1 no contiene el bridge de Stellar Tool.").arg(bridgeDir);
        return false;
    }
    QDir dir(bridgeDir);
    if (!dir.removeRecursively()) {
        if (error) *error = QObject::tr("No se pudo borrar %1").arg(bridgeDir);
        return false;
    }
    return true;
}

QString LiveService::bridgeDir() const {
    return bridgeDirFor(GamePaths::gameRoot());
}

bool LiveService::ue4ssPresent() const {
    const QString ue4ss = ue4ssDirFor(GamePaths::gameRoot());
    return !ue4ss.isEmpty() && QFileInfo::exists(ue4ss);
}

void LiveService::setInstalled(bool value) {
    if (m_installed == value) return;
    m_installed = value;
    emit installedChanged();
}

bool LiveService::install() {
    QString error;
    if (!installTo(bridgeDir(), &error)) {
        emit errorOccurred(error);
        return false;
    }
    setInstalled(true);
    emit installedChanged();   // bridgeDir/ue4ssPresent pueden haber cambiado
    // Publicamos el estado actual para que el bridge arranque sincronizado.
    publishRequest();
    return true;
}

bool LiveService::uninstall() {
    QString error;
    if (!uninstallFrom(bridgeDir(), &error)) {
        emit errorOccurred(error);
        return false;
    }
    setInstalled(false);
    m_status = Status();
    m_alive = false;
    emit statusChanged();
    return true;
}

void LiveService::refresh() {
    setInstalled(isInstalledAt(bridgeDir()));
    emit installedChanged();
    pollStatus();
}

void LiveService::resetAll() {
    m_fovEnabled = false;
    m_fov = 80.0;
    m_speed = 1.0;
    m_jump = 1.0;
    emit requestChanged();
    publishRequest();
}

void LiveService::openBridgeDir() {
    const QString dir = bridgeDir();
    if (dir.isEmpty()) return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void LiveService::setFovEnabled(bool enabled) {
    if (m_fovEnabled == enabled) return;
    m_fovEnabled = enabled;
    emit requestChanged();
    publishRequest();
}

void LiveService::setFov(qreal value) {
    const double clamped = qBound(kFovMin, double(value), kFovMax);
    if (qFuzzyCompare(m_fov, clamped)) return;
    m_fov = clamped;
    emit requestChanged();
    publishRequest();
}

void LiveService::setSpeed(qreal value) {
    const double clamped = clampMultiplier(value);
    if (qFuzzyCompare(m_speed, clamped)) return;
    m_speed = clamped;
    emit requestChanged();
    publishRequest();
}

void LiveService::setJump(qreal value) {
    const double clamped = clampMultiplier(value);
    if (qFuzzyCompare(m_jump, clamped)) return;
    m_jump = clamped;
    emit requestChanged();
    publishRequest();
}

void LiveService::publishRequest() {
    const QString dir = bridgeDir();
    if (dir.isEmpty() || !isInstalledAt(dir)) return;
    ++m_seq;
    const QByteArray body = buildRequest(m_seq, m_fovEnabled ? m_fov : 0.0, m_speed, m_jump);
    QString error;
    if (!publishAtomic(requestPath(dir), body, &error))
        emit errorOccurred(error);
}

void LiveService::pollStatus() {
    const QString dir = bridgeDir();
    if (dir.isEmpty()) return;

    QFile file(statusPath(dir));
    if (!file.open(QIODevice::ReadOnly)) {
        if (m_alive) {
            m_alive = false;
            emit statusChanged();
        }
        return;
    }
    const Status status = parseStatus(file.readAll());
    file.close();
    if (!status.valid) return;

    if (status.beat != m_lastBeat) {
        m_lastBeat = status.beat;
        m_sinceBeat.restart();
    }
    // Sin beat nuevo por 2 s el juego esta cerrado o UE4SS no cargo el mod: el
    // archivo sigue en disco con los ultimos valores, por eso no alcanza con
    // que exista.
    const bool alive = m_sinceBeat.elapsed() < 2000;

    const bool changed = alive != m_alive
        || status.ready != m_status.ready
        || status.message != m_status.message
        || !qFuzzyCompare(1 + status.fovLive, 1 + m_status.fovLive)
        || status.fovProperty != m_status.fovProperty;
    m_alive = alive;
    m_status = status;
    if (changed) emit statusChanged();
}

} // namespace st
