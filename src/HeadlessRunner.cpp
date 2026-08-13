#include "HeadlessRunner.h"
#include "AppController.h"
#include "core/LiveService.h"
#include "core/ReShadePresetService.h"
#include "core/SaveConverterService.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QUrl>

#include <cstdio>

namespace st {

static void out(const QString &s) {
    std::fputs(qPrintable(s + QLatin1Char('\n')), stdout);
    std::fflush(stdout);
}

QStringList HeadlessRunner::knownCommands() {
    return {QStringLiteral("analyze"),  QStringLiteral("merge"),
            QStringLiteral("cns"),      QStringLiteral("replacer"),
            QStringLiteral("build"),    QStringLiteral("baseline"),
            QStringLiteral("status"),   QStringLiteral("detect"),
            QStringLiteral("uninstall"), QStringLiteral("fixids"),
            QStringLiteral("presets"), QStringLiteral("save-to-json"),
            QStringLiteral("save-from-json"), QStringLiteral("fix-save"),
            QStringLiteral("reshade"), QStringLiteral("live")};
}

bool HeadlessRunner::isKnownCommand(const QString &command) {
    return knownCommands().contains(command);
}

bool HeadlessRunner::validate(const QString &command, const Options &o, QString *error) {
    const auto fail = [error](const QString &msg) {
        if (error) *error = msg;
        return false;
    };
    if (!isKnownCommand(command))
        return fail(QStringLiteral("Comando desconocido: %1 (usar %2)")
                        .arg(command, knownCommands().join(QStringLiteral(" | "))));

    if (command == QLatin1String("analyze") || command == QLatin1String("merge")) {
        if (o.mods.isEmpty()) return fail(QStringLiteral("Falta al menos un --mod <ruta>"));
        if (command == QLatin1String("merge") && o.outDir.isEmpty())
            return fail(QStringLiteral("merge necesita --out <dir>"));
    } else if (command == QLatin1String("cns") || command == QLatin1String("replacer")) {
        if (o.mods.isEmpty() || o.outDir.isEmpty())
            return fail(QStringLiteral("%1 necesita --mod <entrada> y --out <dir>").arg(command));
        if (command == QLatin1String("replacer") && o.replacement.isEmpty())
            return fail(QStringLiteral("replacer necesita --replace <outfit>"));
    } else if (command == QLatin1String("build")) {
        if (o.answers.isEmpty() && o.preset.isEmpty())
            return fail(QStringLiteral("build necesita --answers <json|archivo> o --preset <nombre>"));
        if (!o.answers.isEmpty() && !o.preset.isEmpty())
            return fail(QStringLiteral("build acepta --answers o --preset, no los dos"));
        if (o.outDir.isEmpty()) return fail(QStringLiteral("build necesita --out <dir>"));
    } else if (command == QLatin1String("uninstall")) {
        if (!o.uninstallPaks && !o.uninstallHelper)
            return fail(QStringLiteral("uninstall necesita --paks y/o --helper"));
    } else if (command == QLatin1String("fixids")) {
        if (o.mods.isEmpty()) return fail(QStringLiteral("fixids necesita --mod <dir>"));
    } else if (command == QLatin1String("save-to-json") || command == QLatin1String("save-from-json")) {
        if (o.input.isEmpty() || o.outDir.isEmpty())
            return fail(QStringLiteral("%1 necesita --input <archivo> y --out <archivo>").arg(command));
    } else if (command == QLatin1String("fix-save")) {
        if (o.input.isEmpty()) return fail(QStringLiteral("fix-save necesita --input <archivo.sav>"));
    } else if (command == QLatin1String("reshade")) {
        if (o.action.isEmpty()) return fail(QStringLiteral("reshade necesita --action <operacion>"));
        if (o.action == QLatin1String("save") || o.action == QLatin1String("restore")
            || o.action == QLatin1String("delete") || o.action == QLatin1String("export"))
            if (o.name.isEmpty()) return fail(QStringLiteral("reshade %1 necesita --name <nombre>").arg(o.action));
        if (o.action == QLatin1String("rename") && (o.oldName.isEmpty() || o.newName.isEmpty()))
            return fail(QStringLiteral("reshade rename necesita --old-name y --new-name"));
        if ((o.action == QLatin1String("import") || o.action == QLatin1String("export")) && o.input.isEmpty())
            return fail(QStringLiteral("reshade %1 necesita --input <archivo.ini>").arg(o.action));
    } else if (command == QLatin1String("live")) {
        if (o.action.isEmpty()) return fail(QStringLiteral("live necesita --action <status|install|uninstall|reset|set>"));
        if (o.action == QLatin1String("set") && o.fov < 0 && o.speed < 0 && o.jump < 0 && !o.fovEnabledSet)
            return fail(QStringLiteral("live set necesita --fov, --speed, --jump o --fov-enabled/--no-fov"));
    }
    return true;
}

int HeadlessRunner::exec(const QString &command, const Options &o) {
    QString error;
    if (!validate(command, o, &error)) {
        out(QStringLiteral("[ERROR] ") + error);
        return 2;
    }
    if (command == QLatin1String("build")) return runBuild(o);
    if (command == QLatin1String("baseline")) return runBaseline(o);
    if (command == QLatin1String("status")) return runStatus();
    if (command == QLatin1String("detect")) return runDetect();
    if (command == QLatin1String("uninstall")) return runUninstall(o);
    if (command == QLatin1String("fixids")) return runFixIds(o);
    if (command == QLatin1String("presets")) return runPresets();
    if (command == QLatin1String("save-to-json") || command == QLatin1String("save-from-json")
        || command == QLatin1String("fix-save")) return runSave(command, o);
    if (command == QLatin1String("reshade")) return runReShade(o);
    if (command == QLatin1String("live")) return runLive(o);
    if (command == QLatin1String("cns") || command == QLatin1String("replacer"))
        return runCns(command, o.mods.value(0), o.outDir, o.name, o.replacement, o.selection);
    return run(command, o.mods, o.outDir, o.baselineDir, o.preferMod, o.rebuildBaseline);
}

HeadlessRunner::HeadlessRunner(AppController *controller, QObject *parent)
    : QObject(parent), m_controller(controller) {
    connect(m_controller, &AppController::errorOccurred, this,
            [](const QString &m) { out(QStringLiteral("[ERROR] ") + m); });
    connect(m_controller, &AppController::statusChanged, this, [this] {
        const QString s = m_controller->statusText();
        if (!s.isEmpty()) out(QStringLiteral("  ") + s);
    });
}

bool HeadlessRunner::waitIdle() {
    QEventLoop loop;
    connect(m_controller, &AppController::busyChanged, &loop, [&] {
        if (!m_controller->busy()) loop.quit();
    });
    QTimer guard;
    guard.setInterval(15 * 60 * 1000); // tope duro de 15 min por operación
    guard.setSingleShot(true);
    connect(&guard, &QTimer::timeout, &loop, &QEventLoop::quit);
    guard.start();
    if (m_controller->busy())
        loop.exec();
    return !m_controller->busy();
}

int HeadlessRunner::run(const QString &command, const QStringList &mods,
                        const QString &outDir, const QString &baselineDir,
                        const QString &preferMod, bool rebuildBaseline) {
    if (!m_controller->toolsAvailable()) {
        out(QStringLiteral("[ERROR] ") + m_controller->toolsError());
        return 2;
    }
    if (mods.isEmpty()) {
        out(QStringLiteral("[ERROR] Falta al menos un --mod <ruta>"));
        return 2;
    }

    if (rebuildBaseline) {
        out(QStringLiteral("[INFO] Reconstruyendo baseline desde el juego (CUE4Parse)..."));
        m_controller->buildBaselineFromGame();
        if (!waitIdle()) return 3;
    }

    if (!baselineDir.isEmpty()) {
        m_controller->importBaseline(QUrl::fromLocalFile(baselineDir));
        if (!waitIdle()) return 3;
    }
    out(m_controller->hasBaseline()
            ? QStringLiteral("[INFO] Baseline presente: diffs con antes/después.")
            : QStringLiteral("[INFO] Sin baseline: modo degradado (diff solo entre mods)."));

    for (const QString &mod : mods) {
        out(QStringLiteral("[INFO] Importando: %1").arg(mod));
        m_controller->addMod(QUrl::fromLocalFile(mod));
        if (!waitIdle()) return 3;
    }
    if (m_controller->mods().size() != mods.size()) {
        out(QStringLiteral("[ERROR] Solo se importaron %1 de %2 mods.")
                .arg(m_controller->mods().size()).arg(mods.size()));
        return 4;
    }

    out(QStringLiteral("[INFO] Analizando..."));
    m_controller->analyze();
    if (!waitIdle()) return 3;
    if (!m_controller->analyzed()) return 4;

    // Reporte de cambios.
    const auto &items = m_controller->items();
    const auto &groups = m_controller->groups();
    out(QStringLiteral("== Cambios: %1 · Conflictos: %2 ==").arg(items.size()).arg(groups.size()));
    for (const auto &c : items) {
        const QString tag = c.conflictGroup >= 0 ? QStringLiteral(" [CONFLICTO]") : QString();
        out(QStringLiteral("  [%1] %2%3").arg(c.modName, c.summary(), tag));
    }

    if (command == QLatin1String("analyze"))
        return 0;

    // merge
    if (outDir.isEmpty()) {
        out(QStringLiteral("[ERROR] Falta --out <dir>"));
        return 2;
    }
    if (!groups.isEmpty()) {
        if (!preferMod.isEmpty()) {
            QString preferId;
            for (const auto &m : m_controller->mods())
                if (m.name.compare(preferMod, Qt::CaseInsensitive) == 0) preferId = m.id;
            if (preferId.isEmpty()) {
                out(QStringLiteral("[ERROR] --prefer '%1' no coincide con ningún mod cargado.").arg(preferMod));
                return 2;
            }
            m_controller->resolveAllWithMod(preferId);
            m_controller->resolveAllByPriority(); // conflictos donde preferMod no participa
            out(QStringLiteral("[INFO] Conflictos resueltos prefiriendo '%1' (resto por prioridad).").arg(preferMod));
        } else {
            m_controller->resolveAllByPriority();
            out(QStringLiteral("[INFO] Conflictos resueltos por prioridad (orden de --mod)."));
        }
        for (const auto &g : groups) {
            const auto &first = m_controller->items().at(g.itemIndexes.first());
            QString winner;
            for (int idx : g.itemIndexes)
                if (m_controller->items().at(idx).modId == g.resolvedModId)
                    winner = m_controller->items().at(idx).modName;
            out(QStringLiteral("  conflicto: %1 -> gana %2").arg(first.summary(), winner));
        }
    }

    out(QStringLiteral("[INFO] Mergeando a %1 ...").arg(outDir));
    QDir().mkpath(outDir);
    m_controller->merge(QUrl::fromLocalFile(outDir));
    if (!waitIdle()) return 3;

    out(m_controller->lastMergeResult());
    return m_controller->lastMergeOk() ? 0 : 5;
}

int HeadlessRunner::runCns(const QString &command, const QString &input,
                           const QString &outDir, const QString &name,
                           const QString &replacement, const QString &selection) {
    if (input.isEmpty() || outDir.isEmpty()) {
        out(QStringLiteral("[ERROR] CNS requiere --mod <entrada> y --out <dir>"));
        return 2;
    }
    out(QStringLiteral("[INFO] Convirtiendo %1 a %2…")
            .arg(input, command == QLatin1String("cns") ? QStringLiteral("CNS")
                                                        : QStringLiteral("replacer")));
    m_controller->convertCns(QUrl::fromLocalFile(input), QUrl::fromLocalFile(outDir),
                             name, command, replacement, selection);
    if (!waitIdle()) return 3;
    out(m_controller->lastCnsResult());
    return m_controller->lastCnsResult().contains(QLatin1String("assets convertidos")) ? 0 : 5;
}

QString HeadlessRunner::answersFromValue(const QString &value, QString *error) {
    const QString trimmed = value.trimmed();
    if (trimmed.startsWith(QLatin1Char('{'))) return trimmed;
    QFile f(trimmed);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("No se pudo leer --answers: %1").arg(trimmed);
        return {};
    }
    const QString body = QString::fromUtf8(f.readAll());
    // Un .stpreset envuelve las respuestas en {format, schemaVersion, answers}.
    const QJsonObject root = QJsonDocument::fromJson(body.toUtf8()).object();
    if (root.contains(QStringLiteral("answers")))
        return QString::fromUtf8(QJsonDocument(root.value(QStringLiteral("answers")).toObject())
                                     .toJson(QJsonDocument::Compact));
    if (root.isEmpty()) {
        if (error) *error = QStringLiteral("--answers no es JSON valido: %1").arg(trimmed);
        return {};
    }
    return body;
}

