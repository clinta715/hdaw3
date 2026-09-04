#include "McpTools.h"
#include "McpTools_Private.h"
#include "McpServer.h"
#include "McpToolDef.h"
#include "../model/ProjectModel.h"
#include "../engine/AudioEngine.h"
#include "../engine/AudioEngineCommands_Helpers.h"
#include "../engine/EnvelopeGenerator.h"
#include "../engine/MainAudioProcessor.h"
#include "../engine/ProjectPool.h"
#include "../engine/TrackFXSlot.h"
#include "../engine/Dx7SysexImport.h"
#include "../engine/FmSynthEngine.h"
#include "../engine/MidiFx.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <algorithm>
#include <optional>

namespace mcp {

namespace {

struct ProbeNote {
    int pitch;
    float velocity;
    double start;
    double duration;
};

int patchRoleDefaultRoot(const QString& role)
{
    if (role == "bass") return 36;
    if (role == "lead") return 72;
    if (role == "pad")  return 48;
    if (role == "stab") return 60;
    if (role == "arp")  return 60;
    if (role == "fx")   return 36;
    if (role == "riser") return 36;
    return 48;
}

// Deterministic role probe phrase in clip-local beats — a C++ translation of
// timbre-lib/sweep_dx7_patches.py build_probe_notes (same shapes and role->
// root defaults; the audition window is fixed at 8 beats, seed fixed at 12345).
// Pitches are clamped to 0..127 by skipping out-of-range notes.
std::vector<ProbeNote> buildPatchProbeNotes(const QString& role, int root,
                                            double windowBeats)
{
    std::vector<ProbeNote> notes;
    auto add = [&](int pitch, double start, double dur, float vel) {
        if (pitch >= 0 && pitch <= 127)
            notes.push_back({pitch, vel, start, dur});
    };

    if (role == "bass")
    {
        add(root, 0.0, windowBeats, 100.0f);
    }
    else if (role == "lead")
    {
        add(root, 0.0, windowBeats, 100.0f);
        add(root + 4, 0.0, windowBeats, 90.0f);
    }
    else if (role == "pad")
    {
        add(root, 0.0, windowBeats, 100.0f);
        add(root + 7, 0.0, windowBeats, 90.0f);
        add(root + 12, 0.0, windowBeats, 80.0f);
    }
    else if (role == "stab")
    {
        for (double beat = 0.0; beat < windowBeats; beat += 2.0)
        {
            add(root, beat, 0.5, 100.0f);
            add(root + 7, beat, 0.5, 100.0f);
            add(root + 12, beat, 0.5, 100.0f);
        }
    }
    else if (role == "arp")
    {
        static const int seq[] = { 0, 3, 7, 12, 7, 3 };
        double t = 0.0;
        int i = 0;
        while (t < windowBeats)
        {
            add(root + seq[i % 6], t, 0.4, 100.0f);
            t += 0.5;
            ++i;
        }
    }
    else
    {
        // riser / fx / unknown: 16-step rising gliss (the Python fallback,
        // which also doubles as the percussive-role placeholder).
        for (int k = 0; k < 16; ++k)
        {
            const double t = static_cast<double>(k);
            if (t >= windowBeats) break;
            add(root + k, t, 0.8, 100.0f);
        }
    }
    return notes;
}

} // namespace

void registerFxSlotTools(McpServer& s, AudioEngine* e)
{

s.registerTool({"add_fx",
        "Add an FX slot. fxType in {eq,compressor,reverb,delay,chorus,flanger,phaser,filter,saturator,sampler,fm_synth,growl_bass,psyarp,psy_fm,sub_synth}, OR a pluginId.",
        objSchema({{"trackId",  QJsonObject{{"type","integer"}}},
                  {"fxType",   QJsonObject{{"type","string"},
                      {"enum", QJsonArray{"eq","compressor","reverb","delay","chorus","flanger","phaser","filter","saturator","sampler","fm_synth","growl_bass","psyarp","psy_fm","sub_synth"}}}},
                   {"pluginId", QJsonObject{{"type","string"}}},
                   {"position", QJsonObject{{"type","integer"}}}}, {"trackId"}),
        "fx",
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            auto tl = e->getProjectModel().getTrackListTree();
            if (ti < 0 || ti >= tl.getNumChildren())
                return McpToolResult::text("track not found", true);
            std::string type = a.value("fxType").toString().toStdString();
            if (type.empty() && a.contains("pluginId")) type = "plugin";
            std::string pluginId;
            if (a.contains("pluginId")) pluginId = a.value("pluginId").toString().toStdString();
            int pos = a.value("position").toInt(-1);
            auto fxChain = tl.getChild(ti).getChildWithName(IDs::FX_CHAIN);
            int n = fxChain.isValid() ? fxChain.getNumChildren() : 0;
            int idx = (pos < 0 || pos > n) ? n : pos;
            e->getProjectCommands().addFxSlot(ti, type, pos, pluginId);
            return McpToolResult::text(QString("slot=%1").arg(idx));
        }});

s.registerTool({"remove_fx", "Remove an FX slot (destructive).",
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                  {"slotIndex", QJsonObject{{"type","integer"}}},
                  {"dryRun",    QJsonObject{{"type","boolean"}}}}, {"trackId","slotIndex"}),
        "fx",
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            auto tl = e->getProjectModel().getTrackListTree();
            if (ti < 0 || ti >= tl.getNumChildren())
                return McpToolResult::text("track not found", true);
            int s = a.value("slotIndex").toInt();
            if (a.value("dryRun").toBool(false))
                return McpToolResult::text(QString("would remove FX slot %1 on track %2").arg(s).arg(ti));
            e->getProjectCommands().removeFxSlot(ti, s);
            return McpToolResult::text("ok");
        }});

