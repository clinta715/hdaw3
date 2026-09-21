#include "McpTools.h"
#include "McpTools_Private.h"
#include "PresetFileParser.h"
#include "PresetRoute.h"
#include "McpServer.h"
#include "McpToolDef.h"
#include "../model/ProjectModel.h"
#include "../common/MasterFxDefs.h"
#include "../common/FxCaptureStatus.h"
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

// Deterministic role probe phrase in clip-local beats â€” a C++ translation of
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

// M4 (Modular Dawn audit): resolve a param NAME to its index against the
// defs list list_fx_params exposes. Returns -1 when not found.
int internalParamIndexByName(const std::vector<HDAW::TrackFXSlot::InternalParamDef>& defs,
                             const QString& name)
{
    for (const auto& def : defs)
        if (QString::fromUtf8(def.name.toRawUTF8()).compare(name, Qt::CaseInsensitive) == 0)
            return def.index;
    return -1;
}

int masterParamIndexByName(const std::vector<HDAW::MasterFxParamDef>& defs,
                           const QString& name)
{
    for (size_t i = 0; i < defs.size(); ++i)
        if (QString::fromUtf8(defs[i].name).compare(name, Qt::CaseInsensitive) == 0)
            return static_cast<int>(i);
    return -1;
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

s.registerTool({"toggle_plugin_editor", "Open or close the plugin editor window for an FX slot (toggles). Use with windows-mcp/cua-driver to drive the plugin's own UI.",
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                  {"slotIndex", QJsonObject{{"type","integer"}}}}, {"trackId","slotIndex"}),
        "fx",
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            int si = a.value("slotIndex").toInt();
            auto fxSlots = e->getReadModel().getFxSlots(ti);
            if (si < 0 || si >= (int)fxSlots.size())
                return McpToolResult::text("slot not found", true);
            if (fxSlots[si].fxType != "plugin")
                return McpToolResult::text("slot is not a plugin", true);
            e->getAudioGraphCommands().toggleFXEditor(ti, si);
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

s.registerTool({"list_fx_params", "List all automatable parameters of an FX slot. Works for both plugin and internal FX (eq, compressor, reverb, delay, chorus, flanger, phaser, filter, saturator, sampler, fm_synth, growl_bass, psyarp, psy_fm, sub_synth). Plugin (CLAP) params additionally report hasRange/minVal/maxVal/defaultVal/plainValue/stepped plus minText/maxText/defaultText (real units) so writes can be mapped meaningfully; hasRange=false means blind normalized 0..1 (VST3, older children).",
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
                auto& paramSvc = e->getPluginParamService();
                // Persisted offline-replay overrides (IDs::appliedParamOverrides).
                // An `overridden` index survives into tree-copy renders
                // (export_audio / audition_plugin / verify_part) and save/load;
                // a non-overridden index is LIVE-MONITORING ONLY.
                const auto overrides = e->getProjectCommands().getPluginParamOverrides(ti, si);
                for (const auto& pi : params) {
                    QJsonObject o;
                    o["index"] = pi.index;
                    o["name"] = QString::fromStdString(pi.name);
                    o["automatable"] = pi.automatable;
                    o["value"] = static_cast<double>(pi.value);
                    o["text"] = QString::fromStdString(pi.text);
                    for (const auto& ov : overrides)
                        if (ov.first == pi.index) { o["overridden"] = true; break; }
                    o["hasRange"] = pi.hasRange;
                    if (pi.hasRange)
                    {
                        o["minVal"] = pi.minVal;
                        o["maxVal"] = pi.maxVal;
                        o["defaultVal"] = pi.defaultVal;
                        o["plainValue"] = pi.plainValue;
                        o["stepped"] = pi.stepped;
                        const double range = pi.maxVal - pi.minVal;
                        const double dflt = range > 0.0 ? (pi.defaultVal - pi.minVal) / range : 0.0;
                        o["minText"] = QString::fromStdString(
                            paramSvc.getParamText(ti, fxSlots[si].pluginId, pi.index, 0.0f));
                        o["maxText"] = QString::fromStdString(
                            paramSvc.getParamText(ti, fxSlots[si].pluginId, pi.index, 1.0f));
                        o["defaultText"] = QString::fromStdString(
                            paramSvc.getParamText(ti, fxSlots[si].pluginId, pi.index, static_cast<float>(dflt)));
                    }
                    o["paramID"] = 100 + si * 100 + pi.index;
                    arr.append(o);
                }
            }
            else
            {
                // Internal FX: enumerate from param definitions. Include the
                // CURRENT real-unit value alongside the metadata (P1.3, plan
                // 2026-08-30) â€” symmetric with the plugin branch above; the
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

s.registerTool({"set_fx_param", "Set an FX parameter value (normalized 0..1) by paramIndex or paramName (the name list_fx_params returns; case-insensitive, paramName wins when both are given). Works for both plugin and internal FX (eq, compressor, reverb, delay, chorus, flanger, phaser, filter, saturator, sampler, fm_synth, growl_bass, psyarp, psy_fm, sub_synth). For PLUGIN slots the write is live AND persisted as a slot-level offline-replay override (returned as 'ok overrides=N'), so it also reaches export_audio / audition_plugin / verify_part renders and save/load; list_fx_params marks such params 'overridden', clear_fx_param_overrides removes them. For INTERNAL FX the ValueTree param_N property is the durable source.",
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                  {"slotIndex", QJsonObject{{"type","integer"}}},
                  {"paramIndex",QJsonObject{{"type","integer"}}},
                  {"paramName", QJsonObject{{"type","string"}}},
                  {"value",     QJsonObject{{"type","number"}}}}, {"trackId","slotIndex","value"}),
        "fx",
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            int si = a.value("slotIndex").toInt();
            auto fxSlots = e->getReadModel().getFxSlots(ti);
            if (si < 0 || si >= (int)fxSlots.size())
                return McpToolResult::text("slot not found", true);
            if (fxSlots[si].fxType == "none")
                return McpToolResult::text("slot is empty", true);
            const bool hasName = a.contains("paramName") && !a.value("paramName").toString().isEmpty();
            if (!hasName && !a.contains("paramIndex"))
                return McpToolResult::text("paramIndex or paramName required", true);
            int pi = a.value("paramIndex").toInt();
            float v = static_cast<float>(a.value("value").toDouble());
            v = std::clamp(v, 0.0f, 1.0f);

            if (fxSlots[si].fxType == "plugin")
            {
                auto params = e->getPluginParamService().getParams(ti, fxSlots[si].pluginId);
                if (hasName)
                {
                    pi = -1;
                    const QString wantName = a.value("paramName").toString();
                    for (const auto& p : params)
                        if (QString::fromStdString(p.name).compare(wantName, Qt::CaseInsensitive) == 0)
                            { pi = p.index; break; }
                    if (pi < 0)
                        return McpToolResult::text("unknown paramName: " + wantName, true);
                }
                // Bounds: the live param list is authoritative when the instance
                // resolves. An EMPTY list means no live instance (deviceless, or
                // not settled yet): the write is then persisted for the offline
                // replay without a range check — the replay reports out-of-range
                // entries as skippedBeyondCache rather than dropping silently.
                if (pi < 0 || (!params.empty() && pi >= static_cast<int>(params.size())))
                    return McpToolResult::text("param index out of range", true);
                // Shared command layer: live write + durable ledger so
                // tree-copy renders and save/load see it. A bare
                // PluginParamService::setParam reaches the LIVE child only.
                // See docs/plans/2026-09-21-vavra-live-param-delivery.md.
                const int overrides = e->getProjectCommands().setPluginParam(ti, si, pi, v);
                if (overrides < 0)
                    return McpToolResult::text("slot is not a plugin slot", true);
                return McpToolResult::text(
                    "ok overrides=" + QString::number(overrides));
            }
            else
            {
                // Internal FX: route through the command layer which sets the
                // ValueTree property, triggering the listener to apply to DSP.
                // The ValueTree stores real values, so denormalize first.
                auto defs = HDAW::TrackFXSlot::getParamDefsForType(fxSlots[si].fxType);
                if (hasName)
                {
                    pi = internalParamIndexByName(defs, a.value("paramName").toString());
                    if (pi < 0)
                        return McpToolResult::text("unknown paramName: " + a.value("paramName").toString(), true);
                }
                if (pi < 0 || pi >= static_cast<int>(defs.size()))
                    return McpToolResult::text("param index out of range", true);
                float realValue = defs[static_cast<size_t>(pi)].minValue
                    + v * (defs[static_cast<size_t>(pi)].maxValue - defs[static_cast<size_t>(pi)].minValue);
                e->getProjectCommands().setFxSlotParam(ti, si, pi, realValue);
            }
            return McpToolResult::text("ok");
        }});