int HeadlessRunner::runBuild(const Options &o) {
    QString answers = o.answers;
    if (!o.preset.isEmpty()) {
        // Un preset guardado desde la UI: se compila exactamente lo mismo.
        const QJsonArray presets =
            QJsonDocument::fromJson(m_controller->builderPresets().toUtf8()).array();
        for (const QJsonValue &v : presets) {
            const QJsonObject p = v.toObject();
            if (p.value(QStringLiteral("name")).toString().compare(
                    o.preset, Qt::CaseInsensitive) == 0) {
                answers = QString::fromUtf8(
                    QJsonDocument(p.value(QStringLiteral("answers")).toObject())
                        .toJson(QJsonDocument::Compact));
                break;
            }
        }
        if (answers.isEmpty()) {
            out(QStringLiteral("[ERROR] No hay un preset llamado '%1' (ver: --headless presets)")
                    .arg(o.preset));
            return 2;
        }
    } else {
        QString error;
        answers = answersFromValue(o.answers, &error);
        if (answers.isEmpty()) {
            out(QStringLiteral("[ERROR] ") + error);
            return 2;
        }
    }

    QString zip;
    QObject::connect(m_controller, &AppController::builderFinished, this,
                     [&zip](const QString &path) { zip = path; });
    out(QStringLiteral("[INFO] Compilando el mod en %1 ...").arg(o.outDir));
    QDir().mkpath(o.outDir);
    m_controller->runBuilder(answers, QUrl::fromLocalFile(o.outDir),
                             o.installPaks, o.installHelper, QString());
    if (!waitIdle()) return 3;
    if (zip.isEmpty()) return 5;
    out(QStringLiteral("[OK] %1").arg(zip));
    if (o.installPaks || o.installHelper)
        out(QStringLiteral("[INFO] Instalado: ") + m_controller->installedStatus());
    return 0;
}

