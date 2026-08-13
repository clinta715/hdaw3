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
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <algorithm>
#include <optional>

namespace mcp {

static std::optional<HDAW::EnvelopeGenerator::Shape> parseEnvelopeShape(const QString& s) {
    using S = HDAW::EnvelopeGenerator::Shape;
    if (s == "ramp") return S::Ramp;
    if (s == "adsr") return S::ADSR;
    if (s == "sine") return S::Sine;
    if (s == "triangle") return S::Triangle;
    if (s == "saw") return S::Saw;
    if (s == "square") return S::Square;
    if (s == "pulse") return S::Pulse;
    if (s == "staircase") return S::Staircase;
    if (s == "sCurve") return S::SCurve;
    if (s == "randomWalk") return S::RandomWalk;
    if (s == "noise") return S::Noise;
    return std::nullopt;
}

static void registerAudioReadTools(McpServer& s, AudioEngine* e)
{
    s.registerTool({"list_fx", "List FX slots on a track.",
        objSchema({{"trackId", QJsonObject{{"type","integer"}}}}, {"trackId"}),
        [e](const QJsonObject& a) {
            int ti = a.value("trackId").toInt(-1);
            auto tl = e->getProjectModel().getTrackListTree();
            if (ti < 0 || ti >= tl.getNumChildren())
                return McpToolResult::text("track not found", true);
            auto fxSlots = e->getReadModel().getFxSlots(ti);
            QJsonArray arr;
            for (const auto& s2 : fxSlots) {
                bool isPlugin = (s2.fxType == "plugin");
                QJsonObject o{{"slot", s2.slotIndex},
                              {"type", QString::fromStdString(s2.fxType)}};
                if (isPlugin) {
                    o["pluginId"] = QString::fromStdString(s2.pluginId);
                    o["paramCount"] = s2.paramCount;
                }
                o["bypassed"] = s2.bypassed;
                arr.append(o);
            }
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(arr).toJson(QJsonDocument::Compact)));
        }});

    s.registerTool({"list_automation_lanes", "List automation lanes on a track.",
        objSchema({{"trackId", QJsonObject{{"type","integer"}}}}, {"trackId"}),
        [e](const QJsonObject& a) {
            int ti = a.value("trackId").toInt(-1);
            auto tl = e->getProjectModel().getTrackListTree();
            if (ti < 0 || ti >= tl.getNumChildren())
                return McpToolResult::text("track not found", true);
            auto al = tl.getChild(ti).getChildWithName(IDs::AUTOMATION_LIST);
            QJsonArray arr;
            for (int i = 0; i < al.getNumChildren(); ++i) {
                auto lane = al.getChild(i);
                arr.append(QJsonObject{
                    {"name", jstr(lane.getProperty(IDs::name).toString())},
                    {"paramID", static_cast<int>(lane.getProperty(IDs::paramID))},
                    {"enabled", static_cast<bool>(lane.getProperty(IDs::automationEnabled))},
                    {"pointCount", lane.getChildWithName(IDs::POINT_LIST).getNumChildren()}
                });
            }
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(arr).toJson(QJsonDocument::Compact)));
        }});

    s.registerTool({"get_waveform_peaks",
        "Return downsampled min/max peak pairs for an audio clip waveform.",
        objSchema({{"clipId", QJsonObject{{"type","integer"}}},
                   {"numBins", QJsonObject{{"type","integer"}}}}, {"clipId"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int cid = a.value("clipId").toInt(-1);
            auto clip = findClip(e, cid, nullptr);
            if (!clip.isValid())
                return McpToolResult::text(QString("clipId %1 not found").arg(cid), true);
            if (clip.getProperty(IDs::clipType).toString() != juce::String("audio"))
                return McpToolResult::text("not an audio clip", true);

            auto sourceFile = clip.getProperty(IDs::sourceFile).toString();
            if (sourceFile.isEmpty())
                return McpToolResult::text("no source file", true);

            auto file = juce::File(sourceFile);
            if (!file.existsAsFile())
                return McpToolResult::text("source file missing", true);

            auto& fmtMgr = e->getProjectPool().getFormatManager();
            std::unique_ptr<juce::AudioFormatReader> reader(fmtMgr.createReaderFor(file));
            if (!reader)
                return McpToolResult::text("cannot open audio file", true);

            auto totalSamples = reader->lengthInSamples;
            if (totalSamples <= 0)
                return McpToolResult::text("empty audio", true);

            int numChannels = static_cast<int>(reader->numChannels);
            double sampleRate = reader->sampleRate;
            int numBins = a.value("numBins").toInt(1000);
            numBins = std::clamp(numBins, 100, 10000);
            int64_t samplesPerBin = totalSamples / static_cast<int64_t>(numBins);
            if (samplesPerBin < 1) samplesPerBin = 1;

            juce::AudioBuffer<float> buffer(numChannels, static_cast<int>(samplesPerBin));
            QJsonArray peaks;

            for (int i = 0; i < numBins; ++i) {
                int64_t startSample = static_cast<int64_t>(i) * samplesPerBin;
                int numToRead = static_cast<int>(
                    (std::min)(samplesPerBin, totalSamples - startSample));
                if (numToRead <= 0) {
                    peaks.append(0.0f);
                    peaks.append(0.0f);
                    continue;
                }
                buffer.clear();
                reader->read(&buffer, 0, numToRead, startSample, true, true);

                float minVal = 0.0f, maxVal = 0.0f;
                for (int ch = 0; ch < numChannels; ++ch) {
                    auto* data = buffer.getReadPointer(ch);
                    for (int s = 0; s < numToRead; ++s) {
                        if (data[s] < minVal) minVal = data[s];
                        if (data[s] > maxVal) maxVal = data[s];
                    }
                }
                peaks.append(minVal);
                peaks.append(maxVal);
            }

            QJsonObject result{{"peaks", peaks},
                               {"sampleRate", sampleRate},
                               {"numSamples", static_cast<qint64>(totalSamples)}};
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(result).toJson(QJsonDocument::Compact)));
        }});
}

