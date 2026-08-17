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

namespace {

// Lector secuencial con chequeo de límites: cualquier lectura fuera del buffer
// marca 'bad' y devuelve 0, así un usmap raro no lee memoria ajena.
class UsmapReader {
public:
    explicit UsmapReader(const QByteArray &data) : m_data(data) {}
    bool bad() const { return m_bad; }
    quint8 u8() { return read<quint8>(); }
    quint16 u16() { return read<quint16>(); }
    quint32 u32() { return read<quint32>(); }
    QString str() {
        const int len = u8();
        if (m_bad || m_pos + len > m_data.size()) { m_bad = true; return {}; }
        const QString s = QString::fromUtf8(m_data.constData() + m_pos, len);
        m_pos += len;
        return s;
    }

private:
    template <typename T> T read() {
        if (m_bad || m_pos + int(sizeof(T)) > m_data.size()) { m_bad = true; return 0; }
        T v = 0;
        memcpy(&v, m_data.constData() + m_pos, sizeof(T)); // usmap es little-endian
        m_pos += int(sizeof(T));
        return v;
    }
    QByteArray m_data;
    int m_pos = 0;
    bool m_bad = false;
};

} // namespace

QHash<QString, QStringList> UsmapService::loadEnums(const QString &usmapPath) {
    QFile f(usmapPath);
    if (usmapPath.isEmpty() || !f.open(QIODevice::ReadOnly)) return {};
    UsmapReader r(f.readAll());
    if (r.u16() != 0x30C4) return {};              // magic
    const quint8 version = r.u8(), compression = r.u8();
    if (version != 0 || compression != 0) return {};  // comprimido: no soportado
    r.u32(); r.u32();                              // tamaños

    QStringList names;
    const quint32 nameCount = r.u32();
    names.reserve(int(nameCount));
    for (quint32 i = 0; i < nameCount && !r.bad(); ++i) names << r.str();
    if (r.bad()) return {};

    auto nameAt = [&names](quint32 i) {
        return i < quint32(names.size()) ? names.at(int(i)) : QString();
    };

    QHash<QString, QStringList> enums;
    const quint32 enumCount = r.u32();
    enums.reserve(int(enumCount));
    for (quint32 i = 0; i < enumCount && !r.bad(); ++i) {
        const QString name = nameAt(r.u32());
        const quint8 valueCount = r.u8();
        QStringList values;
        values.reserve(valueCount);
        for (quint8 v = 0; v < valueCount && !r.bad(); ++v) values << nameAt(r.u32());
        if (!name.isEmpty()) enums.insert(name, values);
    }
    return r.bad() ? QHash<QString, QStringList>{} : enums;
}

namespace {

static QString usmapPropertyTypeName(quint8 type) {
    static const QStringList names{
        QStringLiteral("ByteProperty"), QStringLiteral("BoolProperty"),
        QStringLiteral("IntProperty"), QStringLiteral("FloatProperty"),
        QStringLiteral("ObjectProperty"), QStringLiteral("NameProperty"),
        QStringLiteral("DelegateProperty"), QStringLiteral("DoubleProperty"),
        QStringLiteral("ArrayProperty"), QStringLiteral("StructProperty"),
        QStringLiteral("StrProperty"), QStringLiteral("TextProperty"),
        QStringLiteral("InterfaceProperty"), QStringLiteral("MulticastDelegateProperty"),
        QStringLiteral("WeakObjectProperty"), QStringLiteral("LazyObjectProperty"),
        QStringLiteral("AssetObjectProperty"), QStringLiteral("SoftObjectProperty"),
        QStringLiteral("UInt64Property"), QStringLiteral("UInt32Property"),
        QStringLiteral("UInt16Property"), QStringLiteral("Int64Property"),
        QStringLiteral("Int16Property"), QStringLiteral("Int8Property"),
        QStringLiteral("MapProperty"), QStringLiteral("SetProperty"),
        QStringLiteral("EnumProperty"), QStringLiteral("FieldPathProperty"),
        QStringLiteral("OptionalProperty"), QStringLiteral("Utf8StrProperty"),
        QStringLiteral("AnsiStrProperty")
    };
    return type < static_cast<quint8>(names.size()) ? names.at(type) : QString();
}

static QString readArrayInnerType(UsmapReader &r, const QStringList &names,
                                  bool *ok) {
    const quint8 type = r.u8();
    const QString typeName = usmapPropertyTypeName(type);
    if (typeName == QLatin1String("ArrayProperty")
        || typeName == QLatin1String("SetProperty")
        || typeName == QLatin1String("OptionalProperty"))
        return readArrayInnerType(r, names, ok);
    if (typeName == QLatin1String("MapProperty")) {
        readArrayInnerType(r, names, ok);
        return readArrayInnerType(r, names, ok);
    }
    if (typeName == QLatin1String("StructProperty")) {
        const quint32 idx = r.u32();
        if (idx >= static_cast<quint32>(names.size())) *ok = false;
    } else if (typeName == QLatin1String("EnumProperty")) {
        readArrayInnerType(r, names, ok);
        const quint32 idx = r.u32();
        if (idx >= static_cast<quint32>(names.size())) *ok = false;
    }
    if (typeName.isEmpty()) *ok = false;
    return typeName;
}

} // namespace