s.registerTool({"clear_fx_param_overrides",
    "Drop every persisted plugin-parameter override for ONE FX slot (the offline-replay ledger written by set_fx_param on plugin slots). Use it to return a slot to 'what the plugin state itself says' before an export/audition — without it, a param set once keeps being replayed into every later tree-copy render. Returns {removed:N}. No effect on internal FX (their param_N properties are the source of truth).",
    objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
              {"slotIndex", QJsonObject{{"type","integer"}}}}, {"trackId","slotIndex"}),
    "fx",
    [e](const QJsonObject& a) -> McpToolResult {
        const int ti = a.value("trackId").toInt();
        const int si = a.value("slotIndex").toInt();
        auto fxSlots = e->getReadModel().getFxSlots(ti);
        if (si < 0 || si >= static_cast<int>(fxSlots.size()))
            return McpToolResult::text("slot not found", true);
        const int removed = e->getProjectCommands().clearPluginParamOverrides(ti, si);
        if (removed < 0)
            return McpToolResult::text("slot not found", true);
        return McpToolResult::text(
            QString::fromUtf8(QJsonDocument(QJsonObject{{"removed", removed}})
                .toJson(QJsonDocument::Compact)));
    }});

s.registerTool({"send_fx_midi",
    "Queue short MIDI messages (programChange/controlChange/noteOn/noteOff/sysEx) into a plugin FX slot's NEXT processed block of the LIVE plugin instance. Loads MIDI-selectable presets â€” e.g. gearmulator Virus plugins: controlChange controller=0 value=bank (0-7 = banks A-H singles) then programChange program=patch. DX7 voices: use fm_synth_import_sysex into an fm_synth slot (plugin slots ignore injected SysEx). Realtime mutation: not undoable. The changed preset reaches offline exports once captured via project save.",
    objSchema({{"trackId",  QJsonObject{{"type","integer"}}},
              {"slotIndex",QJsonObject{{"type","integer"}}},
              {"captureToTree", QJsonObject{{"type","boolean"}}},
              {"messages", QJsonObject{{"type","array"}}}},
              {"trackId","slotIndex","messages"}),
    "fx",
    [e](const QJsonObject& a) -> McpToolResult {
        ProjectCommands::FxMidiParams p;
        p.trackIndex = a.value("trackId").toInt();
        p.slotIndex = a.value("slotIndex").toInt();
        p.captureToTree = a.value("captureToTree").toBool(true);
        const auto msgs = a.value("messages").toArray();
        if (msgs.isEmpty())
            return McpToolResult::text("messages array is empty", true);
        for (const auto& mv : msgs)
        {
            const auto m = mv.toObject();
            const std::string kind = m.value("kind").toString().toStdString();
            ProjectCommands::FxMidiEvent ev;
            ev.channel = m.value("channel").toInt(1);
            if (kind == "programChange") { ev.kind = ProjectCommands::FxMidiEvent::Kind::ProgramChange; ev.data1 = m.value("program").toInt(0); }
            else if (kind == "controlChange") { ev.kind = ProjectCommands::FxMidiEvent::Kind::ControlChange; ev.data1 = m.value("controller").toInt(0); ev.data2 = m.value("value").toInt(0); }
            else if (kind == "noteOn") { ev.kind = ProjectCommands::FxMidiEvent::Kind::NoteOn; ev.data1 = m.value("pitch").toInt(60); ev.data2 = m.value("velocity").toInt(100); }
            else if (kind == "noteOff") { ev.kind = ProjectCommands::FxMidiEvent::Kind::NoteOff; ev.data1 = m.value("pitch").toInt(60); ev.data2 = m.value("velocity").toInt(0); }
            else if (kind == "sysEx") {
                ev.kind = ProjectCommands::FxMidiEvent::Kind::SysEx;
                const auto byteArr = m.value("bytes").toArray();
                for (const auto& b : byteArr)
                    ev.sysex.push_back(static_cast<uint8_t>(b.toInt()));
            }
            else return McpToolResult::text("unknown message kind: " + QString::fromStdString(kind), true);
            p.events.push_back(ev);
        }
        auto r = e->getProjectCommands().sendFxMidi(p);
        if (!r.ok)
            return McpToolResult::text(QString::fromStdString(r.error), true);
        return McpToolResult::text(QString("queued=%1 capturedToTree=%2%3")
            .arg(r.queued)
            .arg(r.capturedToTree ? 1 : 0)
            .arg(r.note.empty() ? QString() : QString(" note=") + QString::fromStdString(r.note)));
    }});

