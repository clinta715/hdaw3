#include "AudioEngineCommands.h"
#include "AudioEngineCommands_Helpers.h"
#include "AudioEngine.h"
#include "MainAudioProcessor.h"
#include "MidiImport.h"
#include "../engine/ProjectSerializer.h"
#include "../model/ProjectModel.h"
#include "../common/BusFxDefs.h"
#include "../common/BusInfo.h"
#include <juce_core/juce_core.h>
#include <algorithm>

AudioEngineCommands::AudioEngineCommands(AudioEngine& engine)
    : engine_(engine) {}

AudioEngineCommands::~AudioEngineCommands() = default;

// ─── Helper methods ──────────────────────────────────────────────

juce::ValueTree AudioEngineCommands::findClipById(int clipId, int& outTrackIndex) const
{
    auto trackList = engine_.getProjectModel().getTrackListTree();
    for (int t = 0; t < trackList.getNumChildren(); ++t)
    {
        auto clipList = trackList.getChild(t).getChildWithName(IDs::CLIP_LIST);
        if (!clipList.isValid()) continue;
        for (int c = 0; c < clipList.getNumChildren(); ++c)
        {
            auto clip = clipList.getChild(c);
            if (static_cast<int>(clip.getProperty(IDs::clipID, 0)) == clipId)
            {
                outTrackIndex = t;
                return clip;
            }
        }
    }
    outTrackIndex = -1;
    return {};
}

juce::ValueTree AudioEngineCommands::findNoteById(int noteId, int& outClipId) const
{
    auto trackList = engine_.getProjectModel().getTrackListTree();
    for (int t = 0; t < trackList.getNumChildren(); ++t)
    {
        auto clipList = trackList.getChild(t).getChildWithName(IDs::CLIP_LIST);
        if (!clipList.isValid()) continue;
        for (int c = 0; c < clipList.getNumChildren(); ++c)
        {
            auto clip = clipList.getChild(c);
            auto noteList = clip.getChildWithName(IDs::MIDI_NOTE_LIST);
            if (!noteList.isValid()) continue;
            for (int n = 0; n < noteList.getNumChildren(); ++n)
            {
                auto note = noteList.getChild(n);
                if (static_cast<int>(note.getProperty(IDs::noteID, 0)) == noteId)
                {
                    outClipId = static_cast<int>(clip.getProperty(IDs::clipID, 0));
                    return note;
                }
            }
        }
    }

    outClipId = -1;
    return {};
}

juce::ValueTree AudioEngineCommands::findCcPointById(int ccId, int& outClipId) const
{
    auto trackList = engine_.getProjectModel().getTrackListTree();
    for (int t = 0; t < trackList.getNumChildren(); ++t)
    {
        auto clipList = trackList.getChild(t).getChildWithName(IDs::CLIP_LIST);
        if (!clipList.isValid()) continue;
        for (int c = 0; c < clipList.getNumChildren(); ++c)
        {
            auto clip = clipList.getChild(c);
            auto ccList = clip.getChildWithName(IDs::CC_LIST);
            if (!ccList.isValid()) continue;
            for (int n = 0; n < ccList.getNumChildren(); ++n)
            {
                auto pt = ccList.getChild(n);
                if (static_cast<int>(pt.getProperty(IDs::ccID, 0)) == ccId)
                {
                    outClipId = static_cast<int>(clip.getProperty(IDs::clipID, 0));
                    return pt;
                }
            }
        }
    }

    outClipId = -1;
    return {};
}

juce::ValueTree AudioEngineCommands::findFxSlot(int trackIndex, int slotIndex) const
{
    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren())
        return {};
    auto fxChain = trackList.getChild(trackIndex).getChildWithName(IDs::FX_CHAIN);
    if (!fxChain.isValid()) return {};
    if (slotIndex < 0 || slotIndex >= fxChain.getNumChildren())
        return {};
    return fxChain.getChild(slotIndex);
}

std::string AudioEngineCommands::resolvePluginName(const std::string& pluginId) const
{
    for (const auto& p : engine_.getPluginService().getPlugins())
    {
        if (p.fileOrIdentifier == pluginId)
            return p.name;
    }
    return {};
}

juce::ValueTree AudioEngineCommands::findAutomationLane(int trackIndex, const std::string& lane) const
{
    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren())
        return {};
    auto autoList = trackList.getChild(trackIndex).getChildWithName(IDs::AUTOMATION_LIST);
    if (!autoList.isValid()) return {};
    for (int i = 0; i < autoList.getNumChildren(); ++i)
    {
        auto autoLane = autoList.getChild(i);
        if (autoLane.getProperty(IDs::name, "").toString().toStdString() == lane)
            return autoLane;
    }
    return {};
}

