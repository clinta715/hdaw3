#include "Router_Audio.h"
#include "RouterHelpers.h"

#include "../../engine/AudioEngine.h"
#include "../../engine/Dx7SysexImport.h"
#include "../../engine/MixReport.h"
#include "../../common/ProjectCommands.h"
#include "../../common/SettingsKeys.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QSettings>
#include <QString>

#include <juce_audio_formats/juce_audio_formats.h>
#include <string>

using namespace frontend::router_helpers;

namespace frontend {

DispatchResult dispatchAudio(AudioEngine& engine, const QString& m, const QJsonValue& params) {
    auto& dm = engine.getDeviceManager();
    const auto o = paramsObject(params);

    if (m == "mixReport") {
        // Offline mix analysis of a rendered file — same contract as the MCP
        // mix_report tool (HDAW::MixReportAnalyzer, windows in SECONDS).
        // fromPlan derives the section windows from the song plan (beats ->
        // seconds via bpm; bpm 0 falls back to the plan bpm).
        std::string filePath;
        if (!requireString(o, "filePath", filePath, nullptr))
            return makeError(-32602, "filePath required");
        double bpm = o.value("bpm").toDouble(0.0);
        std::vector<HDAW::SectionWindow> windows;
        if (o.value("fromPlan").toBool(false)) {
            const auto plan = engine.getProjectCommands().getSongPlan();
            if (plan.sections.empty())
                return makeError(-32602, "no song plan set (fromPlan)");
            if (bpm <= 0.0) bpm = plan.bpm;
            const double spb = (bpm > 0.0) ? 60.0 / bpm : 0.5;
            for (const auto& s : plan.sections)
                windows.push_back(HDAW::SectionWindow{ s.name, s.startBeat * spb, s.endBeat * spb });
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
        if (bpm < 0.0)
            return makeError(-32602, "bpm must be >= 0");
        HDAW::MixReport rep;
        juce::String err;
        if (!HDAW::MixReportAnalyzer::analyze(juce::File(juce::String(filePath.c_str())),
                                              windows, bpm, rep, err))
            return makeError(-32603, QString::fromUtf8(err.toRawUTF8()));
        // Same JSON shape as the MCP mix_report tool.
        QJsonObject root{ { "duration", rep.duration }, { "sampleRate", rep.sampleRate },
                          { "peak", rep.peak }, { "rms", rep.rms },
                          { "bands", QJsonArray{ rep.bands[0], rep.bands[1],
                                                 rep.bands[2], rep.bands[3] } },
                          { "bandLabels", QJsonArray{ "sub", "bass", "body", "high" } },
                          { "kickProminence", rep.kickProminence } };
        if (rep.hasPumpDepth)
            root["pumpDepth"] = rep.pumpDepth;
        QJsonArray sections;
        for (const auto& s : rep.sections)
            sections.append(QJsonObject{ { "name", QString::fromUtf8(s.name.toRawUTF8()) },
                                         { "start", s.start }, { "end", s.end },
                                         { "rms", s.rms }, { "peak", s.peak },
                                         { "bandEnergy", QJsonArray{ s.bandEnergy[0], s.bandEnergy[1],
                                                                    s.bandEnergy[2], s.bandEnergy[3] } } });
        root["sections"] = sections;
        return { false, root };
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
        int ti, si;
        if (!requireInt(o, "trackIndex", ti, nullptr) || !requireInt(o, "slotIndex", si, nullptr))
            return makeError(-32602, "trackIndex and slotIndex required");

        std::string filePath;
        if (!requireString(o, "filePath", filePath, nullptr))
            return makeError(-32602, "filePath required");

        auto* proc = engine.getMainProcessor();
        if (!proc) return makeError(-32603, "audio engine not initialized");
        auto* track = proc->getTrack(ti);
        if (!track) return makeError(-32602, "track not found");
        auto& chain = track->getFXChain();
        if (si < 0 || si >= static_cast<int>(chain.size()))
            return makeError(-32602, "slot not found");
        auto* slot = chain[si].get();
        if (!slot || slot->getType() != "fm_synth")
            return makeError(-32602, "slot is not an FM synth");
        if (!slot->fmSynthEngine())
            return makeError(-32603, "FM synth engine not initialized");

        juce::File syxFile(filePath);
        if (!syxFile.existsAsFile())
            return makeError(-32602, "file not found: " + QString::fromStdString(filePath));

        juce::MemoryBlock raw;
        if (!syxFile.loadFileAsData(raw))
            return makeError(-32603, "failed to read file");

        auto* bytes = static_cast<const uint8_t*>(raw.getData());
        size_t fileSize = raw.getSize();

        std::optional<HDAW::Dx7Voice> voice;
        std::vector<HDAW::Dx7Voice> voices;
        int resolvedVoiceIndex = 0;

        if (fileSize >= 163 && bytes[0] == 0xF0 && bytes[1] == 0x43 && bytes[3] == 0x00) {
            voice = HDAW::parseSingleVoiceSysex(bytes, fileSize);
        } else if (fileSize >= 4104 && bytes[0] == 0xF0 && bytes[1] == 0x43 && bytes[3] == 0x09) {
            voices = HDAW::parseCartridgeSysex(bytes, fileSize);
            int vi = o.value("voiceIndex").toInt(0);
            if (vi >= 0 && vi < static_cast<int>(voices.size())) {
                voice = voices[vi];
                resolvedVoiceIndex = vi;
            }
        } else {
            return makeError(-32602, "not a recognized DX7 SysEx file");
        }

        if (!voice.has_value())
            return makeError(-32603, "failed to parse SysEx data");

        slot->fmSynthEngine()->loadPatch(voice->patchData.data());

        QJsonObject result;
        result["ok"] = true;
        result["voiceName"] = QString::fromStdString(voice->voiceName);
        result["algorithm"] = voice->algorithm;
        if (!voices.empty()) {
            result["totalVoices"] = static_cast<int>(voices.size());
            QJsonArray voicesArr;
            for (int i = 0; i < static_cast<int>(voices.size()); ++i) {
                QJsonObject v;
                v["index"] = i;
                v["name"] = QString::fromStdString(voices[i].voiceName);
                v["algorithm"] = voices[i].algorithm;
                voicesArr.append(v);
            }
            result["voices"] = voicesArr;
            result["voiceIndex"] = resolvedVoiceIndex;
        }

        return { false, result };
    }

    return makeError(-32601, "unknown audio method: " + m);
}

} // namespace frontend