s.registerTool({"get_fx_capture_status",
    "Report the deferred plugin-state capture receipt for ONE FX slot (see send_fx_midi / load_nord_bank / load_virus_preset / apply_preset): {status, stateBytes, capturedAtMs, hasPluginState}. status is pending while the deferred capture is in flight, ok when the injected preset landed in IDs::pluginState, or failed:<reason>. Poll this after a bank load instead of trusting the immediate capturedToTree=0 (the realtime capture completes after the call returns).",
    objSchema({{"trackId",  QJsonObject{{"type","integer"}}},
              {"slotIndex",QJsonObject{{"type","integer"}}}},
             {"trackId","slotIndex"}),
    "fx",
    [e](const QJsonObject& a) -> McpToolResult {
        const int ti = a.value("trackId").toInt();
        const int si = a.value("slotIndex").toInt();
        auto slotTree = e->getProjectModel().getTrackListTree()
            .getChild(ti).getChildWithName(IDs::FX_CHAIN).getChild(si);
        if (!slotTree.isValid())
            return McpToolResult::text("slot not found in tree", true);
        // The receipt read lives in src/common/FxCaptureStatus.cpp — shared with the
        // RPC method audio.getFxCaptureStatus. Output text is unchanged.
        const auto st = HDAW::readFxCaptureStatus(slotTree);
        return McpToolResult::text(QString("status=%1 stateBytes=%2 capturedAtMs=%3 hasPluginState=%4")
            .arg(st.status).arg(st.stateBytes).arg(st.capturedAtMs).arg(st.hasPluginState ? 1 : 0));
    }});
