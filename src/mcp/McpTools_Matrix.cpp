// McpTools_Matrix.cpp — matrix-preset MCP tools (R3, docs/plans/2026-09-16-matrix-presets.md).
//
// list_matrix_presets / apply_matrix_preset — MCP parity for the per-plugin
// matrix-preset pipeline. Engine surface ONLY: sheet parsing + dispatch onto
// EXISTING engine entry points — PluginParamService::setParam (the set_fx_param
// path), ProjectCommands::sendFxMidi (the send_fx_midi path), and
// runNordBankFile (the load_nord_bank path, PresetRoute.h). No DSP, no
// audio-thread code, no ValueTree schema changes.
//
// Sheets (timbre-lib/matrix_presets/):
//   <engine>.json                 schema hdaw.matrix.preset.v1
//   <engine>_morphs.json          schema hdaw.matrix.preset.morph.v1 (xenia:
//                                 xenia_morphs_injectable.json preferred — the
//                                 variant whose steps carry injectable SysEx)
//   <engine>_param_index_map.json decoder name -> live plugin param index
// Location: env HDAW_MATRIX_PRESETS_DIR first, else <cwd>/timbre-lib/
// matrix_presets, <exeDir>/../timbre-lib/matrix_presets, <exeDir>/timbre-lib/
// matrix_presets. The resolved dir is cached (keyed on the env value so an env
// change — tests — re-resolves).

#include "McpTools.h"
#include "McpTools_Private.h"
#include "PresetRoute.h"
#include "McpServer.h"
#include "McpToolDef.h"
#include "../model/ProjectModel.h"
#include "../engine/AudioEngine.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <algorithm>
#include <string>

