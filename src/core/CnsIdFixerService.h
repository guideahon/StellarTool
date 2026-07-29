#pragma once

#include <QObject>
#include <QStringList>

namespace st {

class CnsIdFixerService : public QObject {
    Q_OBJECT
public:
    struct Result {
        bool ok = false;
        int containers = 0;
        int duplicateContainers = 0;
        int fixedContainers = 0;
        int packageConflicts = 0;
        QString report;
        QString error;
    };

    explicit CnsIdFixerService(QObject *parent = nullptr);

    // Escanea .utoc recursivamente. Si fixDuplicates es true, conserva el
    // primer Container_Id y asigna IDs determinísticos a los duplicados.
    // Cada escritura crea antes <archivo>.cnsidfixer.bak.
    Result run(const QString &directory, bool fixDuplicates) const;

private:
    struct Container;
    static bool readContainer(const QString &path, Container *out, QString *error);
    static bool patchContainer(const Container &container, quint64 newId, QString *error);
    static QString idText(quint64 id);
};

} // namespace st
