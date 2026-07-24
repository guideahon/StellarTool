#include "UsmapService.h"
#include "GamePaths.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#ifdef Q_OS_WIN
#include <windows.h>
#pragma comment(lib, "version.lib")
#endif

namespace st {

UsmapService::UsmapService(QObject *parent)
    : QObject(parent), m_net(new QNetworkAccessManager(this)) {}

QString UsmapService::detectGameVersion() {
#ifdef Q_OS_WIN
    const QString root = GamePaths::gameRoot();
    if (root.isEmpty()) return {};
    const QString exe = QDir::toNativeSeparators(
        root + QStringLiteral("/SB/Binaries/Win64/SB-Win64-Shipping.exe"));
    if (!QFileInfo::exists(exe)) return {};
    const std::wstring wexe = exe.toStdWString();
    DWORD dummy = 0;
    const DWORD size = GetFileVersionInfoSizeW(wexe.c_str(), &dummy);
    if (size == 0) return {};
    QByteArray buf(static_cast<int>(size), 0);
    if (!GetFileVersionInfoW(wexe.c_str(), 0, size, buf.data())) return {};
    VS_FIXEDFILEINFO *ffi = nullptr;
    UINT len = 0;
    if (!VerQueryValueW(buf.data(), L"\\", reinterpret_cast<LPVOID *>(&ffi), &len) || !ffi)
        return {};
    const int a = HIWORD(ffi->dwFileVersionMS);
    const int b = LOWORD(ffi->dwFileVersionMS);
    const int c = HIWORD(ffi->dwFileVersionLS);
    if (a == 0 && b == 0 && c == 0) return {};
    return QStringLiteral("%1.%2.%3").arg(a).arg(b).arg(c);
#else
    return {};
#endif
}

void UsmapService::downloadForVersion(const QString &version) {
    if (m_busy) return;
    const QString ver = version.trimmed();
    if (ver.isEmpty()) {
        emit finished(false, tr("Indicá la versión del juego (ej: 1.4.1)."), {});
        return;
    }
    // Archivo público de la comunidad (usmap crudo, sin comprimir).
    const QString url = QStringLiteral(
        "https://raw.githubusercontent.com/TheNaeem/Unreal-Mappings-Archive/main/"
        "Stellar%20Blade/%1/Mappings.usmap").arg(ver);

    m_busy = true;
    emit progress(tr("Descargando mappings para %1...").arg(ver));

    QNetworkRequest req{QUrl(url)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, ver] {
        reply->deleteLater();
        m_busy = false;
        if (reply->error() != QNetworkReply::NoError) {
            emit finished(false,
                tr("No se encontró un mapping para la versión %1 (o falló la descarga). "
                   "Verificá el número de versión.").arg(ver), {});
            return;
        }
        const QByteArray data = reply->readAll();
        // El .usmap arranca con el magic 0xC4 0x30 (little-endian 0x30C4).
        if (data.size() < 4 || static_cast<quint8>(data.at(0)) != 0xC4) {
            emit finished(false,
                tr("La respuesta no parece un .usmap válido para %1.").arg(ver), {});
            return;
        }
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                            + QStringLiteral("/mappings");
        QDir().mkpath(dir);
        const QString path = dir + QStringLiteral("/StellarBlade_") + ver + QStringLiteral(".usmap");
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly)) {
            emit finished(false, tr("No se pudo guardar el mapping en disco."), {});
            return;
        }
        f.write(data);
        f.close();
        emit finished(true, tr("Mapping %1 descargado.").arg(ver), path);
    });
}

} // namespace st
