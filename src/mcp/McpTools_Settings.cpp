#include "McpTools.h"
#include "McpTools_Private.h"
#include "McpServer.h"
#include "McpToolDef.h"
#include "../engine/AudioEngine.h"
#include "../engine/AudioPreviewPlayer.h"
#include "../engine/MainAudioProcessor.h"
#include "../engine/TrackFXSlot.h"
#include "../common/SettingsKeys.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QSettings>

namespace mcp {

// ── Audio Device Config (11 tools) ──────────────────────────────────────────

static void registerAudioDeviceTools(McpServer& s, AudioEngine* e)
{
    s.registerTool({"get_audio_device_types",
        "List available audio driver types.",
        objSchema({}),
        "settings",
        [e](const QJsonObject&) -> McpToolResult {
            QJsonArray arr;
            for (auto* type : e->getDeviceManager().getAvailableDeviceTypes())
                arr.append(QString::fromUtf8(type->getTypeName().toRawUTF8()));
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(arr).toJson(QJsonDocument::Compact)));
        }});

    s.registerTool({"get_audio_output_devices",
        "List output devices for the current audio driver.",
        objSchema({}),
        "settings",
        [e](const QJsonObject&) -> McpToolResult {
            QJsonArray arr;
            auto* devType = e->getDeviceManager().getCurrentDeviceTypeObject();
            if (devType != nullptr)
                for (const auto& name : devType->getDeviceNames(false))
                    arr.append(QString::fromUtf8(name.toRawUTF8()));
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(arr).toJson(QJsonDocument::Compact)));
        }});

    s.registerTool({"get_audio_input_devices",
        "List input devices for the current audio driver.",
        objSchema({}),
        "settings",
        [e](const QJsonObject&) -> McpToolResult {
            QJsonArray arr;
            auto* devType = e->getDeviceManager().getCurrentDeviceTypeObject();
            if (devType != nullptr)
                for (const auto& name : devType->getDeviceNames(true))
                    arr.append(QString::fromUtf8(name.toRawUTF8()));
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(arr).toJson(QJsonDocument::Compact)));
        }});

    s.registerTool({"get_audio_current_setup",
        "Get current audio device setup (driver, devices, sample rate, buffer size, latency).",
        objSchema({}),
        "settings",
        [e](const QJsonObject&) -> McpToolResult {
            auto& dm = e->getDeviceManager();
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
            return McpToolResult::text(QString::fromUtf8(QJsonDocument(QJsonObject{
                {"driver",     QString::fromUtf8(dm.getCurrentAudioDeviceType().toRawUTF8())},
                {"output",     QString::fromUtf8(setup.outputDeviceName.toRawUTF8())},
                {"input",      QString::fromUtf8(setup.inputDeviceName.toRawUTF8())},
                {"sampleRate", sr},
                {"bufferSize", bs},
                {"latencyMs",  latencyMs}
            }).toJson(QJsonDocument::Compact)));
        }});

    s.registerTool({"get_audio_sample_rates",
        "List available sample rates for the current audio device.",
        objSchema({}),
        "settings",
        [e](const QJsonObject&) -> McpToolResult {
            QJsonArray arr;
            auto* dev = e->getDeviceManager().getCurrentAudioDevice();
            if (dev != nullptr)
                for (double rate : dev->getAvailableSampleRates())
                    arr.append(rate);
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(arr).toJson(QJsonDocument::Compact)));
        }});

    s.registerTool({"get_audio_buffer_sizes",
        "List available buffer sizes for the current audio device.",
        objSchema({}),
        "settings",
        [e](const QJsonObject&) -> McpToolResult {
            QJsonArray arr;
            auto* dev = e->getDeviceManager().getCurrentAudioDevice();
            if (dev != nullptr)
                for (int buf : dev->getAvailableBufferSizes())
                    arr.append(buf);
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(arr).toJson(QJsonDocument::Compact)));
        }});

    s.registerTool({"set_audio_device_type",
        "Switch the audio driver type.",
        objSchema({{"type", QJsonObject{{"type","string"}}}}, {"type"}),
        "settings",
        [e](const QJsonObject& a) -> McpToolResult {
            auto& dm = e->getDeviceManager();
            std::string type = a.value("type").toString().toStdString();
            dm.setCurrentAudioDeviceType(juce::String(type), true);
            QSettings st;
            st.setValue(SettingsKeys::kKeyAudioDriver, QString::fromStdString(type));
            return McpToolResult::text("ok");
        }});

    s.registerTool({"set_audio_output_device",
        "Switch the audio output device.",
        objSchema({{"name", QJsonObject{{"type","string"}}}}, {"name"}),
        "settings",
        [e](const QJsonObject& a) -> McpToolResult {
            auto& dm = e->getDeviceManager();
            std::string name = a.value("name").toString().toStdString();
            auto setup = dm.getAudioDeviceSetup();
            setup.outputDeviceName = juce::String(name);
            dm.setAudioDeviceSetup(setup, true);
            QSettings st;
            st.setValue(SettingsKeys::kKeyAudioOutputDevice, QString::fromStdString(name));
            return McpToolResult::text("ok");
        }});

    s.registerTool({"set_audio_input_device",
        "Switch the audio input device.",
        objSchema({{"name", QJsonObject{{"type","string"}}}}, {"name"}),
        "settings",
        [e](const QJsonObject& a) -> McpToolResult {
            auto& dm = e->getDeviceManager();
            std::string name = a.value("name").toString().toStdString();
            auto setup = dm.getAudioDeviceSetup();
            setup.inputDeviceName = juce::String(name);
            dm.setAudioDeviceSetup(setup, true);
            QSettings st;
            st.setValue(SettingsKeys::kKeyAudioInputDevice, QString::fromStdString(name));
            return McpToolResult::text("ok");
        }});

    s.registerTool({"set_audio_sample_rate",
        "Set the audio sample rate.",
        objSchema({{"rate", QJsonObject{{"type","number"}}}}, {"rate"}),
        "settings",
        [e](const QJsonObject& a) -> McpToolResult {
            auto& dm = e->getDeviceManager();
            double rate = a.value("rate").toDouble();
            auto setup = dm.getAudioDeviceSetup();
            setup.sampleRate = rate;
            dm.setAudioDeviceSetup(setup, true);
            QSettings st;
            st.setValue(SettingsKeys::kKeyAudioSampleRate, static_cast<qint64>(rate));
            return McpToolResult::text("ok");
        }});

    s.registerTool({"set_audio_buffer_size",
        "Set the audio buffer size.",
        objSchema({{"size", QJsonObject{{"type","integer"}}}}, {"size"}),
        "settings",
        [e](const QJsonObject& a) -> McpToolResult {
            auto& dm = e->getDeviceManager();
            int size = a.value("size").toInt();
            auto setup = dm.getAudioDeviceSetup();
            setup.bufferSize = size;
            dm.setAudioDeviceSetup(setup, true);
            QSettings st;
            st.setValue(SettingsKeys::kKeyAudioBufferSize, size);
            return McpToolResult::text("ok");
        }});
}