int HeadlessRunner::runBaseline(const Options &) {
    out(QStringLiteral("[INFO] Reconstruyendo baseline desde el juego (CUE4Parse)..."));
    m_controller->buildBaselineFromGame();
    if (!waitIdle()) return 3;
    const bool ok = m_controller->hasBaseline();
    out(ok ? QStringLiteral("[OK] Baseline lista.")
           : QStringLiteral("[ERROR] No se pudo construir la baseline."));
    return ok ? 0 : 5;
}

int HeadlessRunner::runStatus() {
    out(m_controller->installedStatus());
    return 0;
}

int HeadlessRunner::runDetect() {
    const QString game = m_controller->detectStellarBlade();
    if (game.isEmpty()) {
        out(QStringLiteral("[ERROR] No se encontro la instalacion de Stellar Blade."));
        return 5;
    }
    out(game);
    return 0;
}

int HeadlessRunner::runUninstall(const Options &o) {
    if (o.uninstallPaks) {
        m_controller->uninstallMod();
        if (!waitIdle()) return 3;
    }
    if (o.uninstallHelper) {
        m_controller->uninstallHelper();
        if (!waitIdle()) return 3;
    }
    out(m_controller->installedStatus());
    return 0;
}

int HeadlessRunner::runFixIds(const Options &o) {
    out(QStringLiteral("[INFO] Revisando IDs CNS en %1%2")
            .arg(o.mods.value(0), o.applyFixes ? QStringLiteral(" (aplicando cambios)")
                                               : QStringLiteral(" (solo reporte)")));
    m_controller->runCnsIdFixer(QUrl::fromLocalFile(o.mods.value(0)), o.applyFixes);
    if (!waitIdle()) return 3;
    const QString report = m_controller->cnsIdFixerReport();
    out(report.isEmpty() ? QStringLiteral("[ERROR] Sin reporte.") : report);
    return report.isEmpty() ? 5 : 0;
}

