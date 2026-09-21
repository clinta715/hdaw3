#include "MatrixPresetService.h"

#include "../engine/AudioEngine.h"
#include "../model/ProjectModel.h"
#include "../common/ProjectCommands.h"
#include "../common/ParamOverrideLedger.h"
#include "../mcp/PresetFileParser.h"   // pure Nord .syx parsing (juce_core only)

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QtGlobal>

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace HDAW {

namespace {

constexpr int kMaxSheetBytes = 1024 * 1024; // shipped sheets are <= 500 KB
constexpr const char* kSheetSchema = "hdaw.matrix.preset.v1";

MatrixOpResult fail(const QString& msg, int code = -32602)
{
    MatrixOpResult r;
    r.error = msg.toStdString();
    r.errorCode = code;
    return r;
}

// Environment / artifact failure (missing presets dir or sheet, unreadable or
// invalid JSON, engine-command failure): the caller's arguments were fine, the
// machine's state was not.
MatrixOpResult failEnv(const QString& msg)
{
    return fail(msg, -32603);
}

MatrixOpResult succeed(QJsonObject payload)
{
    MatrixOpResult r;
    r.ok = true;
    r.payload = std::move(payload);
    return r;
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

// Size-bounded JSON load — sheets are small; the hard bound keeps a runaway file
// from stalling the command thread.
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
    // xenia: the injectable variant (steps carry full 265-byte SysEx) is the live
    // one; the plain sheet is the pre-injection analysis export.
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

// Morph steps can reference loose .syx FILES (nord: nord_morphs/<pair>/stepN.syx).
// The refs resolve against a files base: the morph sheet's own stem-named
// subdirectory when it exists (nord_morphs.json -> nord_morphs/), else the presets
// dir (inline-sysex sheets need no base).
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
// (parameter-level steps). sysex wins because it is the only fully self-contained
// payload; virus_morphs.json (params only today) upgrades to 'sysex' automatically
// once its parallel sysex task lands.
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

// Engines with a sheet in the presets dir (error-path helper: tell the caller what
// IS available). Morph/index-map/offset files are not engine sheets.
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

// Matrix presets target the LIVE plugin instance — every apply path (param, sysex,
// file) resolves through the same ReadModel slot lookup set_fx_param and
// send_fx_midi use.
SlotRef resolvePluginSlot(AudioEngine& e, int ti, int si)
{
    auto tl = e.getProjectModel().getTrackListTree();
    if (ti < 0 || ti >= tl.getNumChildren())
        return { {}, "track not found" };
    auto fxSlots = e.getReadModel().getFxSlots(ti);
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

// Parameter-level apply result — the set_fx_param engine path
// (PluginParamService::setParam). Sheet values are the decoders' 0..127 MIDI
// scale; the plugin param space is normalized 0..1, so values are clamped to
// [0,127] and scaled by /127 (the convention the 2026-09-16 live apply pass used:
// 46/46 params applied with an audible A/B delta). Null values mean the harvester
// found no consistent value across the cluster — nothing to set, counted as
// skipped. Names absent from the index map are reported in unmapped (never
// silently dropped).
struct ParamApplyResult
{
    int applied = 0;
    int skipped = 0;
    QJsonArray unmapped;
    // RESOLVED param overrides (live param index -> normalized 0..1) — exactly the
    // pairs this loop dispatched to setParam. The caller persists them as the
    // FX_SLOT offline-replay ledger (IDs::appliedParamOverrides), because the
    // deferred state capture is a dead end for plugins whose getStateInformation
    // does not serialize param-driven state (JE8086, measured 2026-09-16). Keys are
    // decimal index strings; a repeated live index (ties) keeps the LAST value —
    // the ledger mirrors the plugin's final param state.
    QJsonObject overrides;
    // Deferred plugin-state capture (send_fx_midi's receipt contract, NB4):
    // setParam reaches the LIVE child only; the capture snapshots the applied state
    // into IDs::pluginState so offline renders see the preset.
    bool captureToTree = false;
    bool capturedToTree = false;   // sync capture wrote IDs::pluginState
    QString captureStatus;         // "pending" | "ok" | "unchanged" | "failed: ..."
    QString captureNote;           // e.g. the deferred-capture note (device running)
    int captureStateBytes = 0;
};

// Normalised live-name resolution: the harvest sheets carry the device's own
// vocabulary ("Assign 4 Amount", "F1Cutoff") while the live plugin exposes
// part-prefixed names ("Ch 1 Assign 4 Amount"), so a static
// <engine>_param_index_map.json was previously the only route. Resolve against the
// slot's actual param list first — it is authoritative and self-maintaining (Virus
// 3086/6939, Vavra 7557, Xenia 2151, NodalRed2x 362 params are exposed).
int resolveLiveParamIndex(AudioEngine& e, int ti, const QString& pluginId, const QString& name)
{
    const auto norm = [](QString s) {
        s = s.toLower();
        s.remove(' ');
        s.remove('_');
        s.remove('-');
        s.remove('/');
        // strip a leading part/channel prefix: "ch1...", "ch12...", "part3..."
        if (s.startsWith("ch"))
        {
            int k = 2;
            while (k < s.size() && s[k].isDigit()) ++k;
            if (k > 2) s = s.mid(k);
        }
        else if (s.startsWith("part"))
        {
            int k = 4;
            while (k < s.size() && s[k].isDigit()) ++k;
            if (k > 4) s = s.mid(k);
        }
        return s;
    };
    const auto want = norm(name);
    if (want.isEmpty())
        return -1;
    const auto live = e.getPluginParamService().getParams(ti, pluginId.toStdString());
    // exact (normalised) match first
    for (const auto& p : live)
        if (norm(QString::fromStdString(p.name)) == want)
            return p.index;
    // then suffix match (sheet name without the part prefix); first hit wins, which
    // is the lowest part — the sheets describe single-part configs
    for (const auto& p : live)
        if (norm(QString::fromStdString(p.name)).endsWith(want))
            return p.index;
    return -1;
}

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
            idx = resolveLiveParamIndex(e, ti, pluginId, name);
        if (idx < 0)
        {
            r.unmapped.append(name);
            ++r.skipped;
            continue;
        }
        const double raw = v.toDouble();
        const float normalized = static_cast<float>(std::clamp(raw, 0.0, 127.0) / 127.0);
        e.getPluginParamService().setParam(ti, pluginId.toStdString(), idx, normalized);
        r.overrides[QString::number(idx)] = static_cast<double>(normalized);
        ++r.applied;
    }
    return r;
}

QJsonObject paramApplyPayload(const ParamApplyResult& r)
{
    QJsonObject o {
        { "applied", r.applied }, { "skipped", r.skipped }, { "unmapped", r.unmapped },
        { "captureToTree", r.captureToTree } };
    if (!r.overrides.isEmpty())
        o["paramOverrides"] = r.overrides.size(); // offline replay ledger size (IDs::appliedParamOverrides)
    if (r.captureToTree)
    {
        // send_fx_midi's receipt contract: status is "pending" while a deferred
        // capture is in flight (poll get_fx_capture_status), or the synchronous
        // outcome ("ok" / "unchanged" / "failed: ...").
        QJsonObject cap { { "status", r.captureStatus } };
        if (r.capturedToTree)
            cap["capturedToTree"] = true;
        if (r.captureStateBytes > 0)
            cap["stateBytes"] = r.captureStateBytes;
        if (!r.captureNote.isEmpty())
            cap["note"] = r.captureNote;
        o["capture"] = cap;
    }
    return o;
}

// Deferred plugin-state capture for the param-apply path — the SAME trigger
// sendFxMidi performs after queueing (ProjectCommands::captureFxSlotState: pending
// receipt -> live getStateInformation -> IDs::pluginState).
// PluginParamService::setParam reaches the LIVE isolated child only, while
// export_audio / verify_part build a FRESH offline plugin domain that boots at init
// state — without this capture, applied presets render as the init patch (measured
// 2026-09-17: 40/40 ear-pass renders bit-identical init audio).
// captureToTree=false (the send_fx_midi flag) skips the trigger. applied <= 0 means
// nothing was written, so there is no state to capture.
void captureAfterParamApply(AudioEngine& e, int ti, int si, int applied,
                            bool captureToTree, ParamApplyResult& r)
{
    r.captureToTree = captureToTree;
    if (!captureToTree || applied <= 0)
        return;
    const auto cap = e.getProjectCommands().captureFxSlotState(ti, si);
    r.captureStatus = QString::fromStdString(cap.ok ? cap.status : "failed: " + cap.error);
    r.capturedToTree = cap.capturedToTree;
    r.captureStateBytes = cap.stateBytes;
    if (!cap.note.empty())
        r.captureNote = QString::fromStdString(cap.note);
}

// Persist the resolved overrides as the FX_SLOT offline-replay ledger:
// IDs::appliedParamOverrides in the shared plain "idx=val;idx=val" grammar owned by
// src/common/ParamOverrideLedger.h (NOT juce::var/JSON — the var(DynamicObject*)
// -> JSON::toString round-trip produced an empty object in this JUCE build,
// measured 2026-09-18: the stored ledger was just "{"). The export render thread
// replays it via ExportManager::replayAppliedParamOverrides after the bake wait.
// Written REGARDLESS of captureToTree — the ledger is orthogonal to the state
// capture. Empty overrides write NOTHING, so slots without the property keep their
// exact prior offline behavior. nullptr undo: matches the pluginState
// volatile-cache convention in AudioEngineCommands_Fx.cpp. The ledger reflects the
// MOST RECENT apply_matrix_preset call (replace semantics — the ear-pass contract
// is one preset per render).
void writeAppliedParamOverrides(AudioEngine& e, int ti, int si,
                                const ParamApplyResult& r)
{
    if (r.overrides.isEmpty())
        return;
    auto slotTree = e.getProjectModel().getTrackListTree().getChild(ti)
                        .getChildWithName(IDs::FX_CHAIN).getChild(si);
    if (!slotTree.isValid())
        return;
    juce::String ledger;
    for (auto it = r.overrides.begin(); it != r.overrides.end(); ++it)
    {
        ledger += (ledger.isEmpty() ? juce::String() : juce::String(";"))
                + HDAW::formatParamOverride(it.key().toInt(),
                                            static_cast<float>(it.value().toDouble()));
    }
    if (!ledger.isEmpty())
        slotTree.setProperty(IDs::appliedParamOverrides, ledger, nullptr);
}

// Loose .syx / .mid step-file route (the load_nord_bank wire format): validate
// EVERY dump before queueing anything (no partial bank loads), then queue the dumps
// (+ optional program change) and a trailing CC125 so the deferred state capture
// sees inert trailing state.
//
// The parsing/validation helpers come from the pure parser (src/mcp/
// PresetFileParser.h — juce_core only, no MCP server/tool types), which is the
// shared authority for the Nord wire format.
MatrixOpResult applyNordSyxFile(AudioEngine& e, int ti, int si, const QString& path,
                                int program, bool captureToTree)
{
    const juce::File f(juce::String::fromUTF8(path.toUtf8()));
    if (!f.existsAsFile())
        return failEnv("file not found: " + path);
    juce::MemoryBlock block;
    if (!f.loadFileAsData(block))
        return failEnv("failed to read file");
    const auto suffix = f.getFileExtension().toLowerCase();
    // Normalize to complete F0..F7 dumps (payload coordinates differ between
    // containers; see PresetFileParser.h for the wire format).
    std::vector<std::vector<uint8_t>> dumps;
    if (suffix == ".syx")
    {
        const auto* b = static_cast<const uint8_t*>(block.getData());
        if (mcp::splitNordSyx(b, block.getSize(), dumps) < 0)
            return failEnv("truncated SysEx (missing F7)");
    }
    else if (suffix == ".mid")
    {
        juce::MemoryInputStream in(block, false);
        juce::MidiFile mf;
        if (!mf.readFrom(in))
            return failEnv("invalid .mid file");
        for (int t = 0; t < mf.getNumTracks(); ++t)
        {
            const auto* seq = mf.getTrack(t);
            for (int ev = 0; ev < seq->getNumEvents(); ++ev)
            {
                const auto metadata = seq->getEventPointer(ev);
                if (!metadata->message.isSysEx())
                    continue;
                const auto* raw = metadata->message.getRawData();
                dumps.emplace_back(raw, raw + metadata->message.getRawDataSize());
            }
        }
    }
    else return fail("unsupported file type (use .syx or .mid)");
    if (dumps.empty())
        return failEnv("no sysex data found in file");

    size_t totalBytes = 0;
    for (const auto& d : dumps)
    {
        if (auto err = mcp::validateNordDump(d.data(), d.size()); !err.isEmpty())
            return failEnv("invalid Nord dump: " + QString::fromStdString(err.toStdString()));
        totalBytes += d.size();
    }
    if (program > 127 || program < -1)
        return fail("program must be 0..127");

    ProjectCommands::FxMidiParams p;
    p.trackIndex = ti;
    p.slotIndex = si;
    p.captureToTree = captureToTree;
    for (const auto& d : dumps)
    {
        ProjectCommands::FxMidiEvent ev;
        ev.kind = ProjectCommands::FxMidiEvent::Kind::SysEx;
        ev.sysex = d;
        p.events.push_back(std::move(ev));
    }
    if (program >= 0)
    {
        // Voice selection AFTER the bank dumps land (BUG-7 plan step 4).
        ProjectCommands::FxMidiEvent pc;
        pc.kind = ProjectCommands::FxMidiEvent::Kind::ProgramChange;
        pc.channel = 1;
        pc.data1 = program;
        p.events.push_back(std::move(pc));
    }
    {
        // Capture-race protocol: append a harmless CC125 (undefined on the NL2x) at
        // the END of the batch so the trailing slot state is inert. Delivery is
        // paced by the slot drain (<=1 SysEx per block, order preserved) and the
        // deferred state capture is delayed ~30ms per queued SysEx (see sendFxMidi),
        // then confirmed via get_fx_capture_status.
        ProjectCommands::FxMidiEvent cc;
        cc.kind = ProjectCommands::FxMidiEvent::Kind::ControlChange;
        cc.channel = 1;
        cc.data1 = 125;
        cc.data2 = 0;
        p.events.push_back(std::move(cc));
    }
    const auto r = e.getProjectCommands().sendFxMidi(p);
    if (!r.ok)
        return failEnv(QString::fromStdString(r.error));
    return succeed(QJsonObject {
        { "queued", r.queued },
        { "route", "file" },
        { "bytes", static_cast<int>(totalBytes) },
        { "program", program },
        { "capturedToTree", r.capturedToTree },
        { "captureDeferred", true } });
}

} // namespace

bool validMatrixEngineId(const QString& engine)
{
    return validEngineId(engine);
}

QString matrixPresetsDir(QString* error)
{
    const auto& md = resolveMatrixDir();
    if (error != nullptr)
        *error = md.dir.isEmpty() ? md.error : QString();
    return md.dir;
}

QStringList matrixPresetEngines()
{
    const auto& md = resolveMatrixDir();
    if (md.dir.isEmpty())
        return {};
    return availableEngines(md.dir);
}

MatrixOpResult listMatrixPresets(const QString& engine)
{
    if (!validEngineId(engine))
        return fail("engine must match [a-z0-9_] (e.g. je8086, virus, nodalred2x, xenia, vavra)");
    const auto& md = resolveMatrixDir();
    if (md.dir.isEmpty())
        return failEnv(md.error);

    QString err;
    const QJsonDocument doc = loadJsonBounded(md.dir + "/" + engine + ".json", err);
    if (!err.isEmpty())
        // Unknown engine => INVALID PARAMS (-32602), not an environment failure:
        // the caller named a sheet that does not exist. Router_Device sets the same
        // precedent for device.listParams.
        return fail(sheetMissingMessage(engine, md.dir, err));
    const QJsonObject root = doc.object();
    const QString schema = root.value("schema").toString();
    if (!schema.isEmpty() && schema != kSheetSchema)
        return failEnv(QString("%1.json: unsupported schema '%2' (expected %3)")
                           .arg(engine, schema, kSheetSchema));

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

    return succeed(QJsonObject {
        { "engine", engine },
        { "sheet", engine + ".json" },
        { "presets", presets },
        { "morphs", morphs } });
}

MatrixOpResult applyMatrixPreset(AudioEngine& e, const QString& engine, const QString& id,
                                 int ti, int si, bool captureToTree)
{
    if (!validEngineId(engine))
        return fail("engine must match [a-z0-9_] (e.g. je8086, virus, nodalred2x, xenia, vavra)");
    if (id.isEmpty())
        return fail("id required (see list_matrix_presets)");
    const auto& md = resolveMatrixDir();
    if (md.dir.isEmpty())
        return failEnv(md.error);

    QString err;
    const QJsonDocument doc = loadJsonBounded(md.dir + "/" + engine + ".json", err);
    if (!err.isEmpty())
        // Unknown engine => INVALID PARAMS (-32602), not an environment failure:
        // the caller named a sheet that does not exist. Router_Device sets the same
        // precedent for device.listParams.
        return fail(sheetMissingMessage(engine, md.dir, err));
    const QJsonObject root = doc.object();

    // -- 1) sheet preset ids -------------------------------------------------
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

        // 0) Device-native dump route. The sheet may carry a complete,
        //    ready-to-inject SysEx dump per preset (built offline from the same
        //    harvested params by timbre-lib/vavra_matrix_sysex.py via
        //    <engine>_offset_map.json). This is the only route for values the
        //    host-parameter model cannot express: the microQ's FX sub-parameters
        //    share indexes (their meaning is set by Fx2Type), so the wrapper folds
        //    them into single host params with derived children and publishing them
        //    adds no host parameter at all.
        const QJsonArray presetDump = preset.value("sysex").toArray();
        if (!presetDump.isEmpty())
        {
            if (presetDump.size() < 7
                || presetDump.first().toInt() != 0xF0
                || presetDump.last().toInt() != 0xF7)
                return fail(QString("preset '%1' carries a malformed SysEx dump").arg(id));
            ProjectCommands::FxMidiParams dp;
            dp.trackIndex = ti;
            dp.slotIndex = si;
            dp.captureToTree = captureToTree;
            ProjectCommands::FxMidiEvent dev;
            dev.kind = ProjectCommands::FxMidiEvent::Kind::SysEx;
            for (const auto& b : presetDump)
                dev.sysex.push_back(static_cast<uint8_t>(b.toInt() & 0xFF));
            dp.events.push_back(std::move(dev));
            const auto dr = e.getProjectCommands().sendFxMidi(dp);
            if (!dr.ok)
                return failEnv(QString::fromStdString(dr.error));
            return succeed(QJsonObject {
                { "queued", dr.queued },
                { "route", "device_dump" },
                { "bytes", presetDump.size() },
                { "baseSyx", preset.value("baseSyx").toString() },
                { "captureDeferred", true } });
        }

        if (!via.isEmpty() && via != "set_fx_param")
            return fail(QString("preset '%1' appliesVia '%2' has no parameter-level apply path"
                                " (use the engine's native loader; see docs/hardware-va-suite.md)")
                            .arg(id, via));

        const auto slot = resolvePluginSlot(e, ti, si);
        if (!slot.error.isEmpty())
            return fail(slot.error);

        const QString mapPath = md.dir + "/" + engine + "_param_index_map.json";
        QJsonObject engineMap;
        if (QFileInfo::exists(mapPath))
        {
            QString merr;
            const QJsonDocument mdoc = loadJsonBounded(mapPath, merr);
            if (!merr.isEmpty())
                return failEnv(merr);
            engineMap = mdoc.object();
        }
        auto r = applyParamsToSlot(e, ti, slot.pluginId,
                                   preset.value("params").toObject(), {}, engineMap);
        writeAppliedParamOverrides(e, ti, si, r);
        captureAfterParamApply(e, ti, si, r.applied, captureToTree, r);
        return succeed(paramApplyPayload(r));
    }

    // -- 2) morph step ids ('<pair>:step<k>' or the step preset id) ----------
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
        return fail(QString("id '%1' not found in %2 presets or morph steps — call"
                            " list_matrix_presets for valid ids").arg(id, engine));

    const auto slot = resolvePluginSlot(e, ti, si);
    if (!slot.error.isEmpty())
        return fail(slot.error);

    // 2a) injectable SysEx — one message, the send_fx_midi engine path.
    const auto sysex = step.value("sysex").toArray();
    if (!sysex.isEmpty())
    {
        ProjectCommands::FxMidiParams p;
        p.trackIndex = ti;
        p.slotIndex = si;
        p.captureToTree = captureToTree;
        ProjectCommands::FxMidiEvent ev;
        ev.kind = ProjectCommands::FxMidiEvent::Kind::SysEx;
        for (const auto& b : sysex)
            ev.sysex.push_back(static_cast<uint8_t>(b.toInt() & 0xFF));
        p.events.push_back(std::move(ev));
        const auto r = e.getProjectCommands().sendFxMidi(p);
        if (!r.ok)
            return failEnv(QString::fromStdString(r.error));
        return succeed(QJsonObject {
            { "queued", r.queued }, { "captureDeferred", true } });
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
        return applyNordSyxFile(e, ti, si, path, -1, captureToTree);
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
                return failEnv(merr);
            engineMap = mdoc.object();
        }
        auto r = applyParamsToSlot(e, ti, slot.pluginId, stepParams,
                                   step.value("paramIndex").toObject(), engineMap);
        writeAppliedParamOverrides(e, ti, si, r);
        captureAfterParamApply(e, ti, si, r.applied, captureToTree, r);
        return succeed(paramApplyPayload(r));
    }

    return fail(QString("morph step '%1' carries no applicable payload (no sysex, file, or params)")
                    .arg(id));
}

} // namespace HDAW