s.registerTool({"set_fx_bypass", "Bypass or unbypass an FX slot.",
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                  {"slotIndex", QJsonObject{{"type","integer"}}},
                  {"bypassed",  QJsonObject{{"type","boolean"}}}}, {"trackId","slotIndex","bypassed"}),
        "fx",
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            int si = a.value("slotIndex").toInt();
            e->getProjectCommands().setFxSlotBypassed(ti, si, a.value("bypassed").toBool());
            return McpToolResult::text("ok");
        }});

s.registerTool({"restart_fx", "Restart a crashed isolated plugin FX slot.",
        objSchema({{"trackIndex", QJsonObject{{"type","integer"}}},
                  {"slotIndex",  QJsonObject{{"type","integer"}}}}, {"trackIndex","slotIndex"}),
        "fx",
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackIndex").toInt();
            int si = a.value("slotIndex").toInt();
            auto tl = e->getProjectModel().getTrackListTree();
            if (ti < 0 || ti >= tl.getNumChildren())
                return McpToolResult::text("track not found", true);
            e->getProjectCommands().respawnFxSlot(ti, si);
            return McpToolResult::text("ok");
        }});

s.registerTool({"list_fx_params", "List all automatable parameters of an FX slot. Works for both plugin and internal FX (eq, compressor, reverb, delay, chorus, flanger, phaser, filter, saturator, sampler, fm_synth, growl_bass, psyarp, psy_fm, sub_synth).",
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                  {"slotIndex", QJsonObject{{"type","integer"}}}}, {"trackId","slotIndex"}),
        "fx",
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            int si = a.value("slotIndex").toInt();
            auto fxSlots = e->getReadModel().getFxSlots(ti);
            if (si < 0 || si >= (int)fxSlots.size())
                return McpToolResult::text("slot not found", true);
            if (fxSlots[si].fxType == "none")
                return McpToolResult::text("slot is empty", true);

            QJsonArray arr;
            if (fxSlots[si].fxType == "plugin")
            {
                auto params = e->getPluginParamService().getParams(ti, fxSlots[si].pluginId);
                for (const auto& pi : params) {
                    QJsonObject o;
                    o["index"] = pi.index;
                    o["name"] = QString::fromStdString(pi.name);
                    o["automatable"] = pi.automatable;
                    o["value"] = static_cast<double>(pi.value);
                    o["text"] = QString::fromStdString(pi.text);
                    o["paramID"] = 100 + si * 100 + pi.index;
                    arr.append(o);
                }
            }
            else
            {
                // Internal FX: enumerate from param definitions. Include the
                // CURRENT real-unit value alongside the metadata (P1.3, plan
                // 2026-08-30) — symmetric with the plugin branch above; the
                // value comes from the ValueTree param_N props (source of
                // truth, same storage the engine loads + list_fx_params reads).
                auto defs = HDAW::TrackFXSlot::getParamDefsForType(fxSlots[si].fxType);
                auto snaps = e->getReadModel().getInternalFxParams(ti, si);
                for (const auto& def : defs) {
                    QJsonObject o;
                    o["index"] = def.index;
                    o["name"] = QString::fromUtf8(def.name.toRawUTF8());
                    o["automatable"] = true;
                    for (const auto& snap : snaps)
                        if (snap.paramIndex == def.index)
                            { o["value"] = static_cast<double>(snap.value); break; }
                    o["minValue"] = static_cast<double>(def.minValue);
                    o["maxValue"] = static_cast<double>(def.maxValue);
                    o["defaultValue"] = static_cast<double>(def.defaultValue);
                    o["paramID"] = 100 + si * 100 + def.index;
                    arr.append(o);
                }
            }
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(QJsonObject{{"params", arr}}).toJson(QJsonDocument::Compact)));
        }});