static void registerFxTools(McpServer& s, AudioEngine* e)
{
    s.registerTool({"add_fx",
        "Add an FX slot. fxType in {eq,compressor,reverb,delay,chorus,flanger,phaser,sampler}, OR a pluginId.",
        objSchema({{"trackId",  QJsonObject{{"type","integer"}}},
                  {"fxType",   QJsonObject{{"type","string"},
                      {"enum", QJsonArray{"eq","compressor","reverb","delay","chorus","flanger","phaser","sampler"}}}},
                  {"pluginId", QJsonObject{{"type","string"}}},
                  {"position", QJsonObject{{"type","integer"}}}}, {"trackId"}),
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

    s.registerTool({"list_plugin_presets",
        "List all preset/program names of a plugin FX slot. Uses the preset cache when available (populated during plugin scanning); falls back to querying the live plugin instance.",
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                  {"slotIndex", QJsonObject{{"type","integer"}}}},
                 {"trackId","slotIndex"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            int si = a.value("slotIndex").toInt();
            auto fxSlots = e->getReadModel().getFxSlots(ti);
            if (si < 0 || si >= (int)fxSlots.size())
                return McpToolResult::text("slot not found", true);
            if (fxSlots[si].fxType != "plugin")
                return McpToolResult::text("slot is not a plugin", true);

            // Try cache first (by pluginId)
            auto* presetInfo = e->getPluginManager().getPresetInfo(juce::String(fxSlots[si].pluginId));
            if (presetInfo && presetInfo->numPrograms > 1)
            {
                QJsonArray arr;
                for (int i = 0; i < presetInfo->numPrograms; ++i)
                {
                    juce::String name = i < presetInfo->programNames.size()
                        ? presetInfo->programNames[i]
                        : juce::String("Preset ") + juce::String(i);
                    arr.append(QJsonObject{{"index", i}, {"name", QString::fromStdString(name.toStdString())}});
                }
                return McpToolResult::text(
                    QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
            }

            // Fallback: live query from instantiated plugin
            auto progs = e->getFxProgramList(ti, si);
            QJsonArray arr;
            for (const auto& p : progs)
                arr.append(QJsonObject{{"index", p.index}, {"name", QString::fromStdString(p.name)}});
            return McpToolResult::text(
                QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
        }});

    s.registerTool({"load_plugin_preset",
        "Load a preset/program by index on a plugin FX slot.",
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                  {"slotIndex", QJsonObject{{"type","integer"}}},
                  {"programIndex", QJsonObject{{"type","integer"}}}},
                 {"trackId","slotIndex","programIndex"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            int si = a.value("slotIndex").toInt();
            auto fxSlots = e->getReadModel().getFxSlots(ti);
            if (si < 0 || si >= (int)fxSlots.size())
                return McpToolResult::text("slot not found", true);
            if (fxSlots[si].fxType != "plugin")
                return McpToolResult::text("slot is not a plugin", true);
            int pi = a.value("programIndex").toInt();
            e->getPluginParamService().setCurrentProgram(ti, fxSlots[si].pluginId, pi);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"set_fx_bypass", "Bypass or unbypass an FX slot.",
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                  {"slotIndex", QJsonObject{{"type","integer"}}},
                  {"bypassed",  QJsonObject{{"type","boolean"}}}}, {"trackId","slotIndex","bypassed"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            int si = a.value("slotIndex").toInt();
            e->getProjectCommands().setFxSlotBypassed(ti, si, a.value("bypassed").toBool());
            return McpToolResult::text("ok");
        }});

    s.registerTool({"restart_fx", "Restart a crashed isolated plugin FX slot.",
        objSchema({{"trackIndex", QJsonObject{{"type","integer"}}},
                  {"slotIndex",  QJsonObject{{"type","integer"}}}}, {"trackIndex","slotIndex"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackIndex").toInt();
            int si = a.value("slotIndex").toInt();
            auto tl = e->getProjectModel().getTrackListTree();
            if (ti < 0 || ti >= tl.getNumChildren())
                return McpToolResult::text("track not found", true);
            e->getProjectCommands().respawnFxSlot(ti, si);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"list_fx_params", "List all automatable parameters of a plugin FX slot.",
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                  {"slotIndex", QJsonObject{{"type","integer"}}}}, {"trackId","slotIndex"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            int si = a.value("slotIndex").toInt();
            auto fxSlots = e->getReadModel().getFxSlots(ti);
            if (si < 0 || si >= (int)fxSlots.size())
                return McpToolResult::text("slot not found", true);
            if (fxSlots[si].fxType != "plugin")
                return McpToolResult::text("slot has no plugin", true);
            auto params = e->getPluginParamService().getParams(ti, fxSlots[si].pluginId);
            QJsonArray arr;
            for (const auto& pi : params) {
                QJsonObject o;
                o["index"] = pi.index;
                o["name"] = QString::fromStdString(pi.name);
                o["automatable"] = pi.automatable;
                o["value"] = static_cast<double>(pi.value);
                o["text"] = QString::fromStdString(pi.text);
                arr.append(o);
            }
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(QJsonObject{{"params", arr}}).toJson(QJsonDocument::Compact)));
        }});

    s.registerTool({"set_fx_param", "Set a plugin FX parameter value (normalized 0..1).",
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                  {"slotIndex", QJsonObject{{"type","integer"}}},
                  {"paramIndex",QJsonObject{{"type","integer"}}},
                  {"value",     QJsonObject{{"type","number"}}}}, {"trackId","slotIndex","paramIndex","value"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            int si = a.value("slotIndex").toInt();
            auto fxSlots = e->getReadModel().getFxSlots(ti);
            if (si < 0 || si >= (int)fxSlots.size())
                return McpToolResult::text("slot not found", true);
            if (fxSlots[si].fxType != "plugin")
                return McpToolResult::text("slot has no plugin", true);
            int pi = a.value("paramIndex").toInt();
            auto params = e->getPluginParamService().getParams(ti, fxSlots[si].pluginId);
            if (pi < 0 || pi >= static_cast<int>(params.size()))
                return McpToolResult::text("param index out of range", true);
            float v = static_cast<float>(a.value("value").toDouble());
            v = std::clamp(v, 0.0f, 1.0f);
            e->getPluginParamService().setParam(ti, fxSlots[si].pluginId, pi, v);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"load_plugin_preset_file",
        "Load a preset file into a plugin FX slot via setStateInformation. "
        "Supports .fxp (standard VST2 FPCh, Serum 2 layout) and .syx (DX7 SysEx) files.",
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                   {"slotIndex", QJsonObject{{"type","integer"}}},
                   {"filePath",  QJsonObject{{"type","string"}}}},
                  {"trackId","slotIndex","filePath"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            int si = a.value("slotIndex").toInt();
            auto fxSlots = e->getReadModel().getFxSlots(ti);
            if (si < 0 || si >= static_cast<int>(fxSlots.size()))
                return McpToolResult::text("slot not found", true);
            if (fxSlots[si].fxType != "plugin")
                return McpToolResult::text("slot is not a plugin", true);

            auto* proc = e->getMainProcessor();
            if (!proc) return McpToolResult::text("audio engine not initialized", true);
            auto* track = proc->getTrack(ti);
            if (!track) return McpToolResult::text("track not found", true);
            auto& chain = track->getFXChain();
            if (si < 0 || si >= static_cast<int>(chain.size()) || !chain[si])
                return McpToolResult::text("FX slot not found in chain", true);
            auto* slot = chain[si].get();
            if (!slot->isPlugin() || !slot->getPluginInstance())
                return McpToolResult::text("slot has no plugin instance", true);

            QString filePath = a.value("filePath").toString();
            if (filePath.isEmpty())
                return McpToolResult::text("filePath required", true);

            juce::File fxpFile(filePath.toStdString());
            if (!fxpFile.existsAsFile())
                return McpToolResult::text("file not found: " + filePath, true);

            juce::MemoryBlock raw;
            if (!fxpFile.loadFileAsData(raw))
                return McpToolResult::text("failed to read file", true);

            auto* bytes = static_cast<const uint8_t*>(raw.getData());

            // SysEx file (DX7 format: starts with F0 43)
            if (bytes[0] == 0xF0 && bytes[1] == 0x43) {
                slot->getPluginInstance()->setStateInformation(raw.getData(), static_cast<int>(raw.getSize()));

                auto& model = e->getProjectModel();
                auto& um = model.getUndoManager();
                auto slotTree = model.getTrackListTree().getChild(ti)
                    .getChildWithName(IDs::FX_CHAIN).getChild(si);
                if (slotTree.isValid()) {
                    slotTree.setProperty(IDs::pluginState, raw.toBase64Encoding(), &um);
                }
                return McpToolResult::text("ok");
            }

            // FXP file (starts with CcnK)
            if (raw.getSize() < 60)
                return McpToolResult::text("file too small for FXP header", true);

            uint32_t chunkMagic = (bytes[0] << 24) | (bytes[1] << 16) | (bytes[2] << 8) | bytes[3];
            uint32_t fxMagic    = (bytes[8] << 24) | (bytes[9] << 16) | (bytes[10] << 8) | bytes[11];

            if (chunkMagic != 0x43636e4b)
                return McpToolResult::text("not a valid FXP or SysEx file", true);
            if (fxMagic != 0x46504368)
                return McpToolResult::text("not a chunk-based FXP (bad FPCh magic)", true);

            const uint8_t* chunkData = nullptr;
            size_t chunkSize = 0;

            auto readBE32 = [](const uint8_t* p) -> uint32_t {
                return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
            };

            if (raw.getSize() >= 60) {
                uint32_t cs2 = readBE32(bytes + 56);
                if (cs2 > 0 && 60 + cs2 == raw.getSize()) {
                    chunkData = bytes + 60;
                    chunkSize = cs2;
                }
            }

            if (!chunkData && raw.getSize() >= 56) {
                uint32_t cs1 = readBE32(bytes + 52);
                if (cs1 > 0 && 56 + cs1 == raw.getSize()) {
                    chunkData = bytes + 56;
                    chunkSize = cs1;
                }
            }

            if (!chunkData || chunkSize == 0)
                return McpToolResult::text("could not locate chunk data in FXP file", true);

            slot->getPluginInstance()->setStateInformation(chunkData, static_cast<int>(chunkSize));

            auto& model = e->getProjectModel();
            auto& um = model.getUndoManager();
            auto slotTree = model.getTrackListTree().getChild(ti)
                .getChildWithName(IDs::FX_CHAIN).getChild(si);
            if (slotTree.isValid()) {
                juce::MemoryBlock stateBlock(chunkData, chunkSize);
                slotTree.setProperty(IDs::pluginState, stateBlock.toBase64Encoding(), &um);
            }

            return McpToolResult::text("ok");
        }});

    s.registerTool({"set_internal_fx_param",
        "Set an internal (non-plugin) FX parameter value. Works for eq, compressor, reverb, delay, chorus, flanger, phaser, and sampler.",
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                  {"slotIndex", QJsonObject{{"type","integer"}}},
                  {"paramIndex",QJsonObject{{"type","integer"}}},
                  {"value",     QJsonObject{{"type","number"}}}}, {"trackId","slotIndex","paramIndex","value"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            int si = a.value("slotIndex").toInt();
            auto fxSlots = e->getReadModel().getFxSlots(ti);
            if (si < 0 || si >= static_cast<int>(fxSlots.size()))
                return McpToolResult::text("slot not found", true);
            if (fxSlots[si].fxType == "plugin" || fxSlots[si].fxType == "none")
                return McpToolResult::text("slot is not an internal FX", true);
            int pi = a.value("paramIndex").toInt();
            float v = static_cast<float>(a.value("value").toDouble());
            e->getProjectCommands().setFxSlotParam(ti, si, pi, v);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"sampler_set_sample",
        "Load an audio file into a sampler FX slot.",
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                  {"slotIndex", QJsonObject{{"type","integer"}}},
                  {"filePath",  QJsonObject{{"type","string"}}},
                  {"rootNote",  QJsonObject{{"type","integer"}}}},
                  {"trackId","slotIndex","filePath"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            int si = a.value("slotIndex").toInt();
            auto fxSlots = e->getReadModel().getFxSlots(ti);
            if (si < 0 || si >= static_cast<int>(fxSlots.size()))
                return McpToolResult::text("slot not found", true);
            if (fxSlots[si].fxType != "sampler")
                return McpToolResult::text("slot is not a sampler", true);

            QString filePath = a.value("filePath").toString();
            if (filePath.isEmpty())
                return McpToolResult::text("filePath required", true);
            juce::File file(filePath.toStdString());
            if (!file.existsAsFile())
                return McpToolResult::text("file not found: " + filePath, true);

            int root = a.value("rootNote").toInt(60);
            e->getProjectCommands().setSamplerSample(ti, si, filePath.toStdString(), root);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"sampler_get_state",
        "Get the current state of a sampler FX slot.",
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                  {"slotIndex", QJsonObject{{"type","integer"}}}},
                  {"trackId","slotIndex"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            int si = a.value("slotIndex").toInt();
            auto fxSlots = e->getReadModel().getFxSlots(ti);
            if (si < 0 || si >= static_cast<int>(fxSlots.size()))
                return McpToolResult::text("slot not found", true);
            if (fxSlots[si].fxType != "sampler")
                return McpToolResult::text("slot is not a sampler", true);

            auto& model = e->getProjectModel();
            auto slotTree = model.getTrackListTree().getChild(ti)
                .getChildWithName(IDs::FX_CHAIN).getChild(si);
            if (!slotTree.isValid())
                return McpToolResult::text("slot tree not found", true);

            QJsonObject state;
            state["sampleFile"] = QString::fromStdString(slotTree.getProperty("sampleFile", "").toString().toStdString());
            state["mode"] = QString::fromStdString(slotTree.getProperty("mode", "classic").toString().toStdString());
            state["rootNote"] = static_cast<int>(slotTree.getProperty("rootNote", 60));
            state["transpose"] = static_cast<int>(slotTree.getProperty("transpose", 0));
            state["mono"] = static_cast<bool>(slotTree.getProperty("mono", false));
            state["playReverse"] = static_cast<bool>(slotTree.getProperty("playReverse", false));

            QJsonObject env;
            env["attack"] = static_cast<double>(slotTree.getProperty("param_0", 0.005));
            env["decay"] = static_cast<double>(slotTree.getProperty("param_1", 0.1));
            env["sustain"] = static_cast<double>(slotTree.getProperty("param_2", 0.9));
            env["release"] = static_cast<double>(slotTree.getProperty("param_3", 0.1));
            state["envelope"] = env;

            auto* proc = e->getMainProcessor();
            if (proc)
            {
                auto* track = proc->getTrack(ti);
                if (track)
                {
                    auto& chain = track->getFXChain();
                    if (si < static_cast<int>(chain.size()) && chain[si])
                    {
                        auto* engine = chain[si]->samplerEngineForTest();
                        state["hasSound"] = (engine != nullptr && engine->currentSound() != nullptr);
                        if (engine)
                            state["activeVoices"] = engine->activeVoiceCount();
                    }
                }
            }

            return McpToolResult::text(
                QString::fromUtf8(QJsonDocument(state).toJson(QJsonDocument::Compact)));
        }});
}