s.registerTool({"load_virus_preset",
    "Load a Virus ROM preset into a gearmulator plugin slot (Osirus=Virus A/B/C, OsTIrus, Vavra, Xenia): CC0 bank select (0-7 = banks A-H singles) + program change (0-127), like the hardware front panel. Applies to the LIVE plugin instance; for offline exports capture via project save.",
    objSchema({{"trackId",  QJsonObject{{"type","integer"}}},
              {"slotIndex",QJsonObject{{"type","integer"}}},
              {"bank",     QJsonObject{{"type","integer"},{"minimum",0},{"maximum",7}}},
              {"program",  QJsonObject{{"type","integer"},{"minimum",0},{"maximum",127}}},
              {"channel",  QJsonObject{{"type","integer"},{"minimum",1},{"maximum",16}}}},
              {"trackId","slotIndex","bank","program"}),
    "fx",
    [e](const QJsonObject& a) -> McpToolResult {
        return runVirusRomPreset(*e,
            a.value("trackId").toInt(), a.value("slotIndex").toInt(),
            a.value("bank").toInt(), a.value("program").toInt(),
            a.value("channel").toInt(1),
            a.value("captureToTree").toBool(true));
    }});

s.registerTool({"load_nord_bank",
    "Load a Nord Lead 2x bank/preset file (.syx raw Clavia SysEx, or .mid SMF wrapping Clavia SysEx) into a NodalRed2x plugin slot via injected MIDI SysEx \u2014 the emulated NL2x firmware applies each dump to its patch banks; optional program (0-127) sends a trailing program change to select a voice afterwards. ATOMIC: validates every dump (F0 33 <dev> 04 header, F7-terminated, <=32768B) BEFORE queueing anything, appends a harmless CC125 after the dumps; delivery is paced at <=1 SysEx per block and the deferred capture is delayed per queued dump, then confirmed via get_fx_capture_status (no capture-race). Realtime mutation: not undoable; capture via project save.",
    objSchema({{"trackId",  QJsonObject{{"type","integer"}}},
              {"slotIndex",QJsonObject{{"type","integer"}}},
              {"filePath", QJsonObject{{"type","string"}}},
              {"program",  QJsonObject{{"type","integer"}}}},
              {"trackId","slotIndex","filePath"}),
    "fx",
    [e](const QJsonObject& a) -> McpToolResult {
        const int program = a.contains("program") ? a.value("program").toInt(-1) : -1;
        if (a.contains("program") && program < 0)
            return McpToolResult::text("program must be 0..127", true);
        return runNordBankFile(*e,
            a.value("trackId").toInt(), a.value("slotIndex").toInt(),
            a.value("filePath").toString(), program,
            a.value("captureToTree").toBool(true));
    }});
s.registerTool({"load_je8086_preset",
    "Load ONE Roland JP-8080 patch from a bank file (.syx raw DT1 SysEx, or .mid SMF wrapping DT1 SysEx) into a JE8086 plugin slot via injected MIDI SysEx. preset is the 1-based patch unit in file order (default 1) - use the je8086 sidecar survey roleShortlist refs (perf016/part2, bank0/slot25) to choose one. ATOMIC: every DT1 message is validated (F0 41 10 00 06 12 header, F7-terminated, Roland checksum, <=32768B) BEFORE anything is queued, so a corrupt bank never half-loads. The dump keeps its UserPatch bank address; the JE8086 wrapper retargets it onto the sounding temp-performance patch (same transform as the plugin's own patch browser), so it sounds immediately. No CC0+program-change recall is sent - a JP-8080 PC loads the emulator's bank program into the current patch and would overwrite the applied dump. Per-patch by design: a 64-patch bank is 128 DT1 messages while the injection carries at most 64 events and the proxy forwards SysEx over a single lane that DROPS when busy rather than queueing. Realtime mutation: not undoable; capture via project save.",
    objSchema({{"trackId",  QJsonObject{{"type","integer"}}},
              {"slotIndex",QJsonObject{{"type","integer"}}},
              {"filePath", QJsonObject{{"type","string"}}},
              {"preset",   QJsonObject{{"type","integer"}}}},
              {"trackId","slotIndex","filePath"}),
    "fx",
    [e](const QJsonObject& a) -> McpToolResult {
        return runJe8086PatchFile(*e,
            a.value("trackId").toInt(), a.value("slotIndex").toInt(),
            a.value("filePath").toString(),
            a.value("preset").toInt(1),
            a.value("captureToTree").toBool(true));
    }});
