#include "DeviceParamMap.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>

namespace HDAW {

namespace {

// vavra.params.json is ~110 KB; the hard bound keeps a runaway file from
// stalling the calling (MCP/RPC) thread.
constexpr qint64 kMaxMapBytes = 4 * 1024 * 1024;

bool hasMaps(const QDir& d)
{
    return d.exists() && !d.entryList({ "*.params.json" }, QDir::Files).isEmpty();
}

bool matchesAny(const QJsonArray& arr, const QString& needle)
{
    for (const auto& v : arr)
        if (v.toString().compare(needle, Qt::CaseInsensitive) == 0)
            return true;
    return false;
}

} // namespace

const DeviceMapDir& resolveDeviceMapDir()
{
    static DeviceMapDir cache;
    const QString env = qEnvironmentVariable("HDAW_DEVICE_MAP_DIR");
    if (!cache.dir.isEmpty() && cache.env == env)
        return cache;

    cache = DeviceMapDir{};
    cache.env = env;

    if (!env.isEmpty())
    {
        // An explicit override is authoritative: never silently fall back.
        if (hasMaps(QDir(env)))
            cache.dir = QDir::cleanPath(env);
        else
            cache.error = QString("HDAW_DEVICE_MAP_DIR is set but not a usable"
                                  " device map dir (no *.params.json): %1").arg(env);
        return cache;
    }

    QStringList candidates;
    candidates << (QDir::currentPath() + "/timbre-lib/device_map");
    const QString exeDir = QCoreApplication::applicationDirPath();
    candidates << (exeDir + "/../timbre-lib/device_map");
    candidates << (exeDir + "/timbre-lib/device_map");
    for (const QString& c : candidates)
    {
        if (hasMaps(QDir(c)))
        {
            cache.dir = QDir::cleanPath(c);
            break;
        }
    }
    if (cache.dir.isEmpty())
        cache.error = QString("device map directory not found (set"
                              " HDAW_DEVICE_MAP_DIR; tried: %1)")
                          .arg(candidates.join(", "));
    return cache;
}

QJsonObject readDeviceMapFile(const QString& path, QString& err)
{
    err.clear();
    QFile f(path);
    if (!f.exists()) { err = "file not found: " + path; return {}; }
    if (!f.open(QIODevice::ReadOnly)) { err = "cannot open: " + path; return {}; }
    if (f.size() > kMaxMapBytes)
    {
        err = QString("map too large (%1 bytes, max %2): %3")
                  .arg(f.size()).arg(kMaxMapBytes).arg(path);
        return {};
    }
    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (doc.isNull() || !doc.isObject())
    {
        err = QString("invalid JSON in %1: %2").arg(path, pe.errorString());
        return {};
    }
    return doc.object();
}

QStringList deviceMapEngines(const QString& dir)
{
    QStringList out;
    const QDir d(dir);
    for (const QString& f : d.entryList({ "*.params.json" }, QDir::Files))
        out << f.left(f.size() - QString(".params.json").size());
    out.sort();
    out.removeDuplicates();
    return out;
}

bool validEngineId(const QString& engine)
{
    if (engine.isEmpty() || engine.size() > 64)
        return false;
    for (const QChar& c : engine)
    {
        const auto u = c.unicode();
        if (!((u >= 'a' && u <= 'z') || (u >= '0' && u <= '9') || u == '_'))
            return false;
    }
    return true;
}

QJsonArray filterDeviceParams(const QJsonObject& map,
                              const QString& category, const QString& intent,
                              const QString& stage, const QString& tier,
                              int limit, int& matched)
{
    matched = 0;
    QJsonArray out;
    for (const auto& pv : map.value("params").toArray())
    {
        const QJsonObject p = pv.toObject();
        if (!category.isEmpty() &&
            p.value("category").toString().compare(category, Qt::CaseInsensitive) != 0)
            continue;
        if (!tier.isEmpty() &&
            p.value("tier").toString().compare(tier, Qt::CaseInsensitive) != 0)
            continue;
        if (!intent.isEmpty() && !matchesAny(p.value("intents").toArray(), intent))
            continue;
        if (!stage.isEmpty() && !matchesAny(p.value("stages").toArray(), stage))
            continue;

        ++matched;
        if (out.size() >= limit)
            continue;

        QJsonObject o {
            { "name", p.value("name") },
            { "category", p.value("category") },
            { "tier", p.value("tier") },
            { "intents", p.value("intents") },
            { "stages", p.value("stages") } };
        if (!p.value("index").isNull())      o["index"] = p.value("index");
        if (!p.value("offset").isNull())     o["offset"] = p.value("offset");
        if (!p.value("trapReason").isNull()) o["trapReason"] = p.value("trapReason");
        if (!p.value("note").isNull())       o["note"] = p.value("note");
        out.append(o);
    }
    return out;
}

} // namespace HDAW
