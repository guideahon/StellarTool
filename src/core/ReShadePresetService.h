#pragma once

#include <QObject>
#include <QString>
#include <QJsonArray>
#include <QUrl>

namespace st {

// Gestiona presets .ini de ReShade sin mezclar el flujo de mods de Unreal.
// Los presets guardados viven en AppLocalData; al restaurar se copia una
// versión administrada a Win64 y se actualiza ReShade.ini con backup previo.
class ReShadePresetService : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(QString reshadeDir READ reshadeDir NOTIFY changed)
    Q_PROPERTY(QString configPath READ configPath NOTIFY changed)
    Q_PROPERTY(QString activePresetPath READ activePresetPath NOTIFY changed)
    Q_PROPERTY(QString lastBackupPath READ lastBackupPath NOTIFY changed)
    Q_PROPERTY(QString presetsJson READ presetsJson NOTIFY changed)
public:
    explicit ReShadePresetService(QObject *parent = nullptr);

    bool available() const { return m_available; }
    QString reshadeDir() const { return m_reshadeDir; }
    QString configPath() const { return m_configPath; }
    QString activePresetPath() const { return m_activePresetPath; }
    QString lastBackupPath() const { return m_lastBackupPath; }
    QString presetsJson() const { return m_presetsJson; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE bool saveCurrentPreset(const QString &name);
    Q_INVOKABLE bool restorePreset(const QString &name);
    Q_INVOKABLE bool renamePreset(const QString &oldName, const QString &newName);
    Q_INVOKABLE bool deletePreset(const QString &name);
    Q_INVOKABLE bool importPreset(const QUrl &fileUrl);
    Q_INVOKABLE bool exportPreset(const QString &name, const QUrl &fileUrl);
    Q_INVOKABLE void openReshadeDir();

signals:
    void changed();
    void errorOccurred(const QString &message);
    void operationFinished(const QString &message);

private:
    QString storageDir() const;
    QString metadataPath() const;
    QString managedDir() const;
    QString backupDir() const;
    QString resolveActivePreset() const;
    QStringList missingShaders(const QString &presetPath) const;
    QString presetFileForName(const QString &name) const;
    QJsonArray loadMetadata() const;
    bool writeMetadata(const QJsonArray &items) const;
    bool backupCurrentConfiguration(QString *backupPath, QString *error) const;
    bool updatePresetPath(const QString &relativePath, QString *error) const;
    bool copyAtomic(const QString &source, const QString &target, QString *error) const;
    void rebuildState();
    void fail(const QString &message);

    bool m_available = false;
    QString m_reshadeDir;
    QString m_configPath;
    QString m_activePresetPath;
    QString m_lastBackupPath;
    QString m_presetsJson = QStringLiteral("[]");
};

} // namespace st