s.registerTool({"set_master_fx_param",
        "Set a MASTER-bus FX slot parameter (eq / compressor / limiter) by paramIndex or paramName (the name get_master_fx_params returns; case-insensitive, paramName wins when both are given). Master FX shapes the whole mix â€” e.g. enable the limiter (slot 1) and set threshold -6 for loudness without touching track faders. Values clamp to the param defs.\n\nSlot map (default project): 0=eq (param0=Frequency Hz, param1=Q, param2=Gain dB), 1=limiter (param0=Threshold dB [-24..0], param1=Release ms [1..500], param2=Ceiling linear [0.5..1.0] — post-limiter output clamp; 1.0 = full scale). A slot only processes when bypassed=false.",
        objSchema({{"slotIndex", QJsonObject{{"type","integer"}}},
                  {"paramIndex",QJsonObject{{"type","integer"}}},
                  {"paramName", QJsonObject{{"type","string"}}},
                  {"value",     QJsonObject{{"type","number"}}}}, {"slotIndex","value"}),
        "fx",
        [e](const QJsonObject& a) -> McpToolResult {
            int si = a.value("slotIndex").toInt();
            int pi = a.value("paramIndex").toInt();
            float v = static_cast<float>(a.value("value").toDouble());
            // Validate against the defs BEFORE writing (Gate 9 parity with
            // set_internal_fx_param: out-of-range index = error, not a stray
            // param_N property).
            auto masterFx = e->getProjectModel().getTree().getChildWithName(IDs::MASTER_FX);
            if (! masterFx.isValid())
                return McpToolResult::text("no MASTER_FX node", true);
            if (si < 0 || si >= masterFx.getNumChildren())
                return McpToolResult::text("slot not found", true);
            const juce::String fxType = masterFx.getChild(si).getProperty(IDs::fxType, "").toString();
            const auto& defs = HDAW::masterFxParamDefs(fxType);
            const bool hasName = a.contains("paramName") && !a.value("paramName").toString().isEmpty();
            if (!hasName && !a.contains("paramIndex"))
                return McpToolResult::text("paramIndex or paramName required", true);
            if (hasName)
            {
                pi = masterParamIndexByName(defs, a.value("paramName").toString());
                if (pi < 0)
                    return McpToolResult::text("unknown paramName: " + a.value("paramName").toString(), true);
            }
            if (pi < 0 || pi >= static_cast<int>(defs.size()))
                return McpToolResult::text("param index out of range", true);
            const float written = e->getProjectCommands().setMasterFxParam(si, pi, v);
            if (written != v)
                return McpToolResult::text(QString("ok (paramIndex %1 clamped: %2 -> %3)")
                    .arg(pi)
                    .arg(QString::number(static_cast<double>(v), 'g', 6))
                    .arg(QString::number(static_cast<double>(written), 'g', 6)));
            return McpToolResult::text("ok");
        }});

s.registerTool({"set_master_fx_bypassed",
        "Enable/disable a MASTER-bus FX slot (bypassed=false = processing). Default master slots: 0=eq, 1=limiter, both bypassed by default. Enable the limiter for loudness-without-clipping.",
        objSchema({{"slotIndex", QJsonObject{{"type","integer"}}},
                  {"bypassed",  QJsonObject{{"type","boolean"}}}}, {"slotIndex","bypassed"}),
        "fx",
        [e](const QJsonObject& a) -> McpToolResult {
            int si = a.value("slotIndex").toInt();
            auto masterFx = e->getProjectModel().getTree().getChildWithName(IDs::MASTER_FX);
            if (! masterFx.isValid())
                return McpToolResult::text("no MASTER_FX node", true);
            if (si < 0 || si >= masterFx.getNumChildren())
                return McpToolResult::text("slot not found", true);
            e->getProjectCommands().setMasterFxBypassed(si, a.value("bypassed").toBool());
            return McpToolResult::text("ok");
        }});

