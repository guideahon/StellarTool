#pragma once

#include <QObject>
#include <QStringList>

namespace st {

class AppController;

// Modo CLI sin UI:
//   StellarTool --headless analyze --mod <ruta>... [--baseline <dir>]
//   StellarTool --headless merge   --mod <ruta>... --out <dir>
//                                  [--baseline <dir>] [--prefer <nombreMod>]
// Sin --prefer, los conflictos se resuelven por prioridad (orden de --mod:
// el primero gana). Salida por stdout; exit code 0 = OK.
class HeadlessRunner : public QObject {
    Q_OBJECT
public:
    explicit HeadlessRunner(AppController *controller, QObject *parent = nullptr);

    // Devuelve el exit code del proceso.
    int run(const QString &command, const QStringList &mods,
            const QString &outDir, const QString &baselineDir,
            const QString &preferMod);

private:
    bool waitIdle();   // procesa el event loop hasta que controller no esté busy
    AppController *m_controller;
};

} // namespace st