int HeadlessRunner::runPresets() {
    out(m_controller->builderPresets());
    return 0;
}

int HeadlessRunner::runSave(const QString &command, const Options &o) {
    SaveConverterService converter;
    SaveConverterService::Result result;
    if (command == QLatin1String("save-to-json"))
        result = converter.toJson(o.input, o.outDir, o.indent);
    else if (command == QLatin1String("save-from-json"))
        result = converter.fromJson(o.input, o.outDir);
    else
        result = converter.fix(o.input);
    out(result.ok ? QStringLiteral("[OK] ") + result.message
                  : QStringLiteral("[ERROR] ") + result.message);
    return result.ok ? 0 : 5;
}

int HeadlessRunner::runReShade(const Options &o) {
    ReShadePresetService reshade;
    reshade.refresh();
    bool ok = false;
    if (o.action == QLatin1String("list")) {
        out(reshade.presetsJson());
        return 0;
    }
    if (o.action == QLatin1String("save")) ok = reshade.saveCurrentPreset(o.name);
    else if (o.action == QLatin1String("restore")) ok = reshade.restorePreset(o.name);
    else if (o.action == QLatin1String("rename")) ok = reshade.renamePreset(o.oldName, o.newName);
    else if (o.action == QLatin1String("delete")) ok = reshade.deletePreset(o.name);
    else if (o.action == QLatin1String("import")) ok = reshade.importPreset(QUrl::fromLocalFile(o.input));
    else if (o.action == QLatin1String("export")) ok = reshade.exportPreset(o.name, QUrl::fromLocalFile(o.input));
    else {
        out(QStringLiteral("[ERROR] Acción ReShade desconocida: %1").arg(o.action));
        return 2;
    }
    out(ok ? QStringLiteral("[OK] ReShade: %1").arg(o.action)
           : QStringLiteral("[ERROR] ReShade no pudo ejecutar: %1").arg(o.action));
    if (ok) out(reshade.presetsJson());
    return ok ? 0 : 5;
}

