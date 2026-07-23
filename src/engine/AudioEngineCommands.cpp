#include "AudioEngineCommands.h"
#include "AudioEngineCommands_Helpers.h"
#include "AudioEngine.h"
#include "MainAudioProcessor.h"
#include "../engine/ProjectSerializer.h"
#include "../model/ProjectModel.h"
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

juce::ValueTree AudioEngineCommands::createTrackValueTree(const std::string& name, int color, int parentBus)
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
