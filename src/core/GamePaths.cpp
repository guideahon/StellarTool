#include "GamePaths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>

namespace st {

QString GamePaths::gameRoot() {
    QSettings s;
    QString root = s.value(QStringLiteral("gameRoot")).toString();
    if (root.isEmpty() || !QFileInfo::exists(root)) {
        root = detectSteam();
        if (!root.isEmpty()) s.setValue(QStringLiteral("gameRoot"), root);
    }
    return root;
}

void GamePaths::setGameRoot(const QString &root) {
    QSettings s;
    s.setValue(QStringLiteral("gameRoot"), root);
}

QString GamePaths::paksDir() {
    const QString root = gameRoot();
    if (root.isEmpty()) return {};
    return root + QStringLiteral("/SB/Content/Paks");
}

bool GamePaths::hasGame() {
    const QString paks = paksDir();
    return !paks.isEmpty() && QFileInfo::exists(paks + QStringLiteral("/global.utoc"));
}

QStringList GamePaths::globalContainerFiles() {
    const QString paks = paksDir();
    QStringList out;
    for (const QString &ext : {QStringLiteral("utoc"), QStringLiteral("ucas"), QStringLiteral("pak")}) {
        const QString f = paks + QStringLiteral("/global.") + ext;
        if (QFileInfo::exists(f)) out << f;
    }
    return out;
}

QString GamePaths::detectSteam() {
    QStringList candidates;
#ifdef Q_OS_WIN
    // Ruta de Steam desde el registro.
    QSettings steam(QStringLiteral("HKEY_CURRENT_USER\\Software\\Valve\\Steam"),
                    QSettings::NativeFormat);
    QString steamPath = steam.value(QStringLiteral("SteamPath")).toString();
    if (steamPath.isEmpty()) {
        QSettings steam64(QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\Valve\\Steam"),
                          QSettings::NativeFormat);
        steamPath = steam64.value(QStringLiteral("InstallPath")).toString();
    }
    if (!steamPath.isEmpty()) {
        candidates << steamPath + QStringLiteral("/steamapps/common/StellarBlade");
        // libraryfolders.vdf: otras unidades donde el usuario instala juegos.
        QFile vdf(steamPath + QStringLiteral("/steamapps/libraryfolders.vdf"));
        if (vdf.open(QIODevice::ReadOnly)) {
            const QString text = QString::fromUtf8(vdf.readAll());
            static const QRegularExpression re(QStringLiteral("\"path\"\\s*\"([^\"]+)\""));
            auto it = re.globalMatch(text);
            while (it.hasNext()) {
                QString p = it.next().captured(1);
                p.replace(QStringLiteral("\\\\"), QStringLiteral("/"));
                candidates << p + QStringLiteral("/steamapps/common/StellarBlade");
            }
        }
    }
    candidates << QStringLiteral("C:/Program Files (x86)/Steam/steamapps/common/StellarBlade");
#endif
    for (const QString &c : candidates) {
        if (QFileInfo::exists(c + QStringLiteral("/SB/Content/Paks/global.utoc")))
            return QDir::cleanPath(c);
    }
    return {};
}

} // namespace st