s.registerTool({"set_fx_param", "Set an FX parameter value (normalized 0..1). Works for both plugin and internal FX (eq, compressor, reverb, delay, chorus, flanger, phaser, filter, saturator, sampler, fm_synth, growl_bass, psyarp, psy_fm, sub_synth).",
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                  {"slotIndex", QJsonObject{{"type","integer"}}},
                  {"paramIndex",QJsonObject{{"type","integer"}}},
                  {"value",     QJsonObject{{"type","number"}}}}, {"trackId","slotIndex","paramIndex","value"}),
        "fx",
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            int si = a.value("slotIndex").toInt();
            auto fxSlots = e->getReadModel().getFxSlots(ti);
            if (si < 0 || si >= (int)fxSlots.size())
                return McpToolResult::text("slot not found", true);
            if (fxSlots[si].fxType == "none")
                return McpToolResult::text("slot is empty", true);
            int pi = a.value("paramIndex").toInt();
            float v = static_cast<float>(a.value("value").toDouble());
            v = std::clamp(v, 0.0f, 1.0f);

            if (fxSlots[si].fxType == "plugin")
            {
                auto params = e->getPluginParamService().getParams(ti, fxSlots[si].pluginId);
                if (pi < 0 || pi >= static_cast<int>(params.size()))
                    return McpToolResult::text("param index out of range", true);
                e->getPluginParamService().setParam(ti, fxSlots[si].pluginId, pi, v);
            }
            else
            {
                // Internal FX: route through the command layer which sets the
                // ValueTree property, triggering the listener to apply to DSP.
                // The ValueTree stores real values, so denormalize first.
                auto defs = HDAW::TrackFXSlot::getParamDefsForType(fxSlots[si].fxType);
                if (pi < 0 || pi >= static_cast<int>(defs.size()))
                    return McpToolResult::text("param index out of range", true);
                float realValue = defs[static_cast<size_t>(pi)].minValue
                    + v * (defs[static_cast<size_t>(pi)].maxValue - defs[static_cast<size_t>(pi)].minValue);
                e->getProjectCommands().setFxSlotParam(ti, si, pi, realValue);
            }
            return McpToolResult::text("ok");
        }});

s.registerTool({"set_internal_fx_param",
        "Set an internal (non-plugin) FX parameter value. Works for eq, compressor, reverb, delay, chorus, flanger, phaser, filter, saturator, sampler, fm_synth, growl_bass, psyarp, psy_fm, and sub_synth.",
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                  {"slotIndex", QJsonObject{{"type","integer"}}},
                  {"paramIndex",QJsonObject{{"type","integer"}}},
                  {"value",     QJsonObject{{"type","number"}}}}, {"trackId","slotIndex","paramIndex","value"}),
        "fx",
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            int si = a.value("slotIndex").toInt();
            auto fxSlots = e->getReadModel().getFxSlots(ti);
            if (si < 0 || si >= static_cast<int>(fxSlots.size()))
                return McpToolResult::text("slot not found", true);
            if (fxSlots[si].fxType == "plugin" || fxSlots[si].fxType == "none")
                return McpToolResult::text("slot is not an internal FX", true);
            int pi = a.value("paramIndex").toInt();
            // Gate 9: validate against the type's real-unit defs table — an
            // out-of-range index must be an error, never a stray param_N
            // property write (existing set_fx_param behavior).
            auto defs = HDAW::TrackFXSlot::getParamDefsForType(fxSlots[si].fxType);
            if (pi < 0 || pi >= static_cast<int>(defs.size()))
                return McpToolResult::text("param index out of range", true);
            float v = static_cast<float>(a.value("value").toDouble());
            e->getProjectCommands().setFxSlotParam(ti, si, pi, v);
            return McpToolResult::text("ok");
        }});