static void registerAutomationTools(McpServer& s, AudioEngine* e)
{
    s.registerTool({"add_automation_point", "Add a point to an automation lane (paramID integer preferred; name accepted).",
        objSchema({{"trackId", QJsonObject{{"type","integer"}}},
                  {"lane",   QJsonObject{{"oneOf", QJsonArray{
                      QJsonObject{{"type","integer"}},
                      QJsonObject{{"type","string"}}}}}},
                  {"time",   QJsonObject{{"type","number"}}},
                  {"value",  QJsonObject{{"type","number"}}}}, {"trackId","lane","time","value"}),
        [e](const QJsonObject& a) -> McpToolResult {
            auto lane = findLane(e, a.value("trackId").toInt(), a.value("lane"));
            if (!lane.isValid()) return McpToolResult::text("lane not found", true);
            auto& um = e->getProjectModel().getUndoManager();
            auto pl = lane.getChildWithName(IDs::POINT_LIST);
            if (!pl.isValid()) { pl = juce::ValueTree(IDs::POINT_LIST); lane.addChild(pl, -1, &um); }
            juce::ValueTree pt(IDs::POINT);
            // MCP boundary speaks beats; the ValueTree stores seconds.
            double bpm = e->getProjectModel().getTree().getProperty(IDs::tempo, 120.0);
            pt.setProperty(IDs::startTime, HDAW::beatsToSeconds(a.value("time").toDouble(), bpm), &um);
            pt.setProperty(IDs::gain, a.value("value").toDouble(), &um);
            pl.addChild(pt, -1, &um);
            if (auto* proc = e->getMainProcessor())
                proc->rebuildAutomationCache(a.value("trackId").toInt());
            return McpToolResult::text("ok");
        }});

    s.registerTool({"set_automation_enabled", "Enable or disable an automation lane.",
        objSchema({{"trackId", QJsonObject{{"type","integer"}}},
                  {"lane",   QJsonObject{{"oneOf", QJsonArray{
                      QJsonObject{{"type","integer"}},
                      QJsonObject{{"type","string"}}}}}},
                  {"enabled",QJsonObject{{"type","boolean"}}}}, {"trackId","lane","enabled"}),
        [e](const QJsonObject& a) -> McpToolResult {
            auto lane = findLane(e, a.value("trackId").toInt(), a.value("lane"));
            if (!lane.isValid()) return McpToolResult::text("lane not found", true);
            lane.setProperty(IDs::automationEnabled, a.value("enabled").toBool(),
                             &e->getProjectModel().getUndoManager());
            if (auto* proc = e->getMainProcessor())
                proc->rebuildAutomationCache(a.value("trackId").toInt());
            return McpToolResult::text("ok");
        }});

    // add_automation_lane / remove_automation_lane â€” the lane-authoring surface.
    // paramID 0 leaves the lane unbound (legacy default); for FX-parameter
    // automation pass the compound id (100 + slotIndex*100 + paramIndex).
    // Mirrors project.addAutomationLane / project.removeAutomationLane so the
    // UI and MCP share one command path (AGENTS.md feature-parity contract).
    s.registerTool({"add_automation_lane", "Create an automation lane, optionally bound to a target paramID (1=volume, 2=pan, 3=mute, or 100+slotIndex*100+paramIndex for a plugin FX param).",
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                  {"laneName",  QJsonObject{{"type","string"}}},
                  {"paramID",   QJsonObject{{"type","integer"}}}}, {"trackId","laneName"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int trackId = a.value("trackId").toInt(-1);
            QString laneNameQ = a.value("laneName").toString();
            if (laneNameQ.isEmpty()) return McpToolResult::text("laneName required", true);
            int paramID = a.value("paramID").toInt(0);
            e->getProjectCommands().addAutomationLane(
                trackId, laneNameQ.toUtf8().constData(), paramID);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"remove_automation_lane", "Remove an automation lane (by paramID integer, or by name string).",
        objSchema({{"trackId", QJsonObject{{"type","integer"}}},
                  {"lane",   QJsonObject{{"oneOf", QJsonArray{
                      QJsonObject{{"type","integer"}},
                      QJsonObject{{"type","string"}}}}}}}, {"trackId","lane"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int trackId = a.value("trackId").toInt(-1);
            auto ref = a.value("lane");
            // Resolve the lane by paramID/name, then delete by its name (the
            // command path addresses lanes by name; findLane handles both).
            auto lane = findLane(e, trackId, ref);
            if (!lane.isValid()) return McpToolResult::text("lane not found", true);
            std::string name = lane.getProperty(IDs::name, "").toString().toStdString();
            e->getProjectCommands().removeAutomationLane(trackId, name);
            return McpToolResult::text("ok");
        }});
}

static void registerSendTools(McpServer& s, AudioEngine* e)
{
    s.registerTool({"get_track_sends", "List all sends on a track.",
        objSchema({{"trackId", QJsonObject{{"type","integer"}}}}, {"trackId"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt(-1);
            if (!e->getMainProcessor()) return McpToolResult::text("engine not ready", true);
            auto sends = e->getReadModel().getTrackSends(ti);
            QJsonArray arr;
            for (const auto& s : sends) {
                arr.append(QJsonObject{
                    {"sendIndex", s.sendIndex},
                    {"level", static_cast<double>(s.level)},
                    {"isPreFader", s.isPreFader},
                    {"bypassed", s.bypassed},
                });
            }
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(arr).toJson(QJsonDocument::Compact)));
        }});

    s.registerTool({"set_track_send_level", "Set the level of a send.",
        objSchema({{"trackId", QJsonObject{{"type","integer"}}},
                  {"sendIndex", QJsonObject{{"type","integer"}}},
                  {"level", QJsonObject{{"type","number"}}}}, {"trackId","sendIndex","level"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt(-1);
            int si = a.value("sendIndex").toInt(-1);
            float lv = static_cast<float>(a.value("level").toDouble());
            e->getProjectCommands().setTrackSendLevel(ti, si, lv);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"set_track_send_mode", "Set send mode: pre or post fader.",
        objSchema({{"trackId", QJsonObject{{"type","integer"}}},
                  {"sendIndex", QJsonObject{{"type","integer"}}},
                  {"isPreFader", QJsonObject{{"type","boolean"}}}}, {"trackId","sendIndex","isPreFader"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt(-1);
            int si = a.value("sendIndex").toInt(-1);
            bool pre = a.value("isPreFader").toBool();
            e->getProjectCommands().setTrackSendMode(ti, si, pre);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"set_track_send_bypassed", "Bypass or unbypass a send.",
        objSchema({{"trackId", QJsonObject{{"type","integer"}}},
                  {"sendIndex", QJsonObject{{"type","integer"}}},
                  {"bypassed", QJsonObject{{"type","boolean"}}}}, {"trackId","sendIndex","bypassed"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt(-1);
            int si = a.value("sendIndex").toInt(-1);
            bool b = a.value("bypassed").toBool();
            e->getProjectCommands().setTrackSendBypassed(ti, si, b);
            return McpToolResult::text("ok");
        }});
}

static void registerMidiFxTools(McpServer& s, AudioEngine* e)
{
    s.registerTool({"add_midi_fx",
        "Add a MIDI FX slot to a track.",
        objSchema({{"trackId", QJsonObject{{"type","integer"}}},
                   {"fxType", QJsonObject{{"type","string"},
                       {"enum", QJsonArray{"arpeggiator","velocity","chord","scale","notelength",
                                           "transpose","keyfilter","multinote","velocitycurve",
                                           "notechance","mididelay","humanize","strum"}}}},
                   {"position", QJsonObject{{"type","integer"}}}}, {"trackId","fxType"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            auto tl = e->getProjectModel().getTrackListTree();
            if (ti < 0 || ti >= tl.getNumChildren())
                return McpToolResult::text("track not found", true);
            std::string type = a.value("fxType").toString().toStdString();
            int pos = a.value("position").toInt(-1);
            e->getProjectCommands().addMidiFxSlot(ti, type, pos);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"remove_midi_fx",
        "Remove a MIDI FX slot from a track.",
        objSchema({{"trackId", QJsonObject{{"type","integer"}}},
                   {"slotIndex", QJsonObject{{"type","integer"}}}}, {"trackId","slotIndex"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            int si = a.value("slotIndex").toInt();
            e->getProjectCommands().removeMidiFxSlot(ti, si);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"set_midi_fx_bypass",
        "Bypass or unbypass a MIDI FX slot.",
        objSchema({{"trackId", QJsonObject{{"type","integer"}}},
                   {"slotIndex", QJsonObject{{"type","integer"}}},
                   {"bypassed", QJsonObject{{"type","boolean"}}}}, {"trackId","slotIndex","bypassed"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            int si = a.value("slotIndex").toInt();
            bool b = a.value("bypassed").toBool();
            e->getProjectCommands().setMidiFxSlotBypassed(ti, si, b);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"set_midi_fx_param",
        "Set a parameter on a MIDI FX slot.",
        objSchema({{"trackId", QJsonObject{{"type","integer"}}},
                   {"slotIndex", QJsonObject{{"type","integer"}}},
                   {"paramName", QJsonObject{{"type","string"}}},
                   {"value", QJsonObject{{"type","number"}}}}, {"trackId","slotIndex","paramName","value"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            int si = a.value("slotIndex").toInt();
            std::string pn = a.value("paramName").toString().toStdString();
            double v = a.value("value").toDouble();
            e->getProjectCommands().setMidiFxSlotParam(ti, si, pn, v);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"set_midi_fx_param_normalized",
        "Set a MIDI FX parameter by normalized value (0..1) for real-time modulation. "
        "Bypasses the ValueTree — use set_midi_fx_param for persistent changes.",
        objSchema({{"trackId",     QJsonObject{{"type","integer"}}},
                   {"slotIndex",   QJsonObject{{"type","integer"}}},
                   {"paramIndex",  QJsonObject{{"type","integer"}}},
                   {"value",       QJsonObject{{"type","number"},{"minimum",0.0},{"maximum",1.0}}}},
                  {"trackId","slotIndex","paramIndex","value"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int trackId = a.value("trackId").toInt();
            int slotIndex = a.value("slotIndex").toInt();
            int paramIndex = a.value("paramIndex").toInt();
            float value = static_cast<float>(a.value("value").toDouble());

            auto* proc = e->getMainProcessor();
            if (!proc) return McpToolResult::text("No audio processor", true);
            auto* track = proc->getTrack(trackId);
            if (!track) return McpToolResult::text("Track not found", true);

            auto& chain = track->getMidiFxChain();
            if (slotIndex < 0 || slotIndex >= static_cast<int>(chain.size()) || !chain[slotIndex])
                return McpToolResult::text("MIDI FX slot not found", true);

            chain[slotIndex]->setAutomationParam(paramIndex, value);
            return McpToolResult::text("ok");
        }});
}

static void registerEnvelopeTools(McpServer& s, AudioEngine* e)
{
    s.registerTool({"list_envelope_shapes",
        "List all available envelope generator shapes.",
        objSchema({}),
        [e](const QJsonObject&) -> McpToolResult {
            QJsonArray arr;
            auto addShape = [&](const char* name, const char* desc) {
                arr.append(QJsonObject{{"name", name}, {"description", desc}});
            };
            addShape("ramp", "Linear ramp between two values");
            addShape("adsr", "Attack-Decay-Sustain-Release envelope");
            addShape("sine", "Sine wave LFO");
            addShape("triangle", "Triangle wave LFO");
            addShape("saw", "Sawtooth wave LFO");
            addShape("square", "Square wave LFO");
            addShape("pulse", "Pulse wave (50% duty)");
            addShape("staircase", "Stepped quantization");
            addShape("sCurve", "S-curve (smooth step)");
            addShape("randomWalk", "Seeded random walk");
            addShape("noise", "Seeded uniform noise");
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(QJsonObject{{"shapes", arr}}).toJson(QJsonDocument::Compact)));
        }});

    s.registerTool({"generate_automation_envelope",
        "Generate an envelope shape on an automation lane.",
        objSchema({{"trackId",  QJsonObject{{"type","integer"}}},
                   {"lane",     QJsonObject{{"oneOf", QJsonArray{
                       QJsonObject{{"type","integer"}},
                       QJsonObject{{"type","string"}}}}}},
                   {"shape",    QJsonObject{{"type","string"}}},
                   {"start",    QJsonObject{{"type","number"}}},
                   {"end",      QJsonObject{{"type","number"}}},
                   {"startValue", QJsonObject{{"type","number"}}},
                   {"endValue", QJsonObject{{"type","number"}}},
                   {"cycles",   QJsonObject{{"type","number"}}},
                   {"steps",    QJsonObject{{"type","integer"}}},
                   {"phase",    QJsonObject{{"type","number"}}},
                   {"density",  QJsonObject{{"type","number"}}},
                   {"smooth",   QJsonObject{{"type","number"}}},
                   {"seed",     QJsonObject{{"type","integer"}}}},
                  {"trackId","lane","shape"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int trackId = a.value("trackId").toInt(-1);
            auto laneRef = a.value("lane");
            auto lane = findLane(e, trackId, laneRef);
            if (!lane.isValid()) return McpToolResult::text("lane not found", true);
            QString shapeStr = a.value("shape").toString();
            auto shape = parseEnvelopeShape(shapeStr);
            if (!shape) return McpToolResult::text("unknown shape: " + shapeStr, true);
            std::string laneName = lane.getProperty(IDs::name, "").toString().toStdString();

            HDAW::EnvelopeGenerator::Params params;
            params.shape = *shape;
            params.startTime = a.value("start").toDouble(0.0);
            params.endTime = a.value("end").toDouble(16.0);
            params.startValue = a.value("startValue").toDouble(0.0);
            params.endValue = a.value("endValue").toDouble(1.0);
            params.cycles = a.value("cycles").toDouble(1.0);
            params.steps = a.value("steps").toInt(8);
            params.phase = a.value("phase").toDouble(0.0);
            params.densityPerSec = a.value("density").toDouble(8.0);
            params.smooth = a.value("smooth").toDouble(0.0);
            params.seed = static_cast<uint64_t>(a.value("seed").toInt(0));

            e->getProjectCommands().generateAutomationEnvelope(trackId, laneName, params);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"generate_clip_gain_envelope",
        "Generate an envelope shape on a clip's gain envelope.",
        objSchema({{"clipId",     QJsonObject{{"type","integer"}}},
                   {"shape",      QJsonObject{{"type","string"}}},
                   {"start",      QJsonObject{{"type","number"}}},
                   {"end",        QJsonObject{{"type","number"}}},
                   {"startValue", QJsonObject{{"type","number"}}},
                   {"endValue",   QJsonObject{{"type","number"}}},
                   {"cycles",     QJsonObject{{"type","number"}}},
                   {"steps",      QJsonObject{{"type","integer"}}},
                   {"phase",      QJsonObject{{"type","number"}}},
                   {"density",    QJsonObject{{"type","number"}}},
                   {"smooth",     QJsonObject{{"type","number"}}},
                   {"seed",       QJsonObject{{"type","integer"}}}},
                  {"clipId","shape"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int clipId = a.value("clipId").toInt(-1);
            auto clip = findClip(e, clipId, nullptr);
            if (!clip.isValid()) return McpToolResult::text("clip not found", true);
            QString shapeStr = a.value("shape").toString();
            auto shape = parseEnvelopeShape(shapeStr);
            if (!shape) return McpToolResult::text("unknown shape: " + shapeStr, true);

            HDAW::EnvelopeGenerator::Params params;
            params.shape = *shape;
            params.startTime = a.value("start").toDouble(0.0);
            params.endTime = a.value("end").toDouble(16.0);
            params.startValue = a.value("startValue").toDouble(0.0);
            params.endValue = a.value("endValue").toDouble(2.0);
            params.cycles = a.value("cycles").toDouble(1.0);
            params.steps = a.value("steps").toInt(8);
            params.phase = a.value("phase").toDouble(0.0);
            params.densityPerSec = a.value("density").toDouble(8.0);
            params.smooth = a.value("smooth").toDouble(0.0);
            params.seed = static_cast<uint64_t>(a.value("seed").toInt(0));

            e->getProjectCommands().generateClipGainEnvelope(clipId, params);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"generate_clip_cc_lane",
        "Generate an envelope shape on a MIDI clip's CC lane.",
        objSchema({{"clipId",           QJsonObject{{"type","integer"}}},
                   {"controllerNumber", QJsonObject{{"type","integer"}}},
                   {"shape",            QJsonObject{{"type","string"}}},
                   {"start",            QJsonObject{{"type","number"}}},
                   {"end",              QJsonObject{{"type","number"}}},
                   {"startValue",       QJsonObject{{"type","number"}}},
                   {"endValue",         QJsonObject{{"type","number"}}},
                   {"cycles",           QJsonObject{{"type","number"}}},
                   {"steps",            QJsonObject{{"type","integer"}}},
                   {"phase",            QJsonObject{{"type","number"}}},
                   {"density",          QJsonObject{{"type","number"}}},
                   {"smooth",           QJsonObject{{"type","number"}}},
                   {"seed",             QJsonObject{{"type","integer"}}}},
                  {"clipId","controllerNumber","shape"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int clipId = a.value("clipId").toInt(-1);
            auto clip = findClip(e, clipId, nullptr);
            if (!clip.isValid()) return McpToolResult::text("clip not found", true);
            int controllerNumber = a.value("controllerNumber").toInt(-1);
            if (controllerNumber < 0 || controllerNumber > 127)
                return McpToolResult::text("controllerNumber must be 0-127", true);
            QString shapeStr = a.value("shape").toString();
            auto shape = parseEnvelopeShape(shapeStr);
            if (!shape) return McpToolResult::text("unknown shape: " + shapeStr, true);

            HDAW::EnvelopeGenerator::Params params;
            params.shape = *shape;
            params.startTime = a.value("start").toDouble(0.0);
            params.endTime = a.value("end").toDouble(16.0);
            params.startValue = a.value("startValue").toDouble(0.0);
            params.endValue = a.value("endValue").toDouble(127.0);
            params.cycles = a.value("cycles").toDouble(1.0);
            params.steps = a.value("steps").toInt(8);
            params.phase = a.value("phase").toDouble(0.0);
            params.densityPerSec = a.value("density").toDouble(8.0);
            params.smooth = a.value("smooth").toDouble(0.0);
            params.seed = static_cast<uint64_t>(a.value("seed").toInt(0));

            e->getProjectCommands().generateClipCcLane(clipId, controllerNumber, params);
            return McpToolResult::text("ok");
        }});
}

void registerAudioDomain(McpServer& s, AudioEngine* e)
{
    registerAudioReadTools(s, e);
    registerFxTools(s, e);
    registerMidiFxTools(s, e);
    registerAutomationTools(s, e);
    registerSendTools(s, e);
    registerEnvelopeTools(s, e);
}

} // namespace mcp