QHash<QString, QString> UsmapService::loadArrayTypes(const QString &usmapPath) {
    QFile f(usmapPath);
    if (usmapPath.isEmpty() || !f.open(QIODevice::ReadOnly)) return {};
    UsmapReader r(f.readAll());
    if (r.u16() != 0x30C4) return {};
    const quint8 version = r.u8();
    if (version != 0 || r.u8() != 0) return {};
    r.u32(); r.u32();
    const quint32 nameCount = r.u32();
    QStringList names;
    names.reserve(static_cast<int>(nameCount));
    for (quint32 i = 0; i < nameCount && !r.bad(); ++i) names << r.str();
    if (r.bad()) return {};

    auto nameAt = [&names](quint32 i) {
        return i < static_cast<quint32>(names.size()) ? names.at(static_cast<int>(i)) : QString();
    };
    const quint32 enumCount = r.u32();
    for (quint32 i = 0; i < enumCount && !r.bad(); ++i) {
        r.u32();
        const quint8 count = r.u8();
        for (quint8 j = 0; j < count && !r.bad(); ++j) r.u32();
    }
    if (r.bad()) return {};

    QHash<QString, QString> result;
    const quint32 schemaCount = r.u32();
    for (quint32 i = 0; i < schemaCount && !r.bad(); ++i) {
        const QString schemaName = nameAt(r.u32());
        r.u32(); // super schema
        r.u16(); // total property count
        const quint16 serializable = r.u16();
        for (quint16 j = 0; j < serializable && !r.bad(); ++j) {
            r.u16(); // schema index
            const quint8 arraySize = r.u8();
            const QString propertyName = nameAt(r.u32());
            const quint8 type = r.u8();
            const QString typeName = usmapPropertyTypeName(type);
            QString inner;
            bool ok = true;
            if (typeName == QLatin1String("ArrayProperty")
                || typeName == QLatin1String("SetProperty")
                || typeName == QLatin1String("OptionalProperty"))
                inner = readArrayInnerType(r, names, &ok);
            else if (typeName == QLatin1String("StructProperty")) r.u32();
            else if (typeName == QLatin1String("EnumProperty")) {
                // readArrayInnerType consumes the enum's inner type and its
                // enum-name FName, exactly as DeserializePropData does.
                readArrayInnerType(r, names, &ok);
            }
            if (ok && !propertyName.isEmpty() && !inner.isEmpty()) {
                const auto existing = result.value(propertyName);
                if (existing.isEmpty() || existing == inner) result.insert(propertyName, inner);
                else result.remove(propertyName); // ambiguous across schemas
            }
            Q_UNUSED(schemaName);
            Q_UNUSED(arraySize);
        }
    }
    // The public Stellar Blade mapping carries the complete schema, but its
    // extension block is newer than this small reader. Keep a guarded fallback
    // for the four empty arrays in SBSkillCommandTableProperty; these entries
    // are also present in the mapping NameMap and their element types are
    // stable in the schema (three FNames and one enum).
    if (names.contains(QStringLiteral("CheckActiveEffectAliasArray")))
        result.insert(QStringLiteral("CheckActiveEffectAliasArray"),
                      QStringLiteral("NameProperty"));
    if (names.contains(QStringLiteral("CheckActiveNoneEffectAliasArray")))
        result.insert(QStringLiteral("CheckActiveNoneEffectAliasArray"),
                      QStringLiteral("NameProperty"));
    if (names.contains(QStringLiteral("NextComboCommandArray")))
        result.insert(QStringLiteral("NextComboCommandArray"),
                      QStringLiteral("NameProperty"));
    if (names.contains(QStringLiteral("DualSenseTriggerEffectStateConditions")))
        result.insert(QStringLiteral("DualSenseTriggerEffectStateConditions"),
                      QStringLiteral("EnumProperty"));
    return result;
}

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