s.registerTool({"get_internal_fx_param",
        "Read back the CURRENT value of an internal (non-plugin) FX slot's parameters in REAL units — the verification complement to set_internal_fx_param. Works for eq, compressor, reverb, delay, chorus, flanger, phaser, filter, saturator, sampler, fm_synth, growl_bass, psyarp, psy_fm, and sub_synth. Returns {params:[{index,name,value,defaultValue,minValue,maxValue}]}; untouched params report their default value. Reads the project ValueTree (source of truth — no render, no DSP access, read-only).",
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                  {"slotIndex", QJsonObject{{"type","integer"}}}}, {"trackId","slotIndex"}),
        "fx",
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            int si = a.value("slotIndex").toInt();
            auto fxSlots = e->getReadModel().getFxSlots(ti);
            if (si < 0 || si >= static_cast<int>(fxSlots.size()))
                return McpToolResult::text("get_internal_fx_param: slot not found", true);
            // Gate 9: validate against the type's real-unit defs table — a
            // plugin, empty, or unknown slot has no defs and must error, never
            // return an empty/meaningless dump.
            if (HDAW::TrackFXSlot::getParamDefsForType(fxSlots[si].fxType).empty())
                return McpToolResult::text("get_internal_fx_param: slot is not an internal FX", true);
            auto snaps = e->getReadModel().getInternalFxParams(ti, si);
            QJsonArray arr;
            for (const auto& s : snaps)
            {
                QJsonObject o;
                o["index"]         = s.paramIndex;
                o["name"]          = QString::fromStdString(s.name);
                o["value"]         = static_cast<double>(s.value);
                o["defaultValue"]  = static_cast<double>(s.defaultValue);
                o["minValue"]      = static_cast<double>(s.minValue);
                o["maxValue"]      = static_cast<double>(s.maxValue);
                arr.append(o);
            }
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(QJsonObject{{"params", arr}}).toJson(QJsonDocument::Compact)));
        }});

s.registerTool({"sub_synth_import_sysex",
        "Import an Access Virus SysEx patch into a sub_synth FX slot. Supports B/C "
        "single dumps (267 bytes) and TI banks (128 x 524-byte blocks). For banks, "
        "loads voiceIndex (default 0). Maps the Virus patch onto the sub_synth "
        "params 0-22 in real units (cutoff/envelopes/levels/waves/...); Virus "
        "features with no sub_synth equivalent (FM, ring mod, LFOs, keytrack, FX, "
        "mod matrix, noise) are reported in 'unmapped' — never silently dropped. "
        "On a bad file/slot/checksum the slot is left unchanged.",
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                   {"slotIndex", QJsonObject{{"type","integer"}}},
                   {"filePath",  QJsonObject{{"type","string"}}},
                   {"voiceIndex",QJsonObject{{"type","integer"}}}},
                   {"trackId","slotIndex","filePath"}),
        "fx",
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            int si = a.value("slotIndex").toInt();
            auto fxSlots = e->getReadModel().getFxSlots(ti);
            if (si < 0 || si >= (int)fxSlots.size())
                return McpToolResult::text("slot not found", true);
            if (fxSlots[si].fxType != "sub_synth")
                return McpToolResult::text("slot is not a sub_synth", true);

            QString filePath = a.value("filePath").toString();
            if (filePath.isEmpty())
                return McpToolResult::text("filePath required", true);
            juce::File syxFile(filePath.toStdString());
            if (!syxFile.existsAsFile())
                return McpToolResult::text("file not found: " + filePath, true);

            const int vi = a.value("voiceIndex").toInt(0);
            auto r = e->getAudioEngineCommands().loadVirusPatch(
                ti, si, filePath.toStdString(), vi);
            if (!r.ok)
                return McpToolResult::text(QString::fromStdString(r.error), true);

            QJsonObject result;
            result["ok"] = true;
            result["name"] = QString::fromStdString(r.name);
            result["bank"] = r.bank;
            result["program"] = r.program;
            result["mappedCount"] = r.mappedCount;
            QJsonArray unmapped;
            for (const auto& u : r.unmapped)
                unmapped.append(QString::fromStdString(u));
            result["unmapped"] = unmapped;
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(result).toJson(QJsonDocument::Compact)));
        }});

