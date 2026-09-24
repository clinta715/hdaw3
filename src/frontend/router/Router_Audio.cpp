#include "Router_Audio.h"
#include "RouterHelpers.h"

#include "../../engine/AudioEngine.h"
#include "../../engine/Dx7SysexImport.h"
#include "../../engine/MixReport.h"
#include "../../common/ProjectCommands.h"
#include "../../common/MixReportJson.h"
#include "../../common/MixVerdict.h"
#include "../../common/ToneVerity.h"
#include "../../mcp/McpJobs.h"
#include "../../common/ModulationCoverage.h"
#include "../../common/SongPlanView.h"
#include "../../engine/SongStructureAudit.h"
#include "../../common/FxCaptureStatus.h"
#include "../../common/SettingsKeys.h"
#include "../../common/FmPatchLoad.h"
#include "../../common/PresetApply.h"

#include <cmath>
#include <limits>
#include <stdexcept>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSettings>
#include <QString>

#include <juce_audio_formats/juce_audio_formats.h>
#include <string>

using namespace frontend::router_helpers;

namespace frontend {

namespace {
// Parse a compact-JSON tool text back into a JSON value (fm_synthImportSysex
// returns the shared loader's text verbatim as JSON — same payload as the
// MCP tool, parsed into the reply like the 2026-09-24 parity-wave routes).
QJsonValue parseText(const QString& text) {
    return QJsonDocument::fromJson(text.toUtf8()).object();
}
} // namespace

DispatchResult dispatchAudio(AudioEngine& engine, const QString& m, const QJsonValue& params) {
    auto& dm = engine.getDeviceManager();
    const auto o = paramsObject(params);

    if (m == "verifyTone") {
        // ToneVerity (Phase 2): the RPC twin of the MCP `tone_verity` tool —
        // SAME builder (buildToneVerityPayload) so the surfaces cannot drift.
        int trackIndex;
        if (!requireInt(o, "trackIndex", trackIndex, nullptr))
            return makeError(-32602, "trackIndex required");
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const auto num = [&](const char* k) {
            return o.contains(k) && o.value(k).isDouble() ? o.value(k).toDouble() : nan;
        };
        ProjectCommands::ToneVerityParams p;
        p.trackIndex = trackIndex;
        p.windowSeconds = o.contains("windowSeconds") ? o.value("windowSeconds").toDouble() : 4.0;
        p.startBeat = num("startBeat"); if (std::isnan(p.startBeat)) p.startBeat = -1.0;
        if (o.contains("binSeconds")) p.binSeconds = o.value("binSeconds").toDouble();
        p.attackMsMin = num("attackMsMin");
        p.attackMsMax = num("attackMsMax");
        p.sustainRatioMin = num("sustainRatioMin");
        p.modRateHz = num("modRateHz");
        if (o.contains("modRateTolPct")) p.modRateTolPct = o.value("modRateTolPct").toDouble();
        p.centroidRiseMin = num("centroidRiseMin");
        p.f0Hz = num("f0Hz");
        if (o.contains("f0CentsMax")) p.f0CentsMax = o.value("f0CentsMax").toDouble();
        auto r = engine.getProjectCommands().verifyTone(p);
        return { false, HDAW::buildToneVerityPayload(r) };
    }

    if (m == "mixReport") {
        // Offline mix analysis of a rendered file. The payload comes from the SHARED builder
        // (src/common/MixReportJson.cpp) that the MCP mix_report tool also uses, so the two
        // surfaces cannot drift — they had before 2026-09-21 (this one lacked the
        // file-visibility guard and emitted a different bands/sections shape, while claiming
        // "Same JSON shape as the MCP mix_report tool").
        // fromPlan derives the windows from the song plan (beats -> seconds via bpm; bpm 0
        // falls back to the plan bpm) and adds the same structure + loudness-gate extras the
        // MCP tool reports, so the payloads match for the same inputs.
        std::string filePath;
        if (!requireString(o, "filePath", filePath, nullptr))
            return makeError(-32602, "filePath required");
        double bpm = o.value("bpm").toDouble(0.0);
        if (bpm < 0.0)
            return makeError(-32602, "bpm must be >= 0");

        const bool fromPlan = o.value("fromPlan").toBool(false);
        auto plan = engine.getProjectCommands().getSongPlan();
        std::vector<HDAW::SectionWindow> windows;
        QJsonObject planKinds;   // section name -> plan kind (for the loudness gate)
        if (fromPlan) {
            if (plan.sections.empty())
                return makeError(-32602, "no song plan set (fromPlan)");
            if (bpm <= 0.0) bpm = plan.bpm;
            const double spb = (bpm > 0.0) ? 60.0 / bpm : 0.5;
            for (const auto& s : plan.sections) {
                windows.push_back(HDAW::SectionWindow{ s.name, s.startBeat * spb, s.endBeat * spb });
                planKinds.insert(QString::fromStdString(s.name), QString::fromStdString(s.kind));
            }
        } else {
            const auto secs = o.value("sections");
            if (secs.isArray()) {
                for (const auto& v : secs.toArray()) {
                    const auto so = v.toObject();
                    windows.push_back(HDAW::SectionWindow{ so.value("name").toString().toStdString(),
                                                          so.value("start").toDouble(),
                                                          so.value("end").toDouble() });
                }
            }
        }

        // Build the payload ONCE for the sync path, or on the job worker for wait=false.
        // Captured BY VALUE: a queued job must never touch the engine.
        const double ratio = [&] {
            double r = o.value("dropBuildRatio").toDouble(0.9);
            return (r <= 0.0 || r > 1.5) ? 0.9 : r;
        }();
        const QJsonObject structureJson = (fromPlan && !planKinds.isEmpty())
            ? HDAW::structureAuditJson(HDAW::auditSongStructure(
                  engine.getProjectModel().getTrackListTree(), plan, bpm))
            : QJsonObject{};

        // Error codes travel with the failure (a message-string test would be fragile):
        // -32602 for the caller's windows, -32603 for everything the harness/file did.
        struct MixReportError : std::runtime_error {
            int code;
            MixReportError(int c, const std::string& w) : std::runtime_error(w), code(c) {}
        };
        auto buildPayload = [filePath, windows, planKinds, bpm, ratio, structureJson]() -> QJsonObject {
            auto built = HDAW::buildMixReportPayload(QString::fromStdString(filePath), windows, bpm);
            if (!built.error.isEmpty())
                throw MixReportError(-32603, built.error.toStdString());
            if (built.allWindowsDropped)
                throw MixReportError(-32602, "no plan sections fall inside the file duration");
            QJsonObject root = built.payload;
            if (!structureJson.isEmpty()) root["structure"] = structureJson;
            if (!planKinds.isEmpty()) HDAW::applyDropVsBuildGate(root, planKinds, ratio);
            return root;
        };

        // wait=false mirrors the MCP mix_report tool: submit to the process-wide job registry and
        // poll it through audio.jobStatus (the per-domain status convention used by
        // tuning.jobStatus / rave.jobStatus — all read the same registry).
        if (!o.value("wait").toBool(true)) {
            const int id = mcp::McpJobs::instance().submit("mix_report", buildPayload);
            return { false, QJsonObject{ { "jobId", id }, { "state", "running" },
                                         { "pollWith", "audio.jobStatus" } } };
        }
        try {
            return { false, buildPayload() };
        } catch (const MixReportError& ex) {
            return makeError(ex.code, QString::fromUtf8(ex.what()));
        } catch (const std::exception& ex) {
            return makeError(-32603, QString::fromUtf8(ex.what()));
        }
    }

    if (m == "jobStatus") {
        // Poll an async analysis job (mix_report today; the registry is process-wide, so rave and
        // tuning jobs are readable here too — the status routes are thin views of one registry).
        int jobId = 0;
        if (!requireInt(o, "jobId", jobId, nullptr))
            return makeError(-32602, "missing or non-numeric param: jobId");
        const auto status = mcp::McpJobs::instance().status(jobId);
        if (status.isEmpty()) return makeError(-32602, "unknown jobId");
        return { false, status };
    }

    if (m == "mixVerdict") {
        // ONE release-readiness verdict (audible / clipping / loudness / structure / intro
        // blast) — composable from the same shared pieces as mix_report; the MCP twin is
        // mix_verdict. MODULATION coverage stays audit_modulation_coverage's job.
        std::string filePath;
        if (!requireString(o, "filePath", filePath, nullptr))
            return makeError(-32602, "filePath required");
        double bpm = o.value("bpm").toDouble(0.0);
        double ratio = o.value("dropBuildRatio").toDouble(0.9);
        if (ratio <= 0.0 || ratio > 1.5) ratio = 0.9;

        std::vector<HDAW::SectionWindow> windows;
        QJsonObject planKinds;
        QJsonObject structureJson;
        if (o.value("fromPlan").toBool(false)) {
            const auto plan = engine.getProjectCommands().getSongPlan();
            if (plan.sections.empty())
                return makeError(-32602, "no song plan set (fromPlan)");
            if (bpm <= 0.0) bpm = plan.bpm;
            const double spb = (bpm > 0.0) ? 60.0 / bpm : 0.5;
            for (const auto& s : plan.sections) {
                windows.push_back(HDAW::SectionWindow{ s.name, s.startBeat * spb, s.endBeat * spb });
                planKinds.insert(QString::fromStdString(s.name), QString::fromStdString(s.kind));
            }
            structureJson = HDAW::structureAuditJson(HDAW::auditSongStructure(
                engine.getProjectModel().getTrackListTree(), plan, bpm));
        } else {
            const auto secs = o.value("sections");
            if (secs.isArray()) {
                for (const auto& v : secs.toArray()) {
                    const auto so = v.toObject();
                    windows.push_back(HDAW::SectionWindow{ so.value("name").toString().toStdString(),
                                                           so.value("start").toDouble(),
                                                           so.value("end").toDouble() });
                }
            }
        }

        const auto v = HDAW::buildMixVerdict(QString::fromStdString(filePath), windows, planKinds,
                                             bpm, ratio, structureJson,
                                             HDAW::modulationCoverageJson(
                                                 engine.getProjectModel().getTrackListTree()),
                                             o.value("introSeconds").toDouble(2.0));
        if (!v.error.isEmpty())
            return makeError(-32603, v.error);
        return { false, v.verdict };
    }

    if (m == "getDeviceTypes") {
        QJsonArray arr;
        for (auto* type : dm.getAvailableDeviceTypes())
            arr.append(QString::fromUtf8(type->getTypeName().toRawUTF8()));
        return { false, arr };
    }

    if (m == "getOutputDevices") {
        QJsonArray arr;
        auto* devType = dm.getCurrentDeviceTypeObject();
        if (devType != nullptr)
            for (const auto& name : devType->getDeviceNames(false))
                arr.append(QString::fromUtf8(name.toRawUTF8()));
        return { false, arr };
    }

    if (m == "getInputDevices") {
        QJsonArray arr;
        auto* devType = dm.getCurrentDeviceTypeObject();
        if (devType != nullptr)
            for (const auto& name : devType->getDeviceNames(true))
                arr.append(QString::fromUtf8(name.toRawUTF8()));
        return { false, arr };
    }

    if (m == "getCurrentSetup") {
        auto setup = dm.getAudioDeviceSetup();
        auto* dev = dm.getCurrentAudioDevice();
        double sr = setup.sampleRate;
        int bs = setup.bufferSize;
        double latencyMs = 0.0;
        if (dev != nullptr) {
            sr = dev->getCurrentSampleRate();
            bs = dev->getCurrentBufferSizeSamples();
            if (sr > 0.0) latencyMs = static_cast<double>(bs) / sr * 1000.0;
        }
        return { false, QJsonObject{
            { "driver",      QString::fromUtf8(dm.getCurrentAudioDeviceType().toRawUTF8()) },
            { "output",      QString::fromUtf8(setup.outputDeviceName.toRawUTF8()) },
            { "input",       QString::fromUtf8(setup.inputDeviceName.toRawUTF8()) },
            { "sampleRate",  sr },
            { "bufferSize",  bs },
            { "latencyMs",   latencyMs },
        }};
    }

    if (m == "getSampleRates") {
        QJsonArray arr;
        auto* dev = dm.getCurrentAudioDevice();
        if (dev != nullptr)
            for (double rate : dev->getAvailableSampleRates())
                arr.append(rate);
        return { false, arr };
    }

    if (m == "getBufferSizes") {
        QJsonArray arr;
        auto* dev = dm.getCurrentAudioDevice();
        if (dev != nullptr)
            for (int buf : dev->getAvailableBufferSizes())
                arr.append(buf);
        return { false, arr };
    }

    if (m == "setDeviceType") {
        std::string type;
        if (!requireString(o, "type", type, nullptr))
            return makeError(-32602, "type required");
        dm.setCurrentAudioDeviceType(juce::String(type), true);
        QSettings s;
        s.setValue(SettingsKeys::kKeyAudioDriver, QString::fromStdString(type));
        return { false, QJsonValue::Null };
    }

    if (m == "setOutputDevice") {
        std::string name;
        if (!requireString(o, "name", name, nullptr))
            return makeError(-32602, "name required");
        auto setup = dm.getAudioDeviceSetup();
        setup.outputDeviceName = juce::String(name);
        dm.setAudioDeviceSetup(setup, true);
        QSettings s;
        s.setValue(SettingsKeys::kKeyAudioOutputDevice, QString::fromStdString(name));
        return { false, QJsonValue::Null };
    }

    if (m == "setInputDevice") {
        std::string name;
        if (!requireString(o, "name", name, nullptr))
            return makeError(-32602, "name required");
        auto setup = dm.getAudioDeviceSetup();
        setup.inputDeviceName = juce::String(name);
        dm.setAudioDeviceSetup(setup, true);
        QSettings s;
        s.setValue(SettingsKeys::kKeyAudioInputDevice, QString::fromStdString(name));
        return { false, QJsonValue::Null };
    }

    if (m == "setSampleRate") {
        double rate;
        if (!requireDouble(o, "rate", rate, nullptr))
            return makeError(-32602, "rate required");
        auto setup = dm.getAudioDeviceSetup();
        setup.sampleRate = rate;
        dm.setAudioDeviceSetup(setup, true);
        QSettings s;
        s.setValue(SettingsKeys::kKeyAudioSampleRate, static_cast<qint64>(rate));
        return { false, QJsonValue::Null };
    }

    if (m == "setBufferSize") {
        int size;
        if (!requireInt(o, "size", size, nullptr))
            return makeError(-32602, "size required");
        auto setup = dm.getAudioDeviceSetup();
        setup.bufferSize = size;
        dm.setAudioDeviceSetup(setup, true);
        QSettings s;
        s.setValue(SettingsKeys::kKeyAudioBufferSize, size);
        return { false, QJsonValue::Null };
    }

    // Deferred-capture receipt for one FX slot — the RPC twin of the MCP tool
    // get_fx_capture_status (shared HDAW::readFxCaptureStatus, so the receipt cannot
    // disagree between surfaces). Poll this after a bank load / preset apply instead
    // of trusting the immediate capturedToTree=0: a deferred capture completes after
    // the call returns. READ-ONLY.
    if (m == "getFxCaptureStatus") {
        int ti, si;
        if (!requireInt(o, "trackIndex", ti, nullptr) || !requireInt(o, "slotIndex", si, nullptr))
            return makeError(-32602, "trackIndex and slotIndex required");
        auto slotTree = engine.getProjectModel().getTrackListTree()
            .getChild(ti).getChildWithName(IDs::FX_CHAIN).getChild(si);
        if (!slotTree.isValid()) return makeError(-32602, "slot not found in tree");
        const auto st = HDAW::readFxCaptureStatus(slotTree);
        return { false, QJsonObject{ { "status", st.status },
                                     { "stateBytes", st.stateBytes },
                                     { "capturedAtMs", static_cast<double>(st.capturedAtMs) },
                                     { "hasPluginState", st.hasPluginState } } };
    }

    // FX A/B comparison: capture/swap plugin state snapshots.
    if (m == "captureFxSnapshot") {
        int ti, si;
        if (!requireInt(o, "trackIndex", ti, nullptr) || !requireInt(o, "slotIndex", si, nullptr))
            return makeError(-32602, "trackIndex and slotIndex required");
        auto* proc = engine.getMainProcessor();
        if (!proc) return makeError(-32603, "audio engine not initialized");
        auto* track = proc->getTrack(ti);
        if (!track) return makeError(-32602, "track not found");
        auto& chain = track->getFXChain();
        if (si < 0 || si >= static_cast<int>(chain.size())) return makeError(-32602, "slot not found");
        auto* slot = chain[si].get();
        if (!slot->isPlugin() || !slot->getPluginInstance()) return makeError(-32602, "slot is not a plugin");
        juce::MemoryBlock state;
        slot->getPluginInstance()->getStateInformation(state);
        auto& model = engine.getProjectModel();
        auto slotTree = model.getTrackListTree().getChild(ti)
            .getChildWithName(IDs::FX_CHAIN).getChild(si);
        if (slotTree.isValid())
            slotTree.setProperty(juce::Identifier("pluginStateB"), state.toBase64Encoding(), &model.getUndoManager());
        return { false, QJsonValue::Null };
    }
    if (m == "swapFxSnapshot") {
        int ti, si;
        if (!requireInt(o, "trackIndex", ti, nullptr) || !requireInt(o, "slotIndex", si, nullptr))
            return makeError(-32602, "trackIndex and slotIndex required");
        auto* proc = engine.getMainProcessor();
        if (!proc) return makeError(-32603, "audio engine not initialized");
        auto* track = proc->getTrack(ti);
        if (!track) return makeError(-32602, "track not found");
        auto& chain = track->getFXChain();
        if (si < 0 || si >= static_cast<int>(chain.size())) return makeError(-32602, "slot not found");
        auto* slot = chain[si].get();
        if (!slot->isPlugin() || !slot->getPluginInstance()) return makeError(-32602, "slot is not a plugin");
        auto& model = engine.getProjectModel();
        auto& um = model.getUndoManager();
        auto slotTree = model.getTrackListTree().getChild(ti)
            .getChildWithName(IDs::FX_CHAIN).getChild(si);
        if (!slotTree.isValid()) return makeError(-32602, "slot tree not found");
        // Capture current state.
        juce::MemoryBlock currentState;
        slot->getPluginInstance()->getStateInformation(currentState);
        // Load snapshot B (if exists), swap.
        juce::String b64 = slotTree.getProperty(juce::Identifier("pluginStateB")).toString();
        if (b64.isNotEmpty())
        {
            juce::MemoryBlock bState;
            bState.fromBase64Encoding(b64);
            slot->getPluginInstance()->setStateInformation(bState.getData(), static_cast<int>(bState.getSize()));
            // Store current as the new snapshot B.
            slotTree.setProperty(juce::Identifier("pluginStateB"), currentState.toBase64Encoding(), &um);
        }
        else
        {
            // No snapshot B yet: capture current as B, load project state as A.
            slotTree.setProperty(juce::Identifier("pluginStateB"), currentState.toBase64Encoding(), &um);
            juce::String a64 = slotTree.getProperty(IDs::pluginState).toString();
            if (a64.isNotEmpty())
            {
                juce::MemoryBlock aState;
                aState.fromBase64Encoding(a64);
                slot->getPluginInstance()->setStateInformation(aState.getData(), static_cast<int>(aState.getSize()));
            }
        }
        return { false, QJsonValue::Null };
    }

    if (m == "fm_synthImportSysex") {
        // MCP twin of fm_synth_import_sysex (McpTools_FmSynth.cpp). The
        // 2026-09-24 parity fix: run the MCP loader's PERSISTING path —
        // parse the dump, then ProjectCommands::setFmPatch, which writes
        // IDs::fmPatchData to the slot tree (save/tree-copy renders keep the
        // patch) AND loads it live best-effort — instead of the old
        // live-only slot->fmSynthEngine()->loadPatch write. The old body
        // also rejected raw 4096-byte VMEM banks the tool accepts. The parse
        // + persistence + success payload now come from ONE place; the MCP
        // wrapper (runFmImportSysex, src/mcp/PresetRoute.h) wraps the same
        // shared loader, so the surfaces cannot drift.
        // Argument names mirror the MCP tool EXACTLY (AGENTS.md): trackId,
        // slotIndex, filePath, voiceIndex. (The pre-fix route's trackIndex
        // spelling predated the parity rule; the tool's trackId wins.)
        int ti, si;
        if (!requireInt(o, "trackId", ti, nullptr) || !requireInt(o, "slotIndex", si, nullptr))
            return makeError(-32602, "trackId and slotIndex required");

        std::string filePath;
        if (!requireString(o, "filePath", filePath, nullptr))
            return makeError(-32602, "filePath required");

        const int voiceIndex = o.contains("voiceIndex") ? o.value("voiceIndex").toInt(0) : 0;

        bool ok = false;
        const auto text = HDAW::fmImportSysexToolText(
            engine, ti, si, QString::fromStdString(filePath), voiceIndex, &ok);
        if (!ok)
            return makeError(-32602, text);
        return { false, parseText(text) };
    }

    if (m == "fmSynthLoadPreset") {
        // MCP twin of fm_synth_load_preset (McpTools_FmSynth.cpp): raw DX7
        // patch (156 bytes, 312-char hex) into an FM synth slot. Runs the
        // SAME shared entry point as the tool (HDAW::fmLoadPresetToolText,
        // src/common/FmPatchLoad.h) -> ProjectCommands::setFmPatch, so the
        // patch persists to the slot tree (fmPatchData) and answers
        // byte-identical text on both surfaces. The tool's key is trackId;
        // this surface's historical key is trackIndex (read.getWaveformPeaks
        // precedent: trackIndex/trackId read side by side).
        int ti = optInt(o, "trackIndex", -1, nullptr);
        if (ti < 0 && o.contains("trackId")) ti = o.value("trackId").toInt(-1);
        int si;
        if (ti < 0 || !requireInt(o, "slotIndex", si, nullptr))
            return makeError(-32602, "trackIndex and slotIndex required");

        std::string hex;
        if (!requireString(o, "patchData", hex, nullptr) || hex.empty())
            return makeError(-32602, "patchData must be 312 hex characters (156 bytes)");

        bool ok = false;
        const auto text = HDAW::fmLoadPresetToolText(
            engine, ti, si, QString::fromStdString(hex), &ok);
        if (!ok)
            return makeError(-32602, text);
        return { false, text };
    }

    if (m == "subSynthImportSysex") {
        // MCP twin of sub_synth_import_sysex (McpTools_FxSlot.cpp): Virus
        // .syx (267-byte B/C single or TI bank) into an internal sub_synth
        // slot via AudioEngineCommands::loadVirusPatch — the command layer IS
        // the shared entry point (src/common/PresetApply.h wraps the same
        // call for the tool), so slot-tree writes, live load and the payload
        // match by construction. Same key names as the tool.
        int ti, si;
        if (!requireInt(o, "trackId", ti, nullptr) || !requireInt(o, "slotIndex", si, nullptr))
            return makeError(-32602, "trackId and slotIndex required");
        std::string filePath;
        if (!requireString(o, "filePath", filePath, nullptr))
            return makeError(-32602, "filePath required");
        const int voiceIndex = o.contains("voiceIndex") ? o.value("voiceIndex").toInt(0) : 0;

        bool ok = false;
        const auto text = HDAW::subSynthImportSysexToolText(
            engine, ti, si, QString::fromStdString(filePath), voiceIndex, &ok);
        if (!ok)
            return makeError(-32602, text);
        return { false, parseText(text) };
    }

    if (m == "applyPreset") {
        // MCP twin of apply_preset — the agentic front door (McpTools_FxSlot.cpp):
        // dispatches by slot fxType/pluginId + file header onto the shared
        // loaders (HDAW::applyPresetToolText, src/common/PresetApply.h — the
        // SAME body the tool runs). The whole argument object goes through,
        // so the tool's key contract (trackId/slotIndex/filePath/program/
        // bank/voiceIndex/channel/captureToTree) holds on both surfaces.
        bool ok = false;
        const auto text = HDAW::applyPresetToolText(engine, o, &ok);
        if (!ok)
            return makeError(-32602, text);
        return { false, parseText(text) };
    }

    if (m == "auditionPatch") {
        // MCP twin of audition_patch — the composite probe (McpTools_FxSlot.cpp):
        // probe track/clip placement + the shared patch loaders
        // (HDAW::auditionPatchToolText, src/common/PresetApply.h — the SAME
        // body the tool runs; path/engine/role/root/trackId keys intact).
        bool ok = false;
        const auto text = HDAW::auditionPatchToolText(engine, o, &ok);
        if (!ok)
            return makeError(-32602, text);
        return { false, parseText(text) };
    }

    return makeError(-32601, "unknown audio method: " + m);
}

} // namespace frontend
