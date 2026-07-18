#include "HeadlessRunner.h"
#include "AppController.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QTimer>
#include <QUrl>

#include <cstdio>

namespace st {

static void out(const QString &s) {
    std::fputs(qPrintable(s + QLatin1Char('\n')), stdout);
    std::fflush(stdout);
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

} // namespace st