// ── MIDI Device Selection (4 tools) ─────────────────────────────────────────

static void registerMidiDeviceTools(McpServer& s, AudioEngine* e)
{
    s.registerTool({"get_midi_devices",
        "List available MIDI input devices.",
        objSchema({}),
        "settings",
        [e](const QJsonObject&) -> McpToolResult {
            auto arr = QJsonArray();
            for (const auto& d : e->getMidiService().getAvailableDevices())
                arr.append(QString::fromStdString(d));
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(arr).toJson(QJsonDocument::Compact)));
        }});

    s.registerTool({"get_open_midi_device",
        "Get the currently open MIDI device name.",
        objSchema({}),
        "settings",
        [e](const QJsonObject&) -> McpToolResult {
            return McpToolResult::text(
                QString::fromStdString(e->getMidiService().getOpenDevice()));
        }});

    s.registerTool({"open_midi_device",
        "Open a MIDI input device by identifier.",
        objSchema({{"identifier", QJsonObject{{"type","string"}}}}, {"identifier"}),
        "settings",
        [e](const QJsonObject& a) -> McpToolResult {
            bool ok = e->getMidiService().openDevice(
                a.value("identifier").toString().toStdString());
            return McpToolResult::text(ok ? "ok" : "failed");
        }});

    s.registerTool({"close_midi_device",
        "Close the currently open MIDI device.",
        objSchema({}),
        "settings",
        [e](const QJsonObject&) -> McpToolResult {
            e->getMidiService().closeDevice();
            return McpToolResult::text("ok");
        }});
}

// ── Metronome (1 tool) ──────────────────────────────────────────────────────

static void registerMetronomeTools(McpServer& s, AudioEngine* e)
{
    s.registerTool({"set_metronome_enabled",
        "Enable or disable the metronome.",
        objSchema({{"enabled", QJsonObject{{"type","boolean"}}}}, {"enabled"}),
        "settings",
        [e](const QJsonObject& a) -> McpToolResult {
            e->getProjectCommands().setMetronomeEnabled(a.value("enabled").toBool());
            return McpToolResult::text("ok");
        }});
}

// ── Punch (1 tool) ──────────────────────────────────────────────────────────

