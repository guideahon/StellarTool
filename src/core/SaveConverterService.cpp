#include "SaveConverterService.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>

namespace st {

QString SaveConverterService::scriptPath() {
    const QString overridePath = qEnvironmentVariable("ST_SAVE_CONVERTER");
    if (!overridePath.isEmpty()) return overridePath;
    const QString app = QCoreApplication::applicationDirPath();
    for (const QString &candidate : {
             app + QStringLiteral("/Builder/save_converter/cli.py"),
             app + QStringLiteral("/../Builder/save_converter/cli.py"),
             QStringLiteral("C:/Users/cristian/Documents/Stellar Tool/Builder/save_converter/cli.py")}) {
        if (QFileInfo::exists(candidate)) return QDir::toNativeSeparators(candidate);
    }
    return {};
}

QString SaveConverterService::pythonPath() {
    const QString script = scriptPath();
    if (!script.isEmpty()) {
        const QString builder = QFileInfo(script).absolutePath() + QStringLiteral("/..");
        const QString embedded = QDir(builder).filePath(QStringLiteral("pyembed/python.exe"));
        if (QFileInfo::exists(embedded)) return QDir::toNativeSeparators(embedded);
    }
    return QStringLiteral("python");
}

SaveConverterService::Result runConverter(const QStringList &arguments) {
    SaveConverterService::Result result;
    const QString script = SaveConverterService::scriptPath();
    if (script.isEmpty()) {
        result.message = QStringLiteral("No se encontró el módulo CNSSaveConverter integrado en Builder/save_converter.");
        return result;
    }
    QProcess process;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PYTHONUTF8"), QStringLiteral("1"));
    env.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
    process.setProcessEnvironment(env);
    process.setProgram(SaveConverterService::pythonPath());
    process.setArguments(QStringList{script} + arguments);
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start();
    if (!process.waitForStarted(30000)) {
        result.message = QStringLiteral("No se pudo iniciar Python para convertir la partida: %1")
                             .arg(process.errorString());
        return result;
    }
    process.waitForFinished(120000);
    const QString output = QString::fromUtf8(process.readAll()).trimmed();
    result.ok = process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    result.message = result.ok ? output : (output.isEmpty()
        ? QStringLiteral("El conversor terminó con código %1.").arg(process.exitCode()) : output);
    return result;
}

SaveConverterService::Result SaveConverterService::toJson(const QString &input,
                                                           const QString &output,
                                                           int indent) const {
    Result result = runConverter({QStringLiteral("tojson"), input, output,
                                  QStringLiteral("--indent"), QString::number(qMax(0, indent))});
    result.outputPath = output;
    return result;
}

SaveConverterService::Result SaveConverterService::fromJson(const QString &input,
                                                             const QString &output) const {
    Result result = runConverter({QStringLiteral("fromjson"), input, output});
    result.outputPath = output;
    return result;
}

SaveConverterService::Result SaveConverterService::fix(const QString &input) const {
    return runConverter({QStringLiteral("fix"), input});
}

} // namespace st