int HeadlessRunner::runLive(const Options &o) {
    LiveService live;
    QObject::connect(&live, &LiveService::errorOccurred, this,
                     [](const QString &message) { out(QStringLiteral("[ERROR] ") + message); });
    live.refresh();
    if (o.action == QLatin1String("install")) {
        const bool ok = live.install();
        out(ok ? QStringLiteral("[OK] Bridge Live instalado.")
               : QStringLiteral("[ERROR] No se pudo instalar el bridge Live."));
        return ok ? 0 : 5;
    }
    if (o.action == QLatin1String("uninstall")) {
        const bool ok = live.uninstall();
        out(ok ? QStringLiteral("[OK] Bridge Live desinstalado.")
               : QStringLiteral("[ERROR] No se pudo desinstalar el bridge Live."));
        return ok ? 0 : 5;
    }
    if (o.action == QLatin1String("reset")) {
        live.resetAll();
        out(QStringLiteral("[OK] Valores Live restablecidos."));
        return 0;
    }
    if (o.action == QLatin1String("set")) {
        if (o.fov >= 0) live.setFov(o.fov);
        if (o.speed >= 0) live.setSpeed(o.speed);
        if (o.jump >= 0) live.setJump(o.jump);
        if (o.fovEnabledSet) live.setFovEnabled(o.fovEnabled);
    } else if (o.action != QLatin1String("status")) {
        out(QStringLiteral("[ERROR] Acción Live desconocida: %1").arg(o.action));
        return 2;
    }
    QJsonObject status{
        {QStringLiteral("installed"), live.installed()},
        {QStringLiteral("ue4ssPresent"), live.ue4ssPresent()},
        {QStringLiteral("bridgeAlive"), live.bridgeAlive()},
        {QStringLiteral("ready"), live.ready()},
        {QStringLiteral("fovEnabled"), live.fovEnabled()},
        {QStringLiteral("fov"), live.fov()},
        {QStringLiteral("speed"), live.speed()},
        {QStringLiteral("jump"), live.jump()},
        {QStringLiteral("message"), live.statusMessage()}
    };
    out(QString::fromUtf8(QJsonDocument(status).toJson(QJsonDocument::Compact)));
    return 0;
}

} // namespace st