juce::ValueTree AudioEngineCommands::createTrackValueTree(const std::string& name, int color, int parentBus, int trackType)
{
    juce::ValueTree track(IDs::TRACK);
    track.setProperty(IDs::name, juce::String(name), nullptr);
    track.setProperty(IDs::volume, 1.0, nullptr);
    track.setProperty(IDs::pan, 0.0, nullptr);
    track.setProperty(IDs::isMuted, false, nullptr);
    track.setProperty(IDs::isSoloed, false, nullptr);
    track.setProperty(IDs::isArm, false, nullptr);
    track.setProperty(IDs::inputMonitor, false, nullptr);
    track.setProperty(IDs::midiChannel, 1, nullptr);
    track.setProperty(IDs::trackHeight, 80.0, nullptr);
    track.setProperty(IDs::trackType, trackType, nullptr);
    if (parentBus >= 0)
        track.setProperty(IDs::parentBus, parentBus, nullptr);
    if (color >= 0)
        track.setProperty(IDs::color, color, nullptr);
    else
        track.setProperty(IDs::color, static_cast<int>(
            ProjectModel::trackColorForIndex(engine_.getProjectModel().getTrackListTree().getNumChildren())), nullptr);

    track.addChild(juce::ValueTree(IDs::CLIP_LIST), -1, nullptr);

    juce::ValueTree fxChain(IDs::FX_CHAIN);
    track.addChild(fxChain, -1, nullptr);

    juce::ValueTree autoList = ProjectModel::createTrackAutomationList();
    track.addChild(autoList, -1, nullptr);

    return track;
}

std::vector<ProjectModel::GainEnvelopePoint> AudioEngineCommands::getGainEnvelopePoints(int clipId)
{
    int trackIdx = -1;
    auto clip = findClipById(clipId, trackIdx);
    if (!clip.isValid()) return {};

    auto envelope = clip.getChildWithName(IDs::GAIN_ENVELOPE);
    return ProjectModel::getGainEnvelopePoints(envelope);
}

// ─── Send operations ──────────────────────────────────────────────

static juce::ValueTree findSendTree(const juce::ValueTree& trackTree, int sendIndex)
{
    auto sendList = trackTree.getChildWithName(IDs::SEND_LIST);
    if (!sendList.isValid()) return {};
    if (sendIndex < 0 || sendIndex >= sendList.getNumChildren()) return {};
    return sendList.getChild(sendIndex);
}

void AudioEngineCommands::setTrackSendLevel(int trackIndex, int sendIndex, float level)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren()) return;
    auto sendTree = findSendTree(trackList.getChild(trackIndex), sendIndex);
    if (!sendTree.isValid()) return;
    sendTree.setProperty(IDs::sendLevel, static_cast<double>(level), &um);
    if (auto* proc = engine_.getMainProcessor())
    {
        if (auto* rm = proc->getRoutingManager())
            rm->setSendLevel(trackIndex, sendIndex, level);
    }
}

void AudioEngineCommands::setTrackSendMode(int trackIndex, int sendIndex, bool isPreFader)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren()) return;
    auto sendTree = findSendTree(trackList.getChild(trackIndex), sendIndex);
    if (!sendTree.isValid()) return;
    sendTree.setProperty(IDs::sendMode, juce::String(isPreFader ? "pre" : "post"), &um);
    if (auto* proc = engine_.getMainProcessor())
    {
        if (auto* rm = proc->getRoutingManager())
            rm->setSendMode(trackIndex, sendIndex, isPreFader);
    }
}

void AudioEngineCommands::setTrackSendBypassed(int trackIndex, int sendIndex, bool bypassed)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren()) return;
    auto sendTree = findSendTree(trackList.getChild(trackIndex), sendIndex);
    if (!sendTree.isValid()) return;
    sendTree.setProperty(IDs::bypassed, bypassed, &um);
    if (auto* proc = engine_.getMainProcessor())
    {
        if (auto* rm = proc->getRoutingManager())
            rm->setSendBypassed(trackIndex, sendIndex, bypassed);
    }
}

// ─── Bus operations ───────────────────────────────────────────────
// Buses live at ROUTING_GRAPH/BUS_LIST/BUS; the busID property is the identity
// (tree position shifts when a bus is removed). The lookup itself is shared
// with the read surfaces (HDAW::findBusNode, common/BusInfo.h) so a command
// and a readback can never disagree about which node a busID names.

// FxBusProcessor::resetFxChain recognises exactly the types in
// HDAW::busFxTypes() (common/BusFxDefs.h); any other value builds a bus that
// enables no DSP at all — a silent passthrough. The shared list keeps
// createBus, setBusFxParam and the read surfaces naming the same set.

