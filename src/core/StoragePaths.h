#pragma once

#include <QDir>
#include <QSettings>
#include <QStandardPaths>

namespace st::StoragePaths {

// Only disposable work and Zen conversion cache move. Baseline, settings,
// diagnostics and user output remain independent of this selection.
inline QString root() {
    const QString selected = QSettings().value(QStringLiteral("storage/workBase")).toString();
    if (selected.isEmpty())
        return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return QDir(selected).absoluteFilePath(QStringLiteral("StellarToolWork"));
}

inline QString work() { return QDir(root()).filePath(QStringLiteral("work")); }
inline QString zenCache() { return QDir(root()).filePath(QStringLiteral("cache/zenglobal")); }

} // namespace st::StoragePaths