s.registerTool({"audition_patch",
        "Load a synth patch file into a probe FX slot and place a role-appropriate "
        "probe MIDI clip, so pressing play on the probe track auditions the patch. "
        "Virus patches (.syx/.mid/.vhc) load via sub_synth; DX7 patches (.syx) via "
        "fm_synth. Creates a probe track when trackId is omitted. engine is inferred "
        "from the file when omitted: the sidecar engine key, else the DX7 header "
        "(F0 43) -> fm_synth, else the Access header (F0 00 20 33) -> sub_synth, "
        "else an error. role picks the probe phrase root (bass 36 / lead 72 / pad 48 / "
        "stab 60 / arp 60 / fx 36 / riser 36; default pad); root overrides it. Live "
        "audition only — no offline render. Returns {ok, trackId, slotIndex, name, engine, role}.",
        objSchema({{"path",   QJsonObject{{"type","string"}}},
                   {"engine", QJsonObject{{"type","string"},
                       {"enum", QJsonArray{"sub_synth","fm_synth"}}}},
                   {"role",   QJsonObject{{"type","string"}}},
                   {"root",   QJsonObject{{"type","integer"},{"minimum",0},{"maximum",127}}},
                   {"trackId",QJsonObject{{"type","integer"}}}},
                  {"path"}),
        "fx",
        [e](const QJsonObject& a) -> McpToolResult {
            QString filePath = a.value("path").toString();
            if (filePath.isEmpty())
                return McpToolResult::text("path required", true);
            juce::File patchFile(filePath.toStdString());
            if (!patchFile.existsAsFile())
                return McpToolResult::text("file not found: " + filePath, true);

            // ── resolve engine: explicit arg, else sidecar engine key, else header ──
            std::string engine = a.value("engine").toString().toStdString();
            if (engine.empty())
            {
                auto sidecarEngine = [](const juce::File& sc) -> std::string {
                    if (!sc.existsAsFile()) return {};
                    auto j = juce::JSON::parse(sc.loadFileAsString());
                    auto* o = j.getDynamicObject();
                    return o ? o->getProperty("engine").toString().toStdString()
                             : std::string();
                };
                engine = sidecarEngine(juce::File(patchFile.getFullPathName() + ".virus.json"));
                if (engine.empty())
                    engine = sidecarEngine(juce::File(patchFile.getFullPathName() + ".dx7.json"));
                if (engine.empty())
                {
                    juce::MemoryBlock raw;
                    if (patchFile.loadFileAsData(raw))
                    {
                        const auto* b = static_cast<const uint8_t*>(raw.getData());
                        const size_t n = raw.getSize();
                        if (n >= 2 && b[0] == 0xF0 && b[1] == 0x43)
                            engine = "fm_synth";
                        else if (n >= 5 && b[0] == 0xF0 && b[1] == 0x00
                                 && b[2] == 0x20 && b[3] == 0x33)
                            engine = "sub_synth";
                    }
                }
                if (engine.empty())
                    return McpToolResult::text(
                        "could not determine patch engine — pass engine explicitly "
                        "(sub_synth or fm_synth)", true);
            }

            // ── probe track (or reuse trackId) ──
            auto& m = e->getProjectModel();
            auto tl = m.getTrackListTree();
            int trackId = a.contains("trackId") ? a.value("trackId").toInt() : -1;
            if (trackId < 0 || trackId >= tl.getNumChildren())
            {
                const int idx = tl.getNumChildren();
                juce::ValueTree t(IDs::TRACK);
                t.setProperty(IDs::name, "Patch Probe", nullptr);
                t.setProperty(IDs::volume, 0.85, nullptr);
                t.setProperty(IDs::pan, 0.0, nullptr);
                t.setProperty(IDs::isMuted, false, nullptr);
                t.setProperty(IDs::isSoloed, false, nullptr);
                t.setProperty(IDs::parentBus, 0, nullptr);
                t.setProperty(IDs::color, static_cast<int>(
                    ProjectModel::trackColorForIndex(idx)), nullptr);
                t.addChild(juce::ValueTree(IDs::CLIP_LIST), -1, nullptr);
                t.addChild(juce::ValueTree(IDs::FX_CHAIN), -1, nullptr);
                t.addChild(ProjectModel::createTrackAutomationList(), -1, nullptr);
                tl.addChild(t, -1, &m.getUndoManager());
                trackId = idx;
            }

            // ── synth slot of the engine type ──
            e->getProjectCommands().addFxSlot(trackId, engine, -1, "");
            auto fxChain = tl.getChild(trackId).getChildWithName(IDs::FX_CHAIN);
            const int n = fxChain.isValid() ? fxChain.getNumChildren() : 0;
            const int slotIndex = n > 0 ? n - 1 : 0;

            // ── load the patch ──
            QString name = QString::fromUtf8(patchFile.getFileName().toRawUTF8());
            if (engine == "sub_synth")
            {
                auto r = e->getAudioEngineCommands().loadVirusPatch(
                    trackId, slotIndex, filePath.toStdString(), 0);
                if (!r.ok)
                    return McpToolResult::text(QString::fromStdString(r.error), true);
                if (!r.name.empty())
                    name = QString::fromStdString(r.name);
            }
            else if (engine == "fm_synth")
            {
                juce::MemoryBlock raw;
                if (!patchFile.loadFileAsData(raw))
                    return McpToolResult::text("failed to read file", true);
                const auto* bytes = static_cast<const uint8_t*>(raw.getData());
                const size_t fileSize = raw.getSize();
                std::optional<HDAW::Dx7Voice> voice;
                if (fileSize >= 163 && bytes[0] == 0xF0 && bytes[1] == 0x43
                    && bytes[3] == 0x00)
                    voice = HDAW::parseSingleVoiceSysex(bytes, fileSize);
                else if (fileSize >= 4104 && bytes[0] == 0xF0 && bytes[1] == 0x43
                         && bytes[3] == 0x09)
                {
                    auto voices = HDAW::parseCartridgeSysex(bytes, fileSize);
                    if (!voices.empty()) voice = voices[0];
                }
                else
                {
                    return McpToolResult::text(
                        "not a recognized DX7 SysEx file (expected F0 43 00 00 or F0 43 00 09 header)", true);
                }
                if (!voice.has_value())
                    return McpToolResult::text("failed to parse SysEx data (bad checksum or size)", true);
                juce::MemoryBlock block(voice->patchData.data(), FmSynthEngine::kPatchSize);
                e->getProjectCommands().setFmPatch(trackId, slotIndex,
                    block.toBase64Encoding().toStdString());
                if (!voice->voiceName.empty())
                    name = QString::fromStdString(voice->voiceName);
            }
            else
            {
                return McpToolResult::text("unsupported engine: " + QString::fromStdString(engine), true);
            }

            // ── role probe phrase clip ──
            QString role = a.value("role").toString("pad");
            const int root = a.contains("root") ? a.value("root").toInt()
                                                : patchRoleDefaultRoot(role);
            constexpr double kWindowBeats = 8.0;
            const double bpm = m.getTree().getProperty(IDs::tempo, 120.0);
            const double durSec = HDAW::beatsToSeconds(kWindowBeats, bpm);
            auto clip = m.createMidiClipEmpty("Patch Probe", 0.0, durSec);
            clip.setProperty(IDs::color, static_cast<int>(
                ProjectModel::trackColorForIndex(trackId)), nullptr);
            auto nl = clip.getChildWithName(IDs::MIDI_NOTE_LIST);
            for (const auto& note : buildPatchProbeNotes(role, root, kWindowBeats))
                nl.addChild(m.createMidiNote(note.pitch, note.velocity / 127.0f,
                                             note.start, note.duration), -1, nullptr);
            tl.getChild(trackId).getChildWithName(IDs::CLIP_LIST).addChild(clip, -1,
                &m.getUndoManager());

            // ── sync the live processor (Gate 2/6): the probe track must be in
            // the routing graph with the loaded slot so pressing play is audible
            // and the live slot values reflect the patch. rebuildRoutingGraph
            // restores param_N / fmPatchData from the tree (Gate 1/10 path).
            if (auto* proc = e->getMainProcessor())
                proc->rebuildRoutingGraph();

            QJsonObject result;
            result["ok"] = true;
            result["trackId"] = trackId;
            result["slotIndex"] = slotIndex;
            result["name"] = name;
            result["engine"] = QString::fromStdString(engine);
            result["role"] = role;
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(result).toJson(QJsonDocument::Compact)));
        }});

}

} // namespace mcp