s.registerTool({"get_master_fx_params",
        "Read the MASTER-bus FX chain: {slots:[{slotIndex,fxType,bypassed,params:[{index,name,value,defaultValue,minValue,maxValue}]}]}. Reads the project ValueTree (source of truth).",
        objSchema({}, {}),
        "fx",
        [e](const QJsonObject&) -> McpToolResult {
            auto masterFx = e->getProjectModel().getTree().getChildWithName(IDs::MASTER_FX);
            if (! masterFx.isValid())
                return McpToolResult::text("no MASTER_FX node", true);
            QJsonArray slotsArr;
            for (int i = 0; i < masterFx.getNumChildren(); ++i)
            {
                auto slot = masterFx.getChild(i);
                const juce::String fxType = slot.getProperty(IDs::fxType, "").toString();
                const auto& defs = HDAW::masterFxParamDefs(fxType);
                QJsonArray paramsArr;
                for (int p = 0; p < static_cast<int>(defs.size()); ++p)
                {
                    QJsonObject po;
                    po["index"] = p;
                    po["name"] = defs[(size_t) p].name;
                    po["value"] = static_cast<double>(slot.getProperty("param_" + juce::String(p), (double) defs[(size_t) p].def));
                    po["defaultValue"] = static_cast<double>(defs[(size_t) p].def);
                    po["minValue"] = static_cast<double>(defs[(size_t) p].min);
                    po["maxValue"] = static_cast<double>(defs[(size_t) p].max);
                    paramsArr.append(po);
                }
                QJsonObject so;
                so["slotIndex"] = i;
                so["fxType"] = QString::fromStdString(fxType.toStdString());
                so["bypassed"] = static_cast<bool>(slot.getProperty("bypassed", true));
                so["params"] = paramsArr;
                slotsArr.append(so);
            }
            QJsonObject root; root["slots"] = slotsArr;
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(root).toJson(QJsonDocument::Compact)));
        }});

s.registerTool({"set_internal_fx_param",
        "Set an internal (non-plugin) FX parameter value. Works for eq, compressor, reverb, delay, chorus, flanger, phaser, filter, saturator, sampler, fm_synth, growl_bass, psyarp, psy_fm, and sub_synth. Values are in REAL units (the engine's internal range per param — cutoff in Hz, drive in dB, etc). Call list_fx_params {trackId, slotIndex} FIRST to discover the exact range and default for each paramIndex, or pass paramName (the name list_fx_params returns; case-insensitive, paramName wins when both are given) — out-of-range values are silently clamped (lesson 23).",
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                  {"slotIndex", QJsonObject{{"type","integer"}}},
                  {"paramIndex",QJsonObject{{"type","integer"}}},
                  {"paramName", QJsonObject{{"type","string"}}},
                  {"value",     QJsonObject{{"type","number"}}}}, {"trackId","slotIndex","value"}),
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
            // Gate 9: validate against the type's real-unit defs table â€” an
            // out-of-range index must be an error, never a stray param_N
            // property write (existing set_fx_param behavior).
            auto defs = HDAW::TrackFXSlot::getParamDefsForType(fxSlots[si].fxType);
            const bool hasName = a.contains("paramName") && !a.value("paramName").toString().isEmpty();
            if (!hasName && !a.contains("paramIndex"))
                return McpToolResult::text("paramIndex or paramName required", true);
            if (hasName)
            {
                pi = internalParamIndexByName(defs, a.value("paramName").toString());
                if (pi < 0)
                    return McpToolResult::text("unknown paramName: " + a.value("paramName").toString(), true);
            }
            if (pi < 0 || pi >= static_cast<int>(defs.size()))
                return McpToolResult::text("param index out of range", true);
            float v = static_cast<float>(a.value("value").toDouble());
            // setFxSlotParam clamps to the def range (lesson-23 guard,
            // unchanged) and returns the value actually written â€” surface a
            // clamp in the response so out-of-range writes are visible
            // instead of a bare "ok" (session 2026-09: ClipType=24, valid
            // 0-3, was silently clamped with no feedback).
            const float written = e->getProjectCommands().setFxSlotParam(ti, si, pi, v);
            if (written != v)
                return McpToolResult::text(QString("ok (paramIndex %1 clamped: %2 -> %3)")
                    .arg(pi)
                    .arg(QString::number(static_cast<double>(v), 'g', 6))
                    .arg(QString::number(static_cast<double>(written), 'g', 6)));
            return McpToolResult::text("ok");
        }});