namespace mcp {

namespace {

constexpr int kMaxSheetBytes = 1024 * 1024; // shipped sheets are <= 500 KB
constexpr const char* kSheetSchema = "hdaw.matrix.preset.v1";

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

struct MatrixDir
{
    QString env;   // env value the cache was resolved for
    QString dir;   // resolved presets directory (empty + error set on failure)
    QString error;
};

const MatrixDir& resolveMatrixDir()
{
    static MatrixDir cache;
    const QString env = qEnvironmentVariable("HDAW_MATRIX_PRESETS_DIR");
    if (!cache.dir.isEmpty() && cache.env == env)
        return cache;

    cache = MatrixDir{};
    cache.env = env;

    const auto hasSheets = [](const QDir& d) {
        return d.exists() && !d.entryList({ "*.json" }, QDir::Files).isEmpty();
    };

    if (!env.isEmpty())
    {
        // An explicit override is authoritative: never silently fall back.
        if (hasSheets(QDir(env)))
            cache.dir = QDir::cleanPath(env);
        else
            cache.error = QString("HDAW_MATRIX_PRESETS_DIR is set but not a usable"
                                  " matrix presets dir (no *.json sheets): %1").arg(env);
        return cache;
    }

    QStringList candidates;
    candidates << (QDir::currentPath() + "/timbre-lib/matrix_presets");
    const QString exeDir = QCoreApplication::applicationDirPath();
    candidates << (exeDir + "/../timbre-lib/matrix_presets");
    candidates << (exeDir + "/timbre-lib/matrix_presets");
    for (const QString& c : candidates)
    {
        if (hasSheets(QDir(c)))
        {
            cache.dir = QDir::cleanPath(c);
            break;
        }
    }
    if (cache.dir.isEmpty())
        cache.error = QString("matrix presets directory not found (set"
                              " HDAW_MATRIX_PRESETS_DIR; tried: %1)")
                          .arg(candidates.join(", "));
    return cache;
}

// Size-bounded JSON load — sheets are small; the hard bound keeps a runaway
// file from stalling the MCP command thread.
QJsonDocument loadJsonBounded(const QString& path, QString& err)
{
    err.clear();
    QFile f(path);
    if (!f.exists())
    {
        err = "file not found: " + path;
        return {};
    }
    if (!f.open(QIODevice::ReadOnly))
    {
        err = "cannot open: " + path;
        return {};
    }
    if (f.size() > kMaxSheetBytes)
    {
        err = QString("sheet too large (%1 bytes, max %2): %3")
                  .arg(f.size()).arg(kMaxSheetBytes).arg(path);
        return {};
    }
    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (doc.isNull() || !doc.isObject())
    {
        err = QString("invalid JSON in %1: %2").arg(path, pe.errorString());
        return {};
    }
    return doc;
}

QStringList morphFileCandidates(const QString& engine)
{
    // xenia: the injectable variant (steps carry full 265-byte SysEx) is the
    // live one; the plain sheet is the pre-injection analysis export.
    if (engine == "xenia")
        return { "xenia_morphs_injectable.json", "xenia_morphs.json" };
    return { engine + "_morphs.json" };
}

struct MorphSheet
{
    bool exists = false;
    QString file;  // file NAME within the presets dir
    QJsonObject root;
};

MorphSheet loadMorphSheet(const QString& dir, const QString& engine)
{
    MorphSheet m;
    for (const QString& name : morphFileCandidates(engine))
    {
        QString err;
        const QJsonDocument doc = loadJsonBounded(dir + "/" + name, err);
        if (!err.isEmpty() || !doc.isObject())
            continue; // missing/invalid morph sheet is not fatal for listing
        m.exists = true;
        m.file = name;
        m.root = doc.object();
        return m;
    }
    return m;
}

// Morph steps can reference loose .syx FILES (nord: nord_morphs/<pair>/
// stepN.syx). The refs resolve against a files base: the morph sheet's own
// stem-named subdirectory when it exists (nord_morphs.json -> nord_morphs/),
// else the presets dir (inline-sysex sheets need no base).
QString morphFilesBase(const QString& dir, const QString& morphFileName)
{
    const QString stem = morphFileName.left(morphFileName.size() - 5); // strip .json
    const QString sub = dir + "/" + stem;
    if (QDir(sub).exists())
        return sub;
    return dir;
}

// Apply kind advertised for a morph pair, derived from what its steps carry:
// 'sysex' (injectable SysEx arrays) > 'file' (.syx step files) > 'params'
// (parameter-level steps). sysex wins because it is the only fully
// self-contained payload; virus_morphs.json (params only today) upgrades to
// 'sysex' automatically once its parallel sysex task lands.
QString morphApplyKind(const QJsonObject& pair)
{
    const auto steps = pair.value("steps").toArray();
    bool hasSysex = false, hasFile = false, hasParams = false;
    for (const auto& sv : steps)
    {
        const auto step = sv.toObject();
        if (!step.value("sysex").toArray().isEmpty())
            hasSysex = true;
        const auto preset = step.value("preset").toObject();
        if (!preset.value("file").toString().isEmpty()
            || !step.value("file").toString().isEmpty())
            hasFile = true;
        if (!step.value("paramIndex").toObject().isEmpty()
            || !step.value("params").toObject().isEmpty()
            || !preset.value("params").toObject().isEmpty())
            hasParams = true;
    }
    if (hasSysex) return "sysex";
    if (hasFile) return "file";
    if (hasParams) return "params";
    return "none";
}

// Engines with a sheet in the presets dir (error-path helper: tell the caller
// what IS available). Morph/index-map/offset files are not engine sheets.
QStringList availableEngines(const QString& dir)
{
    QStringList engines;
    const auto files = QDir(dir).entryList({ "*.json" }, QDir::Files);
    for (const QString& f : files)
    {
        if (f.contains("_morphs") || f.contains("_param_index_map")
            || f.contains("offset_map") || f.contains("offset-map"))
            continue;
        QString err;
        const QJsonDocument doc = loadJsonBounded(dir + "/" + f, err);
        if (!err.isEmpty())
            continue;
        const QJsonObject root = doc.object();
        if (!root.value("schema").toString().isEmpty()
            && root.value("schema").toString() != kSheetSchema)
            continue;
        engines << root.value("engine").toString(f.left(f.size() - 5));
    }
    engines.sort();
    engines.removeDuplicates();
    return engines;
}

QString sheetMissingMessage(const QString& engine, const QString& dir, const QString& err)
{
    if (!err.startsWith("file not found"))
        return err;
    const QStringList avail = availableEngines(dir);
    return QString("no matrix preset sheet for engine '%1' in %2 (available: %3)")
        .arg(engine, dir, avail.isEmpty() ? QString("none") : avail.join(", "));
}

struct SlotRef
{
    QString pluginId;
    QString error;
};

// Matrix presets target the LIVE plugin instance — every apply path (param,
// sysex, file) resolves through the same ReadModel slot lookup set_fx_param
// and send_fx_midi use.
SlotRef resolvePluginSlot(AudioEngine* e, int ti, int si)
{
    auto tl = e->getProjectModel().getTrackListTree();
    if (ti < 0 || ti >= tl.getNumChildren())
        return { {}, "track not found" };
    auto fxSlots = e->getReadModel().getFxSlots(ti);
    if (si < 0 || si >= (int)fxSlots.size())
        return { {}, "slot not found" };
    if (fxSlots[si].fxType == "none")
        return { {}, "slot is empty" };
    if (fxSlots[si].fxType != "plugin")
        return { {}, "slot is not a plugin (matrix presets apply to the live plugin instance)" };
    if (fxSlots[si].pluginId.empty())
        return { {}, "slot has no pluginId" };
    return { QString::fromStdString(fxSlots[si].pluginId), {} };
}

// Parameter-level apply — the set_fx_param engine path (PluginParamService::
// setParam). Sheet values are the decoders' 0..127 MIDI scale; the plugin
// param space is normalized 0..1, so values are clamped to [0,127] and scaled
// by /127 (the convention the 2026-09-16 live apply pass used: 46/46 params
// applied with an audible A/B delta). Null values mean the harvester found no
// consistent value across the cluster — nothing to set, counted as skipped.
// Names absent from the index map are reported in unmapped (never silently
// dropped).
struct ParamApplyResult
{
    int applied = 0;
    int skipped = 0;
    QJsonArray unmapped;
};

ParamApplyResult applyParamsToSlot(AudioEngine& e, int ti, const QString& pluginId,
                                   const QJsonObject& params,
                                   const QJsonObject& stepIndex,
                                   const QJsonObject& engineMap)
{
    ParamApplyResult r;
    const auto mapped = engineMap.value("map").toObject();
    for (auto it = params.begin(); it != params.end(); ++it)
    {
        const QString name = it.key();
        const QJsonValue v = it.value();
        if (v.isNull() || v.isUndefined())
        {
            ++r.skipped;
            continue;
        }
        int idx = stepIndex.value(name).toInt(-1);
        if (idx < 0)
            idx = mapped.value(name).toObject().value("index").toInt(-1);
        if (idx < 0)
        {
            r.unmapped.append(name);
            ++r.skipped;
            continue;
        }
        const double raw = v.toDouble();
        const float normalized = static_cast<float>(std::clamp(raw, 0.0, 127.0) / 127.0);
        e.getPluginParamService().setParam(ti, pluginId.toStdString(), idx, normalized);
        ++r.applied;
    }
    return r;
}

McpToolResult paramApplyResultText(const ParamApplyResult& r)
{
    return McpToolResult::text(QString::fromUtf8(QJsonDocument(QJsonObject {
        { "applied", r.applied }, { "skipped", r.skipped }, { "unmapped", r.unmapped }
    }).toJson(QJsonDocument::Compact)));
}

} // namespace

void registerMatrixTools(McpServer& s, AudioEngine* e)
{
    s.registerTool({ "list_matrix_presets",
        "List the harvested matrix presets + morph chains for ONE core plugin engine"
        " (je8086, virus, nodalred2x, xenia, vavra). Sheets are read from"
        " timbre-lib/matrix_presets/ (HDAW_MATRIX_PRESETS_DIR overrides the location)."
        " Returns {engine, sheet, presets:[{id,name,role,appliesVia,evidence}],"
        " morphs:[{pair,distance,steps,apply}]} where morph apply is 'sysex'"
        " (injectable SysEx steps), 'file' (.syx step files, nord), or 'params'",
        objSchema({ { "engine", QJsonObject{ { "type", "string" } } } },
                  QJsonArray{ "engine" }),
        "fx",
        [e](const QJsonObject& a) -> McpToolResult {
            const QString engine = a.value("engine").toString();
            if (!validEngineId(engine))
                return McpToolResult::text(
                    "engine must match [a-z0-9_] (e.g. je8086, virus, nodalred2x, xenia, vavra)", true);
            const auto& md = resolveMatrixDir();
            if (md.dir.isEmpty())
                return McpToolResult::text(md.error, true);

            QString err;
            const QJsonDocument doc = loadJsonBounded(md.dir + "/" + engine + ".json", err);
            if (!err.isEmpty())
                return McpToolResult::text(sheetMissingMessage(engine, md.dir, err), true);
            const QJsonObject root = doc.object();
            const QString schema = root.value("schema").toString();
            if (!schema.isEmpty() && schema != kSheetSchema)
                return McpToolResult::text(QString("%1.json: unsupported schema '%2' (expected %3)")
                                               .arg(engine, schema, kSheetSchema), true);

            QJsonArray presets;
            for (const auto& pv : root.value("presets").toArray())
            {
                const auto p = pv.toObject();
                presets.append(QJsonObject {
                    { "id", p.value("id") },
                    { "name", p.value("name") },
                    { "role", p.value("role") },
                    { "appliesVia", p.value("appliesVia") },
                    { "evidence", p.value("evidence") } });
            }

            QJsonArray morphs;
            const auto ms = loadMorphSheet(md.dir, engine);
            if (ms.exists)
            {
                for (const auto& pv : ms.root.value("pairs").toArray())
                {
                    const auto pair = pv.toObject();
                    QJsonObject o {
                        { "pair", pair.value("pair") },
                        { "steps", pair.value("steps").toArray().size() },
                        { "apply", morphApplyKind(pair) } };
                    if (pair.contains("distance"))
                        o["distance"] = pair.value("distance");
                    morphs.append(o);
                }
            }

            return McpToolResult::text(QString::fromUtf8(QJsonDocument(QJsonObject {
                { "engine", engine },
                { "sheet", engine + ".json" },
                { "presets", presets },
                { "morphs", morphs } }).toJson(QJsonDocument::Compact)));
        } });

    s.registerTool({ "apply_matrix_preset",
        "Apply ONE matrix preset or morph step (ids from list_matrix_presets) to a plugin FX"
        " slot. Dispatch: parameter-level ids (je8086 presets / param-carrying morph steps) go"
        " through the set_fx_param engine path, resolving decoder names to live param indexes"
        " via <engine>_param_index_map.json, and return {applied,skipped,unmapped}; morph steps"
        " carrying SysEx queue through the send_fx_midi path and return"
        " {queued,captureDeferred:true}; nodalred2x morph steps load their .syx file through the"
        " load_nord_bank path. Realtime mutation: not undoable; capture via project save.",
        objSchema({ { "engine", QJsonObject{ { "type", "string" } } },
                   { "id", QJsonObject{ { "type", "string" } } },
                   { "trackId", QJsonObject{ { "type", "integer" } } },
                   { "slotIndex", QJsonObject{ { "type", "integer" } } },
                   { "captureToTree", QJsonObject{ { "type", "boolean" } } } },
                  QJsonArray{ "engine", "id", "trackId", "slotIndex" }),
        "fx",
        [e](const QJsonObject& a) -> McpToolResult {
            const QString engine = a.value("engine").toString();
            const QString id = a.value("id").toString();
            if (!validEngineId(engine))
                return McpToolResult::text(
                    "engine must match [a-z0-9_] (e.g. je8086, virus, nodalred2x, xenia, vavra)", true);
            if (id.isEmpty())
                return McpToolResult::text("id required (see list_matrix_presets)", true);
            if (!a.contains("trackId") || !a.contains("slotIndex"))
                return McpToolResult::text("trackId and slotIndex required", true);
            const int ti = a.value("trackId").toInt(-1);
            const int si = a.value("slotIndex").toInt(-1);
            const auto& md = resolveMatrixDir();
            if (md.dir.isEmpty())
                return McpToolResult::text(md.error, true);

            QString err;
            const QJsonDocument doc = loadJsonBounded(md.dir + "/" + engine + ".json", err);
            if (!err.isEmpty())
                return McpToolResult::text(sheetMissingMessage(engine, md.dir, err), true);
            const QJsonObject root = doc.object();

            // -- 1) sheet preset ids ------------------------------------------------
            QJsonObject preset;
            for (const auto& pv : root.value("presets").toArray())
            {
                if (pv.toObject().value("id").toString() == id)
                {
                    preset = pv.toObject();
                    break;
                }
            }
            if (!preset.isEmpty())
            {
                const QString via = preset.value("appliesVia").toString();
                if (!via.isEmpty() && via != "set_fx_param")
                    return McpToolResult::text(
                        QString("preset '%1' appliesVia '%2' has no parameter-level apply path"
                                " (use the engine's native loader; see docs/hardware-va-suite.md)")
                            .arg(id, via), true);

                const auto slot = resolvePluginSlot(e, ti, si);
                if (!slot.error.isEmpty())
                    return McpToolResult::text(slot.error, true);

                const QString mapPath = md.dir + "/" + engine + "_param_index_map.json";
                QJsonObject engineMap;
                if (QFileInfo::exists(mapPath))
                {
                    QString merr;
                    const QJsonDocument mdoc = loadJsonBounded(mapPath, merr);
                    if (!merr.isEmpty())
                        return McpToolResult::text(merr, true);
                    engineMap = mdoc.object();
                }
                const auto r = applyParamsToSlot(*e, ti, slot.pluginId,
                                                 preset.value("params").toObject(), {}, engineMap);
                return paramApplyResultText(r);
            }

            // -- 2) morph step ids ('<pair>:step<k>' or the step preset id) ---------
            const auto ms = loadMorphSheet(md.dir, engine);
            const QJsonArray pairs = ms.exists ? ms.root.value("pairs").toArray() : QJsonArray{};

            QJsonObject pairObj;
            QString pairName;
            int stepK = -1;

            const int lc = id.lastIndexOf(':');
            if (lc > 0 && id.mid(lc + 1).startsWith("step"))
            {
                bool ok = false;
                const int k = id.mid(lc + 5).toInt(&ok); // 'step' is 4 chars
                const QString pn = id.left(lc);
                if (ok && k >= 1)
                {
                    for (const auto& pv : pairs)
                    {
                        if (pv.toObject().value("pair").toString() == pn)
                        {
                            pairObj = pv.toObject();
                            pairName = pn;
                            stepK = k;
                            break;
                        }
                    }
                }
            }
            QJsonObject step;
            if (stepK >= 1 && !pairObj.isEmpty())
            {
                const auto steps = pairObj.value("steps").toArray();
                if (stepK <= steps.size())
                    step = steps.at(stepK - 1).toObject();
            }
            else
            {
                for (const auto& pv : pairs)
                {
                    const auto p = pv.toObject();
                    for (const auto& sv : p.value("steps").toArray())
                    {
                        if (sv.toObject().value("preset").toObject().value("id").toString() == id)
                        {
                            pairObj = p;
                            pairName = p.value("pair").toString();
                            step = sv.toObject();
                            break;
                        }
                    }
                    if (!step.isEmpty())
                        break;
                }
            }
            if (step.isEmpty())
                return McpToolResult::text(
                    QString("id '%1' not found in %2 presets or morph steps — call"
                            " list_matrix_presets for valid ids").arg(id, engine), true);

            const auto slot = resolvePluginSlot(e, ti, si);
            if (!slot.error.isEmpty())
                return McpToolResult::text(slot.error, true);

            // 2a) injectable SysEx — one message, the send_fx_midi engine path.
            const auto sysex = step.value("sysex").toArray();
            if (!sysex.isEmpty())
            {
                ProjectCommands::FxMidiParams p;
                p.trackIndex = ti;
                p.slotIndex = si;
                p.captureToTree = a.value("captureToTree").toBool(true);
                ProjectCommands::FxMidiEvent ev;
                ev.kind = ProjectCommands::FxMidiEvent::Kind::SysEx;
                for (const auto& b : sysex)
                    ev.sysex.push_back(static_cast<uint8_t>(b.toInt() & 0xFF));
                p.events.push_back(std::move(ev));
                const auto r = e->getProjectCommands().sendFxMidi(p);
                if (!r.ok)
                    return McpToolResult::text(QString::fromStdString(r.error), true);
                return McpToolResult::text(QString::fromUtf8(QJsonDocument(QJsonObject {
                    { "queued", r.queued }, { "captureDeferred", true }
                }).toJson(QJsonDocument::Compact)));
            }

            // 2b) loose .syx step file — the load_nord_bank engine path.
            QString fileRef = step.value("preset").toObject().value("file").toString();
            if (fileRef.isEmpty())
                fileRef = step.value("file").toString();
            if (!fileRef.isEmpty())
            {
                const QString base = morphFilesBase(md.dir, ms.file);
                QString pairDir = pairName;
                pairDir.replace(':', '-'); // nord pairs '29:32' live in '29-32/'
                const QString path = base + "/" + pairDir + "/" + fileRef;
                return runNordBankFile(*e, ti, si, path, -1,
                                       a.value("captureToTree").toBool(true));
            }

            // 2c) parameter-level morph steps (je8086 embeds paramIndex per step).
            QJsonObject stepParams = step.value("params").toObject();
            if (stepParams.isEmpty())
                stepParams = step.value("preset").toObject().value("params").toObject();
            if (!stepParams.isEmpty())
            {
                QJsonObject engineMap;
                const QString mapPath = md.dir + "/" + engine + "_param_index_map.json";
                if (QFileInfo::exists(mapPath))
                {
                    QString merr;
                    const QJsonDocument mdoc = loadJsonBounded(mapPath, merr);
                    if (!merr.isEmpty())
                        return McpToolResult::text(merr, true);
                    engineMap = mdoc.object();
                }
                const auto r = applyParamsToSlot(*e, ti, slot.pluginId, stepParams,
                                                 step.value("paramIndex").toObject(), engineMap);
                return paramApplyResultText(r);
            }

            return McpToolResult::text(
                QString("morph step '%1' carries no applicable payload (no sysex, file, or params)")
                    .arg(id), true);
        } });
}

} // namespace mcp