ProjectCommands::BusCreateResult AudioEngineCommands::createBus(const std::string& busType,
                                                               const std::string& name,
                                                               const std::string& fxType,
                                                               int busTarget)
{
    BusCreateResult result;

    if (busType != "fx" && busType != "group")
    {
        result.error = "createBus: busType must be \"fx\" or \"group\" (got \"" + busType + "\")";
        return result;
    }
    if (busType == "fx")
    {
        bool known = false;
        for (const char* type : HDAW::busFxTypes())
            known = known || (fxType == type);
        if (!known)
        {
            result.error = "createBus: fxType \"" + fxType + "\" is not supported (expected one of: "
                           + HDAW::busFxTypesText() + ")";
            return result;
        }
    }

    auto& model = engine_.getProjectModel();
    auto busList = model.getBusListTree();
    if (!busList.isValid())
    {
        result.error = "createBus: project has no ROUTING_GRAPH/BUS_LIST node";
        return result;
    }
    if (!HDAW::findBusNode(busList, busTarget).isValid())
    {
        result.error = "createBus: busTarget " + std::to_string(busTarget) + " does not exist";
        return result;
    }

    const int newBusID = model.allocateBusID();

    juce::ValueTree bus(IDs::BUS);
    bus.setProperty(IDs::name, juce::String(name), nullptr);
    bus.setProperty(IDs::busID, newBusID, nullptr);
    bus.setProperty(IDs::busType, juce::String(busType), nullptr);
    bus.setProperty(IDs::busTarget, busTarget, nullptr);
    if (busType == "fx")
        bus.setProperty(IDs::fxType, juce::String(fxType), nullptr);

    // One undo unit for this command: open the transaction so the bus is
    // distinguishable from whatever preceded it (createSend then appends to it).
    auto& um = model.getUndoManager();
    um.beginNewTransaction(juce::String(busType == "fx" ? "Create FX bus" : "Create group bus"));
    busList.addChild(bus, busList.getNumChildren(), &um);

    // ONE rebuild for the structural change (lesson 6), through the pump-park-safe
    // wrapper — the AudioProcessorGraph is never mutated from this thread.
    rebuildRoutingGraph();

    result.ok = true;
    result.busID = newBusID;
    return result;
}

bool AudioEngineCommands::removeBus(int busID, std::string& error)
{
    auto& model = engine_.getProjectModel();
    auto busList = model.getBusListTree();
    if (!busList.isValid())
    {
        error = "removeBus: project has no ROUTING_GRAPH/BUS_LIST node";
        return false;
    }

    int busIndex = -1;
    for (int i = 0; i < busList.getNumChildren(); ++i)
    {
        if (static_cast<int>(busList.getChild(i).getProperty(IDs::busID, -1)) == busID)
        {
            busIndex = i;
            break;
        }
    }
    if (busIndex < 0)
    {
        error = "removeBus: no bus with id " + std::to_string(busID);
        return false;
    }
    if (busID == 0 || busList.getChild(busIndex).getProperty(IDs::busType).toString() == "master")
    {
        error = "removeBus: the master bus cannot be removed";
        return false;
    }

    auto& um = model.getUndoManager();

    // One undo unit for this command, mirroring createBus: the cascade below
    // and the bus removal undo together.
    um.beginNewTransaction("Remove bus");

    // Cascade: a SEND whose sendTarget no longer resolves is a dead node
    // (RoutingManager::addSend bails out on an unknown target), so every send
    // anywhere in the tree that targets this bus goes with it — one undo unit.
    auto trackList = model.getTrackListTree();
    for (int t = 0; t < trackList.getNumChildren(); ++t)
    {
        auto sendList = trackList.getChild(t).getChildWithName(IDs::SEND_LIST);
        if (!sendList.isValid()) continue;
        for (int s = sendList.getNumChildren(); --s >= 0;)
        {
            if (static_cast<int>(sendList.getChild(s).getProperty(IDs::sendTarget, -1)) == busID)
                sendList.removeChild(s, &um);
        }
    }

    busList.removeChild(busIndex, &um);
    rebuildRoutingGraph();
    return true;
}