s.registerTool({"apply_sub_synth_mod_preset",
        "Apply a named factory preset to a sub_synth slot's internal modulation LFO (params 27-32) in ONE atomic, undoable step â€” the mod matrix moves together and every other synth param is untouched. presetId one of: off, slow_filter_drift, vibrato, tremolo, fm_motion, animated_sweep.",
        objSchema({{"trackId",  QJsonObject{{"type","integer"}}},
                  {"slotIndex", QJsonObject{{"type","integer"}}},
                  {"presetId",  QJsonObject{{"type","string"},
                      {"enum", QJsonArray{"off","slow_filter_drift","vibrato","tremolo","fm_motion","animated_sweep"}}}}},
                   {"trackId","slotIndex","presetId"}),
        "fx",
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            int si = a.value("slotIndex").toInt();
            const std::string presetId = a.value("presetId").toString().toStdString();
            std::string err;
            if (!e->getProjectCommands().applySubSynthModPreset(ti, si, presetId, &err))
                return McpToolResult::text(QString::fromStdString(
                    err.empty() ? "apply_sub_synth_mod_preset failed" : err), true);
            return McpToolResult::text(QString("ok: preset '%1' applied (params 27-32)")
                .arg(QString::fromStdString(presetId)));
        }});

s.registerTool({"get_internal_fx_param",
        "Read back the CURRENT value of an internal (non-plugin) FX slot's parameters in REAL units â€” the verification complement to set_internal_fx_param. Works for eq, compressor, reverb, delay, chorus, flanger, phaser, filter, saturator, sampler, fm_synth, growl_bass, psyarp, psy_fm, and sub_synth. Returns {params:[{index,name,value,defaultValue,minValue,maxValue}]}; untouched params report their default value. Reads the project ValueTree (source of truth â€” no render, no DSP access, read-only).",
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                  {"slotIndex", QJsonObject{{"type","integer"}}}}, {"trackId","slotIndex"}),
        "fx",
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            int si = a.value("slotIndex").toInt();
            auto fxSlots = e->getReadModel().getFxSlots(ti);
            if (si < 0 || si >= static_cast<int>(fxSlots.size()))
                return McpToolResult::text("get_internal_fx_param: slot not found", true);
            // Gate 9: validate against the type's real-unit defs table â€” a
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
        "params 0-22 plus Osc2 FM (param 25) in real units "
        "(cutoff/envelopes/levels/waves/...); Virus "
        "features with no sub_synth equivalent (ring mod, LFOs, keytrack, FX, "
        "mod matrix, noise) are reported in 'unmapped' â€” never silently dropped. "
        "On a bad file/slot/checksum the slot is left unchanged.",
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                   {"slotIndex", QJsonObject{{"type","integer"}}},
                   {"filePath",  QJsonObject{{"type","string"}}},
                   {"voiceIndex",QJsonObject{{"type","integer"}}}},
                   {"trackId","slotIndex","filePath"}),
        "fx",
        [e](const QJsonObject& a) -> McpToolResult {
            return runSubSynthImportSysex(*e,
                a.value("trackId").toInt(), a.value("slotIndex").toInt(),
                a.value("filePath").toString(),
                a.value("voiceIndex").toInt(0));
        }});