static void registerPunchTools(McpServer& s, AudioEngine* e)
{
    s.registerTool({"set_punch_enabled",
        "Enable or disable punch recording.",
        objSchema({{"enabled", QJsonObject{{"type","boolean"}}}}, {"enabled"}),
        "settings",
        [e](const QJsonObject& a) -> McpToolResult {
            e->getTransportCommands().setPunchEnabled(a.value("enabled").toBool());
            return McpToolResult::text("ok");
        }});
}

// ── Preview Player (7 tools) ────────────────────────────────────────────────

static void registerPreviewTools(McpServer& s, AudioEngine* e)
{
    s.registerTool({"preview_load",
        "Load a file into the preview player.",
        objSchema({{"filePath", QJsonObject{{"type","string"}}}}, {"filePath"}),
        "settings",
        [e](const QJsonObject& a) -> McpToolResult {
            QString filePath = a.value("filePath").toString();
            if (filePath.isEmpty())
                return McpToolResult::text("filePath required", true);
            e->getPreviewPlayer().loadFile(juce::File(filePath.toStdString()));
            return McpToolResult::text("ok");
        }});

    s.registerTool({"preview_play",
        "Start preview playback.",
        objSchema({}),
        "settings",
        [e](const QJsonObject&) -> McpToolResult {
            e->getPreviewPlayer().play();
            return McpToolResult::text("ok");
        }});

    s.registerTool({"preview_stop",
        "Stop preview playback.",
        objSchema({}),
        "settings",
        [e](const QJsonObject&) -> McpToolResult {
            e->getPreviewPlayer().stop();
            return McpToolResult::text("ok");
        }});

    s.registerTool({"preview_set_volume",
        "Set preview player volume (0.0 to 1.0).",
        objSchema({{"volume", QJsonObject{{"type","number"},
            {"minimum",0.0},{"maximum",1.0}}}}, {"volume"}),
        "settings",
        [e](const QJsonObject& a) -> McpToolResult {
            float vol = static_cast<float>(a.value("volume").toDouble());
            e->getPreviewPlayer().setVolume(vol);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"preview_set_tempo_match",
        "Enable or disable tempo-matched preview.",
        objSchema({{"enabled", QJsonObject{{"type","boolean"}}},
                   {"fileBpm", QJsonObject{{"type","number"}}}}, {"enabled"}),
        "settings",
        [e](const QJsonObject& a) -> McpToolResult {
            bool enabled = a.value("enabled").toBool();
            double fileBpm = a.value("fileBpm").toDouble(0.0);
            e->getPreviewPlayer().setTempoMatch(enabled, fileBpm);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"preview_set_project_bpm",
        "Set the project BPM for preview tempo matching.",
        objSchema({{"bpm", QJsonObject{{"type","number"},
            {"minimum",1.0},{"maximum",999.0}}}}, {"bpm"}),
        "settings",
        [e](const QJsonObject& a) -> McpToolResult {
            e->getPreviewPlayer().setProjectBpm(a.value("bpm").toDouble());
            return McpToolResult::text("ok");
        }});

    s.registerTool({"preview_is_playing",
        "Check if preview is currently playing.",
        objSchema({}),
        "settings",
        [e](const QJsonObject&) -> McpToolResult {
            return McpToolResult::text(
                e->getPreviewPlayer().isPlaying() ? "true" : "false");
        }});
}

// ── Plugin Blacklist (4 tools) ──────────────────────────────────────────────

static void registerPluginBlacklistTools(McpServer& s, AudioEngine* e)
{
    s.registerTool({"is_plugin_blacklisted",
        "Check if a plugin is blacklisted.",
        objSchema({{"pluginId", QJsonObject{{"type","string"}}}}, {"pluginId"}),
        "settings",
        [e](const QJsonObject& a) -> McpToolResult {
            bool blacklisted = e->getPluginService().isBlacklisted(
                a.value("pluginId").toString().toStdString());
            return McpToolResult::text(blacklisted ? "true" : "false");
        }});

    s.registerTool({"blacklist_plugin",
        "Blacklist a plugin.",
        objSchema({{"pluginId", QJsonObject{{"type","string"}}}}, {"pluginId"}),
        "settings",
        [e](const QJsonObject& a) -> McpToolResult {
            e->getPluginService().blacklistPlugin(
                a.value("pluginId").toString().toStdString());
            return McpToolResult::text("ok");
        }});

    s.registerTool({"unblacklist_plugin",
        "Remove a plugin from the blacklist.",
        objSchema({{"pluginId", QJsonObject{{"type","string"}}}}, {"pluginId"}),
        "settings",
        [e](const QJsonObject& a) -> McpToolResult {
            e->getPluginService().unblacklistPlugin(
                a.value("pluginId").toString().toStdString());
            return McpToolResult::text("ok");
        }});

    s.registerTool({"get_blacklist_reason",
        "Get the reason a plugin was blacklisted.",
        objSchema({{"pluginId", QJsonObject{{"type","string"}}}}, {"pluginId"}),
        "settings",
        [e](const QJsonObject& a) -> McpToolResult {
            auto reason = e->getPluginService().getBlacklistReason(
                a.value("pluginId").toString().toStdString());
            return McpToolResult::text(QString::fromStdString(reason));
        }});
}

// ── FX A/B Snapshots (2 tools) ──────────────────────────────────────────────

static void registerFxSnapshotTools(McpServer& s, AudioEngine* e)
{
    s.registerTool({"capture_fx_snapshot",
        "Capture current FX state as snapshot B for A/B comparison.",
        objSchema({{"trackIndex", QJsonObject{{"type","integer"}}},
                   {"slotIndex", QJsonObject{{"type","integer"}}}},
                  {"trackIndex","slotIndex"}),
        "settings",
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackIndex").toInt();
            int si = a.value("slotIndex").toInt();
            auto* proc = e->getMainProcessor();
            if (!proc) return McpToolResult::text("audio engine not initialized", true);
            auto* track = proc->getTrack(ti);
            if (!track) return McpToolResult::text("track not found", true);
            auto& chain = track->getFXChain();
            if (si < 0 || si >= static_cast<int>(chain.size()))
                return McpToolResult::text("slot not found", true);
            auto* slot = chain[si].get();
            if (!slot->isPlugin() || !slot->getPluginInstance())
                return McpToolResult::text("slot is not a plugin", true);
            juce::MemoryBlock state;
            slot->getPluginInstance()->getStateInformation(state);
            auto& model = e->getProjectModel();
            auto slotTree = model.getTrackListTree().getChild(ti)
                .getChildWithName(IDs::FX_CHAIN).getChild(si);
            if (slotTree.isValid())
                slotTree.setProperty(juce::Identifier("pluginStateB"),
                    state.toBase64Encoding(), &model.getUndoManager());
            return McpToolResult::text("ok");
        }});

    s.registerTool({"swap_fx_snapshot",
        "Swap between A and B FX snapshots.",
        objSchema({{"trackIndex", QJsonObject{{"type","integer"}}},
                   {"slotIndex", QJsonObject{{"type","integer"}}}},
                  {"trackIndex","slotIndex"}),
        "settings",
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackIndex").toInt();
            int si = a.value("slotIndex").toInt();
            auto* proc = e->getMainProcessor();
            if (!proc) return McpToolResult::text("audio engine not initialized", true);
            auto* track = proc->getTrack(ti);
            if (!track) return McpToolResult::text("track not found", true);
            auto& chain = track->getFXChain();
            if (si < 0 || si >= static_cast<int>(chain.size()))
                return McpToolResult::text("slot not found", true);
            auto* slot = chain[si].get();
            if (!slot->isPlugin() || !slot->getPluginInstance())
                return McpToolResult::text("slot is not a plugin", true);
            auto& model = e->getProjectModel();
            auto& um = model.getUndoManager();
            auto slotTree = model.getTrackListTree().getChild(ti)
                .getChildWithName(IDs::FX_CHAIN).getChild(si);
            if (!slotTree.isValid())
                return McpToolResult::text("slot tree not found", true);

            juce::MemoryBlock currentState;
            slot->getPluginInstance()->getStateInformation(currentState);

            juce::String b64 = slotTree.getProperty(
                juce::Identifier("pluginStateB")).toString();
            if (b64.isNotEmpty()) {
                juce::MemoryBlock bState;
                bState.fromBase64Encoding(b64);
                slot->getPluginInstance()->setStateInformation(
                    bState.getData(), static_cast<int>(bState.getSize()));
                slotTree.setProperty(juce::Identifier("pluginStateB"),
                    currentState.toBase64Encoding(), &um);
            } else {
                slotTree.setProperty(juce::Identifier("pluginStateB"),
                    currentState.toBase64Encoding(), &um);
                juce::String a64 = slotTree.getProperty(IDs::pluginState).toString();
                if (a64.isNotEmpty()) {
                    juce::MemoryBlock aState;
                    aState.fromBase64Encoding(a64);
                    slot->getPluginInstance()->setStateInformation(
                        aState.getData(), static_cast<int>(aState.getSize()));
                }
            }
            return McpToolResult::text("ok");
        }});
}

// ── Domain entry point ──────────────────────────────────────────────────────

void registerSettingsDomain(McpServer& s, AudioEngine* e)
{
    registerAudioDeviceTools(s, e);
    registerMidiDeviceTools(s, e);
    registerMetronomeTools(s, e);
    registerPunchTools(s, e);
    registerPreviewTools(s, e);
    registerPluginBlacklistTools(s, e);
    registerFxSnapshotTools(s, e);
}

} // namespace mcp