ProjectCommands::SendCreateResult AudioEngineCommands::createSend(int trackIndex, int busTarget,
                                                                  float level, bool isPreFader)
{
    SendCreateResult result;

    auto& model = engine_.getProjectModel();
    auto trackList = model.getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren())
    {
        result.error = "createSend: trackIndex " + std::to_string(trackIndex) + " is out of range";
        return result;
    }
    auto busList = model.getBusListTree();
    if (!HDAW::findBusNode(busList, busTarget).isValid())
    {
        result.error = "createSend: busTarget " + std::to_string(busTarget) + " does not exist";
        return result;
    }

    // Documented clamp: a send gain is linear and non-negative
    // (SendProcessor::processBlock applies it straight to the buffer).
    const float clampedLevel = juce::jmax(0.0f, level);

    auto track = trackList.getChild(trackIndex);
    auto& um = model.getUndoManager();

    auto sendList = track.getChildWithName(IDs::SEND_LIST);
    if (!sendList.isValid())
    {
        sendList = juce::ValueTree(IDs::SEND_LIST);
        track.addChild(sendList, -1, &um);
    }

    juce::ValueTree send(IDs::SEND);
    send.setProperty(IDs::sendTarget, busTarget, nullptr);
    send.setProperty(IDs::sendLevel, static_cast<double>(clampedLevel), nullptr);
    send.setProperty(IDs::sendMode, juce::String(isPreFader ? "pre" : "post"), nullptr);
    send.setProperty(IDs::bypassed, false, nullptr);

    const int sendIndex = sendList.getNumChildren();
    // No beginNewTransaction here: the send joins the caller's current undo unit
    // (createBus above), so "create the bus, then route to it" is ONE undo step.
    sendList.addChild(send, sendIndex, &um);

    rebuildRoutingGraph();

    result.ok = true;
    result.sendIndex = sendIndex;
    return result;
}

bool AudioEngineCommands::setBusFxParam(int busID, int paramIndex, float value,
                                       std::string& error)
{
    auto& model = engine_.getProjectModel();
    auto busList = model.getBusListTree();
    if (!busList.isValid())
    {
        error = "setBusFxParam: project has no ROUTING_GRAPH/BUS_LIST node";
        return false;
    }

    auto busTree = HDAW::findBusNode(busList, busID);
    if (!busTree.isValid())
    {
        error = "setBusFxParam: no bus with id " + std::to_string(busID);
        return false;
    }
    const juce::String busType = busTree.getProperty(IDs::busType).toString();
    if (busType != "fx")
    {
        error = "setBusFxParam: bus " + std::to_string(busID) + " is not an fx bus (busType \""
                + busType.toStdString() + "\")";
        return false;
    }

    // The def list IS the validation: an fxType with no defs has no params the
    // DSP honors, so writing one would be a fake param (G5).
    const juce::String fxType = busTree.getProperty(IDs::fxType).toString();
    const auto& defs = HDAW::busFxParamDefs(fxType);
    if (defs.empty())
    {
        error = "setBusFxParam: bus " + std::to_string(busID) + " has unsupported fxType \""
                + fxType.toStdString() + "\" (expected one of: " + HDAW::busFxTypesText() + ")";
        return false;
    }
    if (paramIndex < 0 || paramIndex >= static_cast<int>(defs.size()))
    {
        error = "setBusFxParam: paramIndex " + std::to_string(paramIndex)
                + " is out of range for fxType \"" + fxType.toStdString() + "\" (0.."
                + std::to_string(static_cast<int>(defs.size()) - 1) + ")";
        return false;
    }

    // Write-side clamp (lesson 23): the tree is re-read verbatim on every
    // rebuild/export, so an out-of-range value must never reach it.
    const float clamped = HDAW::clampBusFxParam(fxType, paramIndex, value);

    auto& um = model.getUndoManager();
    um.beginNewTransaction("Set bus FX param");
    busTree.setProperty("param_" + juce::String(paramIndex),
                        static_cast<double>(clamped), &um);

    // Live apply: the same clamped value lands on the running bus processor
    // (the tree write above is what a later rebuild restores from).
    if (auto* proc = engine_.getMainProcessor())
        if (auto* rm = proc->getRoutingManager())
            if (auto* fx = rm->getFxBus(busID))
                fx->setParam(paramIndex, clamped);

    return true;
}

bool AudioEngineCommands::removeSend(int trackIndex, int sendIndex, std::string& error)
{
    auto& model = engine_.getProjectModel();
    auto trackList = model.getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren())
    {
        error = "removeSend: trackIndex " + std::to_string(trackIndex) + " is out of range";
        return false;
    }
    auto track = trackList.getChild(trackIndex);
    auto sendList = track.getChildWithName(IDs::SEND_LIST);
    if (!findSendTree(track, sendIndex).isValid())
    {
        error = "removeSend: no send at index " + std::to_string(sendIndex)
                + " on track " + std::to_string(trackIndex);
        return false;
    }

    auto& um = model.getUndoManager();
    um.beginNewTransaction("Remove send");
    sendList.removeChild(sendIndex, &um);
    rebuildRoutingGraph();
    return true;
}

std::vector<int> AudioEngineCommands::importMidiFile(const std::string& filePath, int trackIndex)
{
    return HDAW::importMidiFile(engine_, QString::fromStdString(filePath), trackIndex);
}