s.registerTool({"apply_preset",
    "Apply a preset to ONE FX slot, dispatching by slot target + file header — the agentic front door that replaces load_nord_bank / load_virus_preset / fm_synth_import_sysex / sub_synth_import_sysex / load_plugin_preset_file (which all stay registered). Reads the slot's fxType + pluginId, detects the file format from the header bytes, routes to the matching loader, and returns that loader's result:\n"
    "- NodalRed2x slot + Clavia dump (F0 33 .syx / SMF .mid) -> bank load via MIDI SysEx (optional program 0-127 selects a voice afterwards).\n"
    "- Xenia/Vavra slot + Waldorf dump (F0 3E 0E / F0 3E 10 .syx) -> live MIDI SysEx import with plugin-state capture.\n"
    "- Gearmulator Virus slot (OsTIrus/Osirus/Vavra/Xenia/JE8086) + program (optional bank 0-7, no filePath) -> ROM preset via CC0+PC.\n"
        "- Internal fm_synth slot + F0 43 .syx (single 163B, cartridge 4104B, raw VMEM 4096B) -> patch via setFmPatch (voiceIndex picks the cartridge voice).\n"
    "- Internal sub_synth slot + Virus dump (F0 00 20 33) -> patch via loadVirusPatch (voiceIndex for TI banks).\n"
    "- Any plugin slot + .SerumPreset (XferJson) / .fxp (CcnK) -> setStateInformation via parsePresetFile.\n"
    "Otherwise errors: cannot determine preset type. Realtime mutations (SysEx/CC/PC routes) are not undoable; capture via project save.",
    objSchema({{"trackId",     QJsonObject{{"type","integer"}}},
              {"slotIndex",   QJsonObject{{"type","integer"}}},
              {"filePath",    QJsonObject{{"type","string"}}},
              {"program",     QJsonObject{{"type","integer"},{"minimum",0},{"maximum",127}}},
              {"bank",        QJsonObject{{"type","integer"},{"minimum",0},{"maximum",7}}},
              {"voiceIndex",  QJsonObject{{"type","integer"},{"minimum",0}}},
              {"channel",     QJsonObject{{"type","integer"},{"minimum",1},{"maximum",16}}},
              {"captureToTree",QJsonObject{{"type","boolean"}}}},
             {"trackId","slotIndex"}),
    "fx",
    [e](const QJsonObject& a) -> McpToolResult {
        int ti = a.value("trackId").toInt();
        int si = a.value("slotIndex").toInt();
        auto fxSlots = e->getReadModel().getFxSlots(ti);
        if (si < 0 || si >= (int)fxSlots.size())
            return McpToolResult::text("slot not found", true);
        if (fxSlots[si].fxType == "none")
            return McpToolResult::text("slot is empty", true);
        const std::string fxType = fxSlots[si].fxType;
        const std::string pluginId = fxSlots[si].pluginId;

        const bool hasFile = a.contains("filePath")
            && !a.value("filePath").toString().isEmpty();
        juce::MemoryBlock raw;
        const uint8_t* bytes = nullptr;
        size_t size = 0;
        juce::String extension;
        if (hasFile)
        {
            const QString path = a.value("filePath").toString();
            const juce::File f(juce::String::fromUTF8(path.toUtf8()));
            if (!f.existsAsFile())
                return McpToolResult::text("file not found: " + path, true);
            if (!f.loadFileAsData(raw))
                return McpToolResult::text("failed to read file", true);
            bytes = static_cast<const uint8_t*>(raw.getData());
            size = raw.getSize();
            extension = f.getFileExtension().toLowerCase();
        }

        const bool hasProgram = a.contains("program");
        if (hasProgram)
        {
            const int program = a.value("program").toInt(-1);
            if (program < 0 || program > 127)
                return McpToolResult::text("program must be 0..127", true);
        }

        const auto route = mcp::resolvePresetRoute(
            fxType, pluginId, bytes, size, extension, hasProgram);
        if (route.kind == PresetRouteKind::None)
            return McpToolResult::text(jstr(route.error), true);

        const bool capture = a.value("captureToTree").toBool(true);
        const QString path = hasFile ? a.value("filePath").toString() : QString();
        switch (route.kind)
        {
            case PresetRouteKind::NordBank:
            {
                const int program = a.contains("program")
                    ? a.value("program").toInt(-1) : -1;
                return runNordBankFile(*e, ti, si, path, program, capture);
            }
            case PresetRouteKind::Je8086Patch:
            {
                // program doubles as the 1-based patch unit for JP-8080 files
                const int unit = a.contains("program") ? a.value("program").toInt(1) : 1;
                return runJe8086PatchFile(*e, ti, si, path,
                    unit > 0 ? unit : 1, capture);
            }
            case PresetRouteKind::WaldorfSysex:
                return runWaldorfSysexFile(*e, ti, si, path, pluginId, capture);
            case PresetRouteKind::VirusRom:
                return runVirusRomPreset(*e, ti, si,
                    a.value("bank").toInt(0), a.value("program").toInt(0),
                    a.value("channel").toInt(1), capture);
            case PresetRouteKind::FmSysex:
                return runFmImportSysex(*e, ti, si, path,
                    a.value("voiceIndex").toInt(0));
            case PresetRouteKind::SubSynthVirus:
                return runSubSynthImportSysex(*e, ti, si, path,
                    a.value("voiceIndex").toInt(0));
            case PresetRouteKind::PluginPresetFile:
                return runLoadPluginPresetFile(*e, ti, si, path);
            case PresetRouteKind::None:
                break;
        }
        return McpToolResult::text(jstr(route.error), true);
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
        "audition only â€” no offline render. Returns {ok, trackId, slotIndex, name, engine, role}.",
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

            // â”€â”€ resolve engine: explicit arg, else sidecar engine key, else header â”€â”€
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
                        "could not determine patch engine â€” pass engine explicitly "
                        "(sub_synth or fm_synth)", true);
            }

            // â”€â”€ probe track (or reuse trackId) â”€â”€
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

            // â”€â”€ synth slot of the engine type â”€â”€
            e->getProjectCommands().addFxSlot(trackId, engine, -1, "");
            auto fxChain = tl.getChild(trackId).getChildWithName(IDs::FX_CHAIN);
            const int n = fxChain.isValid() ? fxChain.getNumChildren() : 0;
            const int slotIndex = n > 0 ? n - 1 : 0;

            // â”€â”€ load the patch â”€â”€
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

            // â”€â”€ role probe phrase clip â”€â”€
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

            // â”€â”€ sync the live processor (Gate 2/6): the probe track must be in
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
