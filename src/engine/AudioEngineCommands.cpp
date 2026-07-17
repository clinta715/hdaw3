#include "AudioEngineCommands.h"
#include "AudioEngine.h"
#include "MainAudioProcessor.h"
#include "TransientDetector.h"
#include "RegionClipboard.h"
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

// ─── ProjectCommands — Track operations ───────────────────────────

int AudioEngineCommands::addTrack(const std::string& name, int color, int parentBus)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto trackList = engine_.getProjectModel().getTrackListTree();
    auto track = createTrackValueTree(name, color, parentBus);
    int idx = trackList.getNumChildren();
    trackList.addChild(track, idx, &um);
    return idx;
}

void AudioEngineCommands::removeTrack(int trackIndex)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (trackIndex >= 0 && trackIndex < trackList.getNumChildren())
        trackList.removeChild(trackIndex, &um);
}

void AudioEngineCommands::moveTrack(int trackIndex, int newIndex)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren()) return;
    if (newIndex < 0 || newIndex >= trackList.getNumChildren()) return;
    if (trackIndex == newIndex) return;
    auto track = trackList.getChild(trackIndex);
    trackList.removeChild(trackIndex, &um);
    if (newIndex > trackIndex) --newIndex;
    trackList.addChild(track, newIndex, &um);
}

void AudioEngineCommands::setTrackName(int trackIndex, const std::string& name)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (trackIndex >= 0 && trackIndex < trackList.getNumChildren())
        trackList.getChild(trackIndex).setProperty(IDs::name, juce::String(name), &um);
}

void AudioEngineCommands::setTrackColor(int trackIndex, int color)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (trackIndex >= 0 && trackIndex < trackList.getNumChildren())
        trackList.getChild(trackIndex).setProperty(IDs::color, color, &um);
}

void AudioEngineCommands::setTrackVolume(int trackIndex, float volume)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (trackIndex >= 0 && trackIndex < trackList.getNumChildren())
        trackList.getChild(trackIndex).setProperty(IDs::volume, static_cast<double>(volume), &um);
}

void AudioEngineCommands::setTrackPan(int trackIndex, float pan)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (trackIndex >= 0 && trackIndex < trackList.getNumChildren())
        trackList.getChild(trackIndex).setProperty(IDs::pan, static_cast<double>(pan), &um);
}

void AudioEngineCommands::setTrackMuted(int trackIndex, bool muted)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (trackIndex >= 0 && trackIndex < trackList.getNumChildren())
        trackList.getChild(trackIndex).setProperty(IDs::isMuted, muted, &um);
}

void AudioEngineCommands::setTrackSoloed(int trackIndex, bool soloed)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (trackIndex >= 0 && trackIndex < trackList.getNumChildren())
        trackList.getChild(trackIndex).setProperty(IDs::isSoloed, soloed, &um);
}

void AudioEngineCommands::setTrackArmed(int trackIndex, bool armed)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (trackIndex >= 0 && trackIndex < trackList.getNumChildren())
        trackList.getChild(trackIndex).setProperty(IDs::isArm, armed, &um);
}

void AudioEngineCommands::setTrackInputMonitor(int trackIndex, bool monitor)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (trackIndex >= 0 && trackIndex < trackList.getNumChildren())
        trackList.getChild(trackIndex).setProperty(IDs::inputMonitor, monitor, &um);
}

void AudioEngineCommands::setTrackHeight(int trackIndex, int height)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (trackIndex >= 0 && trackIndex < trackList.getNumChildren())
        trackList.getChild(trackIndex).setProperty(IDs::trackHeight, static_cast<double>(height), &um);
}

void AudioEngineCommands::setTrackMidiChannel(int trackIndex, int channel)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (trackIndex >= 0 && trackIndex < trackList.getNumChildren())
        trackList.getChild(trackIndex).setProperty(IDs::midiChannel, channel, &um);
}

int AudioEngineCommands::duplicateTrack(int trackIndex)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren()) return -1;
    auto source = trackList.getChild(trackIndex);
    auto copy = source.createCopy();
    int newIdx = trackList.getNumChildren();
    trackList.addChild(copy, newIdx, &um);
    return newIdx;
}

// ─── ProjectCommands — Clip operations ────────────────────────────

int AudioEngineCommands::addAudioClip(int trackIndex, double start, double duration,
                                      const std::string& sourceFile, const std::string& name)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren()) return -1;

    auto clip = ProjectModel::createAudioClip(
        juce::String(name), start, duration, juce::String(sourceFile));

    auto track = trackList.getChild(trackIndex);
    auto clipList = track.getChildWithName(IDs::CLIP_LIST);
    if (!clipList.isValid())
    {
        clipList = juce::ValueTree(IDs::CLIP_LIST);
        track.addChild(clipList, -1, &um);
    }
    int clipId = static_cast<int>(clip.getProperty(IDs::clipID, 0));
    clipList.addChild(clip, -1, &um);
    return clipId;
}

int AudioEngineCommands::addMidiClip(int trackIndex, double start, double duration,
                                     const std::string& name)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren()) return -1;

    auto clip = ProjectModel::createMidiClipEmpty(
        juce::String(name), start, duration);

    auto track = trackList.getChild(trackIndex);
    auto clipList = track.getChildWithName(IDs::CLIP_LIST);
    if (!clipList.isValid())
    {
        clipList = juce::ValueTree(IDs::CLIP_LIST);
        track.addChild(clipList, -1, &um);
    }
    int clipId = static_cast<int>(clip.getProperty(IDs::clipID, 0));
    clipList.addChild(clip, -1, &um);
    return clipId;
}

void AudioEngineCommands::removeClip(int clipId)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int trackIdx = -1;
    auto clip = findClipById(clipId, trackIdx);
    if (!clip.isValid()) return;
    clip.getParent().removeChild(clip, &um);
}

void AudioEngineCommands::moveClip(int clipId, int newTrackIndex, double newStart)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int oldTrackIdx = -1;
    auto clip = findClipById(clipId, oldTrackIdx);
    if (!clip.isValid()) return;
    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (newTrackIndex < 0 || newTrackIndex >= trackList.getNumChildren()) return;

    clip.setProperty(IDs::startTime, newStart, &um);

    if (newTrackIndex != oldTrackIdx)
    {
        auto oldParent = clip.getParent();
        auto newTrack = trackList.getChild(newTrackIndex);
        auto newClipList = newTrack.getChildWithName(IDs::CLIP_LIST);
        if (!newClipList.isValid())
        {
            newClipList = juce::ValueTree(IDs::CLIP_LIST);
            newTrack.addChild(newClipList, -1, &um);
        }
        oldParent.removeChild(clip, &um);
        newClipList.addChild(clip, -1, &um);
    }
}

void AudioEngineCommands::setClipStart(int clipId, double start)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int trackIdx = -1;
    auto clip = findClipById(clipId, trackIdx);
    if (clip.isValid())
        clip.setProperty(IDs::startTime, start, &um);
}

void AudioEngineCommands::setClipDuration(int clipId, double duration)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int trackIdx = -1;
    auto clip = findClipById(clipId, trackIdx);
    if (clip.isValid())
        clip.setProperty(IDs::duration, duration, &um);
}

void AudioEngineCommands::setClipGain(int clipId, float gain)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int trackIdx = -1;
    auto clip = findClipById(clipId, trackIdx);
    if (clip.isValid())
        clip.setProperty(IDs::gain, static_cast<double>(gain), &um);
}

void AudioEngineCommands::setClipFadeIn(int clipId, double fadeIn)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int trackIdx = -1;
    auto clip = findClipById(clipId, trackIdx);
    if (clip.isValid())
        clip.setProperty(IDs::fadeIn, fadeIn, &um);
}

void AudioEngineCommands::setClipFadeOut(int clipId, double fadeOut)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int trackIdx = -1;
    auto clip = findClipById(clipId, trackIdx);
    if (clip.isValid())
        clip.setProperty(IDs::fadeOut, fadeOut, &um);
}

void AudioEngineCommands::setClipOffset(int clipId, double offset)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int trackIdx = -1;
    auto clip = findClipById(clipId, trackIdx);
    if (clip.isValid())
        clip.setProperty(IDs::offset, offset, &um);
}

void AudioEngineCommands::setClipLooping(int clipId, bool looping)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int trackIdx = -1;
    auto clip = findClipById(clipId, trackIdx);
    if (clip.isValid())
        clip.setProperty(IDs::looping, looping, &um);
}

int AudioEngineCommands::duplicateClip(int clipId)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int trackIdx = -1;
    auto clip = findClipById(clipId, trackIdx);
    if (!clip.isValid() || trackIdx < 0) return -1;

    auto newClip = clip.createCopy();
    newClip.setProperty(IDs::clipID, ProjectModel::allocateClipID(), nullptr);
    double start = newClip.getProperty(IDs::startTime, 0.0);
    double duration = newClip.getProperty(IDs::duration, 0.0);
    newClip.setProperty(IDs::startTime, start + duration, nullptr);
    juce::String origName = newClip.getProperty(IDs::name).toString();
    if (!origName.endsWith(" copy"))
        newClip.setProperty(IDs::name, origName + " copy", nullptr);

    auto trackList = engine_.getProjectModel().getTrackListTree();
    auto clipList = trackList.getChild(trackIdx).getChildWithName(IDs::CLIP_LIST);
    if (!clipList.isValid()) return -1;
    clipList.addChild(newClip, -1, &um);
    return static_cast<int>(newClip.getProperty(IDs::clipID, 0));
}

// ─── ProjectCommands — MIDI note operations ───────────────────────

int AudioEngineCommands::addNote(int clipId, int pitch, int velocity,
                                 double startBeat, double durationBeats)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int trackIdx = -1;
    auto clip = findClipById(clipId, trackIdx);
    if (!clip.isValid()) return -1;

    auto noteList = clip.getChildWithName(IDs::MIDI_NOTE_LIST);
    if (!noteList.isValid())
    {
        noteList = juce::ValueTree(IDs::MIDI_NOTE_LIST);
        clip.addChild(noteList, -1, nullptr);
    }

    auto note = ProjectModel::createMidiNote(
        pitch, static_cast<float>(velocity) / 127.0f, startBeat, durationBeats);
    int noteId = static_cast<int>(note.getProperty(IDs::noteID, 0));
    noteList.addChild(note, -1, &um);
    return noteId;
}

void AudioEngineCommands::removeNote(int noteId)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int clipId = -1;
    auto note = findNoteById(noteId, clipId);
    if (note.isValid())
        note.getParent().removeChild(note, &um);
}

void AudioEngineCommands::setNotePitch(int noteId, int pitch)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int clipId = -1;
    auto note = findNoteById(noteId, clipId);
    if (note.isValid())
        note.setProperty(IDs::noteNumber, pitch, &um);
}

void AudioEngineCommands::setNoteVelocity(int noteId, int velocity)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int clipId = -1;
    auto note = findNoteById(noteId, clipId);
    if (note.isValid())
        note.setProperty(IDs::velocity, static_cast<float>(velocity) / 127.0f, &um);
}

void AudioEngineCommands::setNoteStart(int noteId, double startBeat)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int clipId = -1;
    auto note = findNoteById(noteId, clipId);
    if (note.isValid())
        note.setProperty(IDs::startBeat, startBeat, &um);
}

void AudioEngineCommands::setNoteDuration(int noteId, double durationBeats)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int clipId = -1;
    auto note = findNoteById(noteId, clipId);
    if (note.isValid())
        note.setProperty(IDs::durationBeats, durationBeats, &um);
}

void AudioEngineCommands::clearNotes(int clipId)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int trackIdx = -1;
    auto clip = findClipById(clipId, trackIdx);
    if (!clip.isValid()) return;

    auto noteList = clip.getChildWithName(IDs::MIDI_NOTE_LIST);
    if (noteList.isValid())
        noteList.removeAllChildren(&um);
}

void AudioEngineCommands::addCcPoint(int clipId, int controllerNumber, double beat, int value)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int trackIdx = -1;
    auto clip = findClipById(clipId, trackIdx);
    if (!clip.isValid()) return;

    auto ccList = clip.getChildWithName(IDs::CC_LIST);
    if (!ccList.isValid())
    {
        ccList = juce::ValueTree(IDs::CC_LIST);
        clip.addChild(ccList, -1, nullptr);
    }

    juce::ValueTree pt(IDs::CC_POINT);
    pt.setProperty(IDs::controllerNumber, controllerNumber, &um);
    pt.setProperty(IDs::beat, beat, &um);
    pt.setProperty(IDs::value, value, &um);
    ccList.addChild(pt, -1, &um);
}

// ─── ProjectCommands — FX operations ──────────────────────────────

void AudioEngineCommands::addFxSlot(int trackIndex, int type, int position,
                                    const std::string& pluginId)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren()) return;

    auto track = trackList.getChild(trackIndex);
    auto fxChain = track.getChildWithName(IDs::FX_CHAIN);
    if (!fxChain.isValid())
    {
        fxChain = juce::ValueTree(IDs::FX_CHAIN);
        track.addChild(fxChain, -1, &um);
    }

    // Map integer type to string
    std::string typeStr;
    switch (type)
    {
        case 0: typeStr = "eq"; break;
        case 1: typeStr = "compressor"; break;
        case 2: typeStr = "reverb"; break;
        case 3: typeStr = "delay"; break;
        default: typeStr = "plugin"; break;
    }

    juce::ValueTree slot(IDs::FX_SLOT);
    slot.setProperty(IDs::fxType, juce::String(typeStr), &um);
    if (typeStr == "plugin" && !pluginId.empty())
    {
        slot.setProperty(IDs::pluginID, juce::String(pluginId), &um);
        slot.setProperty(IDs::pluginFormat,
            juce::String(engine_.getProjectModel().resolvePluginFormat(pluginId)), &um);
    }
    slot.setProperty(IDs::bypassed, false, &um);

    int n = fxChain.getNumChildren();
    int insertIdx = (position < 0 || position > n) ? n : position;
    fxChain.addChild(slot, insertIdx, &um);
}

void AudioEngineCommands::addFxSlot(int trackIndex, const std::string& type,
                                    int position, const std::string& pluginId)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren()) return;

    auto track = trackList.getChild(trackIndex);
    auto fxChain = track.getChildWithName(IDs::FX_CHAIN);
    if (!fxChain.isValid())
    {
        fxChain = juce::ValueTree(IDs::FX_CHAIN);
        track.addChild(fxChain, -1, &um);
    }

    juce::ValueTree slot(IDs::FX_SLOT);
    slot.setProperty(IDs::fxType, juce::String(type), &um);
    if (type == "plugin" && !pluginId.empty())
    {
        slot.setProperty(IDs::pluginID, juce::String(pluginId), &um);
        slot.setProperty(IDs::pluginFormat,
            juce::String(engine_.getProjectModel().resolvePluginFormat(pluginId)), &um);
    }
    slot.setProperty(IDs::bypassed, false, &um);

    int n = fxChain.getNumChildren();
    int insertIdx = (position < 0 || position > n) ? n : position;
    fxChain.addChild(slot, insertIdx, &um);
}

void AudioEngineCommands::removeFxSlot(int trackIndex, int slotIndex)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto slot = findFxSlot(trackIndex, slotIndex);
    if (slot.isValid())
        slot.getParent().removeChild(slot, &um);
}

void AudioEngineCommands::setFxSlotBypassed(int trackIndex, int slotIndex, bool bypassed)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto slot = findFxSlot(trackIndex, slotIndex);
    if (slot.isValid())
        slot.setProperty(IDs::bypassed, bypassed, &um);
}

void AudioEngineCommands::setFxSlotParam(int trackIndex, int slotIndex, int paramIndex,
                                         float value)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto slot = findFxSlot(trackIndex, slotIndex);
    if (!slot.isValid()) return;

    juce::String propName = "param_" + juce::String(paramIndex);
    slot.setProperty(juce::Identifier(propName), static_cast<double>(value), &um);
}

void AudioEngineCommands::reorderFxSlots(int trackIndex, int fromSlot, int toSlot)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren()) return;
    auto fxChain = trackList.getChild(trackIndex).getChildWithName(IDs::FX_CHAIN);
    if (!fxChain.isValid()) return;
    int n = fxChain.getNumChildren();
    if (fromSlot < 0 || fromSlot >= n || toSlot < 0 || toSlot >= n) return;
    if (fromSlot == toSlot) return;
    auto slot = fxChain.getChild(fromSlot);
    fxChain.removeChild(fromSlot, &um);
    if (toSlot > fromSlot) --toSlot;
    fxChain.addChild(slot, toSlot, &um);
}

void AudioEngineCommands::setFxSlotPlugin(int trackIndex, int slotIndex,
    const std::string& fxType, const std::string& pluginID,
    const std::string& pluginFormat, const std::string& pluginPath)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto slot = findFxSlot(trackIndex, slotIndex);
    if (!slot.isValid()) return;

    slot.setProperty(IDs::fxType, juce::String(fxType), &um);
    slot.setProperty(IDs::pluginID, juce::String(pluginID), &um);
    slot.setProperty(IDs::pluginFormat, juce::String(pluginFormat), &um);
    slot.setProperty(IDs::pluginPath, juce::String(pluginPath), &um);
}

// ─── ProjectCommands — Automation ─────────────────────────────────

void AudioEngineCommands::addAutomationLane(int trackIndex, const std::string& laneName)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren()) return;

    auto track = trackList.getChild(trackIndex);
    auto autoList = track.getChildWithName(IDs::AUTOMATION_LIST);
    if (!autoList.isValid())
    {
        autoList = juce::ValueTree(IDs::AUTOMATION_LIST);
        track.addChild(autoList, -1, &um);
    }

    // Don't add duplicate lanes
    for (int i = 0; i < autoList.getNumChildren(); ++i)
    {
        if (autoList.getChild(i).getProperty(IDs::name, "").toString().toStdString() == laneName)
            return;
    }

    juce::ValueTree lane(IDs::AUTOMATION);
    lane.setProperty(IDs::name, juce::String(laneName), &um);
    lane.setProperty(IDs::automationEnabled, true, &um);
    lane.addChild(juce::ValueTree(IDs::POINT_LIST), -1, nullptr);
    autoList.addChild(lane, -1, &um);
    if (auto* proc = engine_.getMainProcessor())
        proc->rebuildAutomationCache(trackIndex);
}

void AudioEngineCommands::removeAutomationLane(int trackIndex, const std::string& laneName)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto autoLane = findAutomationLane(trackIndex, laneName);
    if (autoLane.isValid())
        autoLane.getParent().removeChild(autoLane, &um);
    if (auto* proc = engine_.getMainProcessor())
        proc->rebuildAutomationCache(trackIndex);
}

void AudioEngineCommands::addAutomationPoint(int trackIndex, const std::string& lane,
                                             double time, float value)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto autoLane = findAutomationLane(trackIndex, lane);
    if (!autoLane.isValid()) return;

    auto pointList = autoLane.getChildWithName(IDs::POINT_LIST);
    if (!pointList.isValid())
    {
        pointList = juce::ValueTree(IDs::POINT_LIST);
        autoLane.addChild(pointList, -1, nullptr);
    }

    juce::ValueTree point(IDs::POINT);
    point.setProperty(IDs::startTime, time, nullptr);
    point.setProperty(IDs::gain, static_cast<double>(value), nullptr);
    pointList.addChild(point, -1, &um);
    if (auto* proc = engine_.getMainProcessor())
        proc->rebuildAutomationCache(trackIndex);
}

void AudioEngineCommands::removeAutomationPoint(int trackIndex, const std::string& lane,
                                                double time)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto autoLane = findAutomationLane(trackIndex, lane);
    if (!autoLane.isValid()) return;

    auto pointList = autoLane.getChildWithName(IDs::POINT_LIST);
    if (!pointList.isValid()) return;

    for (int i = 0; i < pointList.getNumChildren(); ++i)
    {
        auto pt = pointList.getChild(i);
        if (static_cast<double>(pt.getProperty(IDs::startTime, 0.0)) == time)
        {
            pointList.removeChild(i, &um);
            if (auto* proc = engine_.getMainProcessor())
                proc->rebuildAutomationCache(trackIndex);
            return;
        }
    }
}

void AudioEngineCommands::setAutomationEnabled(int trackIndex, const std::string& lane,
                                               bool enabled)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto autoLane = findAutomationLane(trackIndex, lane);
    if (autoLane.isValid())
    {
        autoLane.setProperty(IDs::automationEnabled, enabled, &um);
        if (auto* proc = engine_.getMainProcessor())
            proc->rebuildAutomationCache(trackIndex);
    }
}

void AudioEngineCommands::setAutomationPointValue(int trackIndex, const std::string& lane,
                                                   double time, float value)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto autoLane = findAutomationLane(trackIndex, lane);
    if (!autoLane.isValid()) return;

    auto pointList = autoLane.getChildWithName(IDs::POINT_LIST);
    if (!pointList.isValid()) return;

    for (int i = 0; i < pointList.getNumChildren(); ++i)
    {
        auto pt = pointList.getChild(i);
        if (static_cast<double>(pt.getProperty(IDs::startTime, 0.0)) == time)
        {
            pt.setProperty(IDs::startTime, time, &um);
            pt.setProperty(IDs::gain, static_cast<double>(value), &um);
            if (auto* proc = engine_.getMainProcessor())
                proc->rebuildAutomationCache(trackIndex);
            return;
        }
    }
}

// ─── ProjectCommands — MIDI CC ──────────────────────────────────────────

int AudioEngineCommands::addModulation(int trackIndex, const juce::ValueTree& modulationTree)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren()) return -1;

    auto track = trackList.getChild(trackIndex);
    auto modList = track.getChildWithName(IDs::MODULATION_LIST);
    if (!modList.isValid())
    {
        modList = juce::ValueTree(IDs::MODULATION_LIST);
        track.addChild(modList, -1, &um);
    }

    int lfoIndex = modList.getNumChildren();
    auto newMod = modulationTree.createCopy();
    modList.addChild(newMod, -1, &um);
    return lfoIndex;
}

void AudioEngineCommands::removeModulation(int trackIndex, int lfoIndex)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren()) return;

    auto track = trackList.getChild(trackIndex);
    auto modList = track.getChildWithName(IDs::MODULATION_LIST);
    if (!modList.isValid() || lfoIndex < 0 || lfoIndex >= modList.getNumChildren()) return;

    modList.removeChild(modList.getChild(lfoIndex), &um);
    if (auto* proc = engine_.getMainProcessor())
        proc->rebuildModulation(trackIndex);
}

void AudioEngineCommands::setModulationProperty(int trackIndex, int lfoIndex,
    const std::string& propertyID, float value)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren()) return;

    auto track = trackList.getChild(trackIndex);
    auto modList = track.getChildWithName(IDs::MODULATION_LIST);
    if (!modList.isValid() || lfoIndex < 0 || lfoIndex >= modList.getNumChildren()) return;

    auto modTree = modList.getChild(lfoIndex);
    auto propName = juce::Identifier(juce::String(propertyID));
    modTree.setProperty(propName, static_cast<double>(value), &um);

    if (auto* proc = engine_.getMainProcessor())
        proc->rebuildModulation(trackIndex);
}

// ─── ProjectCommands — Transport properties ───────────────────────

void AudioEngineCommands::setTempo(double bpm)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    engine_.getProjectModel().getTree().setProperty(IDs::tempo, bpm, &um);
}

void AudioEngineCommands::setLoopStart(double beat)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto transport = engine_.getProjectModel().getTransportTree();
    if (transport.isValid())
        transport.setProperty(IDs::loopStart, beat, &um);
}

void AudioEngineCommands::setLoopEnd(double beat)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto transport = engine_.getProjectModel().getTransportTree();
    if (transport.isValid())
        transport.setProperty(IDs::loopEnd, beat, &um);
}

void AudioEngineCommands::setLooping(bool looping)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto transport = engine_.getProjectModel().getTransportTree();
    if (transport.isValid())
        transport.setProperty(IDs::isLooping, looping, &um);
}

void AudioEngineCommands::setMetronomeEnabled(bool enabled)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto transport = engine_.getProjectModel().getTransportTree();
    if (transport.isValid())
        transport.setProperty(IDs::metronomeEnabled, enabled, &um);
}

void AudioEngineCommands::setTimeSignature(int numerator, int denominator)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto transport = engine_.getProjectModel().getTransportTree();
    if (transport.isValid())
    {
        transport.setProperty(IDs::timeSigNumerator, numerator, &um);
        transport.setProperty(IDs::timeSigDenominator, denominator, &um);
    }
}

// ─── ProjectCommands — Markers ────────────────────────────────────

int AudioEngineCommands::addMarker(const std::string& name, double time, int color)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto projectTree = engine_.getProjectModel().getTree();
    auto markerList = projectTree.getChildWithName(IDs::MARKER_LIST);
    if (!markerList.isValid())
    {
        markerList = juce::ValueTree(IDs::MARKER_LIST);
        projectTree.addChild(markerList, -1, &um);
    }
    juce::ValueTree marker(IDs::MARKER);
    marker.setProperty(IDs::markerName, juce::String(name), &um);
    marker.setProperty(IDs::markerTime, time, &um);
    marker.setProperty(IDs::markerColor, color, &um);
    int idx = markerList.getNumChildren();
    markerList.addChild(marker, -1, &um);
    return idx;
}

void AudioEngineCommands::removeMarker(int index)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto projectTree = engine_.getProjectModel().getTree();
    auto markerList = projectTree.getChildWithName(IDs::MARKER_LIST);
    if (!markerList.isValid()) return;
    if (index >= 0 && index < markerList.getNumChildren())
        markerList.removeChild(index, &um);
}

void AudioEngineCommands::setMarkerName(int index, const std::string& name)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto projectTree = engine_.getProjectModel().getTree();
    auto markerList = projectTree.getChildWithName(IDs::MARKER_LIST);
    if (!markerList.isValid()) return;
    if (index >= 0 && index < markerList.getNumChildren())
        markerList.getChild(index).setProperty(IDs::markerName, juce::String(name), &um);
}

void AudioEngineCommands::setMarkerTime(int index, double time)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto projectTree = engine_.getProjectModel().getTree();
    auto markerList = projectTree.getChildWithName(IDs::MARKER_LIST);
    if (!markerList.isValid()) return;
    if (index >= 0 && index < markerList.getNumChildren())
        markerList.getChild(index).setProperty(IDs::markerTime, time, &um);
}

// ─── ProjectCommands — Undo/redo ──────────────────────────────────

void AudioEngineCommands::undo()  { engine_.getProjectModel().getUndoManager().undo(); }
void AudioEngineCommands::redo()  { engine_.getProjectModel().getUndoManager().redo(); }
bool AudioEngineCommands::canUndo() const { return engine_.getProjectModel().getUndoManager().canUndo(); }
bool AudioEngineCommands::canRedo() const { return engine_.getProjectModel().getUndoManager().canRedo(); }

void AudioEngineCommands::beginTransaction(const std::string& name)
{
    engine_.getProjectModel().getUndoManager().beginNewTransaction(juce::String(name));
}

void AudioEngineCommands::endTransaction()
{
    // UndoManager's beginNewTransaction implicitly ends the previous one.
    // Calling beginNewTransaction with an empty name is the standard way
    // to close the current transaction without starting a new named one.
    engine_.getProjectModel().getUndoManager().beginNewTransaction({});
}

// ─── ProjectCommands — Project lifecycle ──────────────────────────

void AudioEngineCommands::newProject()
{
    HDAW::ProjectSerializer::createNew(engine_.getProjectModel());
}

bool AudioEngineCommands::saveProject(const std::string& filePath)
{
    return HDAW::ProjectSerializer::save(engine_.getProjectModel(), juce::File(filePath));
}

bool AudioEngineCommands::loadProject(const std::string& filePath)
{
    return HDAW::ProjectSerializer::load(engine_.getProjectModel(), juce::File(filePath));
}

// ─── ProjectCommands — Scale ──────────────────────────────────────

void AudioEngineCommands::setScaleRoot(int root) { engine_.getProjectModel().setScaleRoot(root); }
void AudioEngineCommands::setScaleMode(int mode) { engine_.getProjectModel().setScaleMode(mode); }

// ─── TransportCommands ────────────────────────────────────────────

void AudioEngineCommands::play()
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto transport = engine_.getProjectModel().getTransportTree();
    if (transport.isValid())
        transport.setProperty(IDs::isPlaying, true, &um);
}

void AudioEngineCommands::stop()
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto transport = engine_.getProjectModel().getTransportTree();
    if (transport.isValid())
    {
        transport.setProperty(IDs::isPlaying, false, &um);
        transport.setProperty(IDs::position, 0.0, &um);
    }
}

void AudioEngineCommands::pause()
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto transport = engine_.getProjectModel().getTransportTree();
    if (transport.isValid())
        transport.setProperty(IDs::isPlaying, false, &um);
}

void AudioEngineCommands::rewind()
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto transport = engine_.getProjectModel().getTransportTree();
    if (transport.isValid())
        transport.setProperty(IDs::position, 0.0, &um);
}

void AudioEngineCommands::toggleLoop()
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto transport = engine_.getProjectModel().getTransportTree();
    if (transport.isValid())
    {
        bool current = transport.getProperty(IDs::isLooping, false);
        transport.setProperty(IDs::isLooping, !current, &um);
    }
}

void AudioEngineCommands::seekToSample(int64_t sample)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto transport = engine_.getProjectModel().getTransportTree();
    if (transport.isValid())
    {
        double sr = engine_.getTransportManager().getSampleRate();
        if (sr <= 0.0) return;
        transport.setProperty(IDs::position, static_cast<double>(sample) / sr, &um);
    }
}

void AudioEngineCommands::seekToSeconds(double seconds)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto transport = engine_.getProjectModel().getTransportTree();
    if (transport.isValid())
        transport.setProperty(IDs::position, seconds, &um);
}

void AudioEngineCommands::startRecording()
{
    if (auto* proc = engine_.getMainProcessor())
        proc->startRecording();
}

void AudioEngineCommands::stopRecording()
{
    if (auto* proc = engine_.getMainProcessor())
        proc->stopRecording();
}

bool AudioEngineCommands::isRecording() const
{
    if (auto* proc = engine_.getMainProcessor())
        return proc->isRecording();
    return false;
}

// ─── AudioGraphCommands ───────────────────────────────────────────

void AudioEngineCommands::rebuildRoutingGraph()
{
    if (auto* proc = engine_.getMainProcessor())
        proc->rebuildRoutingGraph();
}

void AudioEngineCommands::rebuildTrackFX(int trackIndex)
{
    if (auto* proc = engine_.getMainProcessor())
        proc->rebuildTrackFX(trackIndex);
}

void AudioEngineCommands::rebuildAutomationCache(int trackIndex)
{
    if (auto* proc = engine_.getMainProcessor())
        proc->rebuildAutomationCache(trackIndex);
}

void AudioEngineCommands::rebuildModulation(int trackIndex)
{
    if (auto* proc = engine_.getMainProcessor())
        proc->rebuildModulation(trackIndex);
}

void AudioEngineCommands::toggleFXEditor(int trackIndex, int slotIndex)
{
    if (auto* proc = engine_.getMainProcessor())
        proc->toggleFXEditor(trackIndex, slotIndex);
}

void AudioEngineCommands::switchClipTake(int clipId)
{
    int trackIdx = -1;
    auto clip = findClipById(clipId, trackIdx);
    if (!clip.isValid() || trackIdx < 0) return;

    auto trackList = engine_.getProjectModel().getTrackListTree();
    auto clipList = trackList.getChild(trackIdx).getChildWithName(IDs::CLIP_LIST);
    if (!clipList.isValid()) return;

    int clipIdx = -1;
    for (int c = 0; c < clipList.getNumChildren(); ++c)
    {
        if (static_cast<int>(clipList.getChild(c).getProperty(IDs::clipID, 0)) == clipId)
        {
            clipIdx = c;
            break;
        }
    }
    if (clipIdx < 0) return;

    juce::String sourceFile = clip.getProperty(IDs::sourceFile, "").toString();

    if (auto* proc = engine_.getMainProcessor())
    {
        if (auto* rm = proc->getRoutingManager())
            rm->switchClipTake(trackIdx, clipIdx, sourceFile);
    }
}

void AudioEngineCommands::setClipSourceBpm(int clipId, double bpm)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int trackIdx = -1;
    auto clip = findClipById(clipId, trackIdx);
    if (clip.isValid())
        clip.setProperty(IDs::sourceBpm, juce::jmax(0.0, bpm), &um);
}

void AudioEngineCommands::setClipStretchMode(int clipId, int mode)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int trackIdx = -1;
    auto clip = findClipById(clipId, trackIdx);
    if (!clip.isValid()) return;

    int clamped = juce::jlimit(0, 2, mode);
    clip.setProperty(IDs::stretchMode, clamped, &um);

    // When switching to TempoMatch, derive the ratio immediately from the
    // clip's sourceBpm and the project tempo so the UI/route reflect it.
    if (clamped == 1)
    {
        double sourceBpm = clip.getProperty(IDs::sourceBpm, 0.0);
        if (sourceBpm > 0.0)
        {
            double projectBpm = engine_.getProjectModel().getTree().getProperty(IDs::tempo, 120.0);
            double ratio = sourceBpm / projectBpm;
            double sourceDur = clip.getProperty(IDs::sourceDuration, 0.0);
            clip.setProperty(IDs::stretchRatio, ratio, &um);
            if (sourceDur > 0.0)
                clip.setProperty(IDs::duration, sourceDur * ratio, &um);
        }
    }
    else if (clamped == 0)
    {
        // Off: restore duration to the original source length.
        double sourceDur = clip.getProperty(IDs::sourceDuration, 0.0);
        clip.setProperty(IDs::stretchRatio, 1.0, &um);
        if (sourceDur > 0.0)
            clip.setProperty(IDs::duration, sourceDur, &um);
    }
}

void AudioEngineCommands::setClipStretchRatio(int clipId, double ratio)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int trackIdx = -1;
    auto clip = findClipById(clipId, trackIdx);
    if (!clip.isValid()) return;

    double clamped = juce::jlimit(0.25, 4.0, ratio);
    clip.setProperty(IDs::stretchRatio, clamped, &um);

    // Keep the timeline-visible duration consistent with the new ratio.
    double sourceDur = clip.getProperty(IDs::sourceDuration, 0.0);
    if (sourceDur > 0.0)
        clip.setProperty(IDs::duration, sourceDur * clamped, &um);
}

void AudioEngineCommands::tempoMatchClip(int clipId)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int trackIdx = -1;
    auto clip = findClipById(clipId, trackIdx);
    if (!clip.isValid()) return;

    double sourceBpm = clip.getProperty(IDs::sourceBpm, 0.0);
    if (sourceBpm <= 0.0)
        return; // can't tempo-match without a known source tempo

    double projectBpm = engine_.getProjectModel().getTree().getProperty(IDs::tempo, 120.0);
    if (projectBpm <= 0.0) return;

    double ratio = sourceBpm / projectBpm;
    double sourceDur = clip.getProperty(IDs::sourceDuration, 0.0);
    clip.setProperty(IDs::stretchMode, 1, &um);
    clip.setProperty(IDs::stretchRatio, ratio, &um);
    if (sourceDur > 0.0)
        clip.setProperty(IDs::duration, sourceDur * ratio, &um);
}

void AudioEngineCommands::fitClipToLoop(int clipId)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int trackIdx = -1;
    auto clip = findClipById(clipId, trackIdx);
    if (!clip.isValid()) return;

    auto transport = engine_.getProjectModel().getTransportTree();
    double loopStart = transport.getProperty(IDs::loopStart, 0.0);
    double loopEnd = transport.getProperty(IDs::loopEnd, 0.0);
    double loopLen = loopEnd - loopStart;
    if (loopLen <= 0.0)
        return; // no valid loop region

    double sourceDur = clip.getProperty(IDs::sourceDuration, 0.0);
    if (sourceDur <= 0.0)
        return;

    double ratio = loopLen / sourceDur;
    clip.setProperty(IDs::stretchMode, 2, &um); // ManualRatio
    clip.setProperty(IDs::stretchRatio, ratio, &um);
    clip.setProperty(IDs::duration, loopLen, &um);
    clip.setProperty(IDs::offset, 0.0, &um);
}

// ─── ProjectCommands — Gain Envelope ────────────────────────────────

void AudioEngineCommands::addGainEnvelopePoint(int clipId, double time, double gain)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int trackIdx = -1;
    auto clip = findClipById(clipId, trackIdx);
    if (!clip.isValid()) return;

    auto envelope = ProjectModel::ensureGainEnvelope(clip, &um);
    ProjectModel::addGainEnvelopePoint(envelope, time, gain, &um);
    notifyClipGainEnvelopeChanged(clipId);
}

void AudioEngineCommands::moveGainEnvelopePoint(int clipId, int pointIndex, double time, double gain)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int trackIdx = -1;
    auto clip = findClipById(clipId, trackIdx);
    if (!clip.isValid()) return;

    auto envelope = clip.getChildWithName(IDs::GAIN_ENVELOPE);
    if (!envelope.isValid() || pointIndex < 0 || pointIndex >= envelope.getNumChildren()) return;

    // Re-insert via the sorted inserter rather than mutating the point in
    // place. addGainEnvelopePoint inserts at the time-correct position, so a
    // drag that crosses a neighbour reorders the children correctly; an
    // in-place setProperty leaves the ValueTree out of order, and
    // ClipSourceProcessor::getGainAtTime (binary search) then misses the
    // bracket and returns wrong gains.
    ProjectModel::removeGainEnvelopePoint(envelope, pointIndex, &um);
    ProjectModel::addGainEnvelopePoint(envelope, time, gain, &um);
    notifyClipGainEnvelopeChanged(clipId);
}

void AudioEngineCommands::removeGainEnvelopePoint(int clipId, int pointIndex)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int trackIdx = -1;
    auto clip = findClipById(clipId, trackIdx);
    if (!clip.isValid()) return;

    auto envelope = clip.getChildWithName(IDs::GAIN_ENVELOPE);
    ProjectModel::removeGainEnvelopePoint(envelope, pointIndex, &um);
    notifyClipGainEnvelopeChanged(clipId);
}

void AudioEngineCommands::clearGainEnvelope(int clipId)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int trackIdx = -1;
    auto clip = findClipById(clipId, trackIdx);
    if (!clip.isValid()) return;

    auto envelope = clip.getChildWithName(IDs::GAIN_ENVELOPE);
    if (envelope.isValid())
        clip.removeChild(envelope, &um);
    notifyClipGainEnvelopeChanged(clipId);
}

std::vector<ProjectModel::GainEnvelopePoint> AudioEngineCommands::getGainEnvelopePoints(int clipId)
{
    int trackIdx = -1;
    auto clip = findClipById(clipId, trackIdx);
    if (!clip.isValid()) return {};

    auto envelope = clip.getChildWithName(IDs::GAIN_ENVELOPE);
    return ProjectModel::getGainEnvelopePoints(envelope);
}

void AudioEngineCommands::sliceClipAtTimes(int clipId, const std::vector<double>& times)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int trackIdx = -1;
    auto clip = findClipById(clipId, trackIdx);
    if (!clip.isValid()) return;

    auto slices = ProjectModel::sliceClipAtTimes(clip, times, &um);
    
    // Rebuild routing for new clips
    if (auto* proc = engine_.getMainProcessor())
        proc->rebuildRoutingGraph();
}

void AudioEngineCommands::sliceClipAtTransients(int clipId)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int trackIdx = -1;
    auto clip = findClipById(clipId, trackIdx);
    if (!clip.isValid()) return;

    juce::String sourceFile = clip.getProperty(IDs::sourceFile);
    if (sourceFile.isEmpty()) return;

    // Load the source file into a buffer for synchronous detection
    auto* pool = &engine_.getProjectPool();
    auto* fm = &pool->getFormatManager();
    
    auto reader = std::unique_ptr<juce::AudioFormatReader>(fm->createReaderFor(juce::File(sourceFile)));
    if (!reader) return;
    
    juce::AudioBuffer<float> buffer(reader->numChannels, static_cast<int>(reader->lengthInSamples));
    reader->read(&buffer, 0, static_cast<int>(reader->lengthInSamples), 0, true, true);

    // Run transient detection synchronously
    HDAW::TransientDetector detector;
    auto result = detector.detect(buffer, reader->sampleRate);

    if (result.transientTimes.empty()) return;

    // TransientDetector returns times relative to the SOURCE FILE's sample 0,
    // but sliceClipAtTimes expects timeline-absolute times (it compares
    // against clipStart..clipEnd). Map each transient into the clip's
    // timeline frame: timelineTime = clipStart + (transientTime - offset).
    // `offset` is the inaudible file head skipped before the clip's audible
    // region, so we only keep transients that fall inside the audible window
    // [clipStart, clipEnd]. Without this mapping every transient is < clipStart
    // (for clips not at timeline 0) and gets filtered out — slicing silently
    // does nothing.
    double clipStart = clip.getProperty(IDs::startTime);
    double clipDur = clip.getProperty(IDs::duration);
    double clipOffset = clip.getProperty(IDs::offset);
    double clipEnd = clipStart + clipDur;

    std::vector<double> timelineTimes;
    timelineTimes.reserve(result.transientTimes.size());
    for (double ft : result.transientTimes)
    {
        double t = clipStart + (ft - clipOffset);
        if (t > clipStart && t < clipEnd)
            timelineTimes.push_back(t);
    }
    if (timelineTimes.empty()) return;

    // Slice at detected transients
    auto slices = ProjectModel::sliceClipAtTimes(clip, timelineTimes, &um);
    
    // Rebuild routing for new clips
    if (auto* proc = engine_.getMainProcessor())
        proc->rebuildRoutingGraph();
}

void AudioEngineCommands::sliceClipAtPlayhead(int clipId)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int trackIdx = -1;
    auto clip = findClipById(clipId, trackIdx);
    if (!clip.isValid()) return;

    double playhead = engine_.getTransportManager().getCurrentPositionSeconds();
    double clipStart = static_cast<double>(clip.getProperty(IDs::startTime));
    double clipEnd = clipStart + static_cast<double>(clip.getProperty(IDs::duration));
    
    if (playhead <= clipStart || playhead >= clipEnd) return;
    
    auto slices = ProjectModel::sliceClipAtTimes(clip, {playhead}, &um);
    
    // Rebuild routing for new clips
    if (auto* proc = engine_.getMainProcessor())
        proc->rebuildRoutingGraph();
}

int AudioEngineCommands::copyAudioClipRegion(int clipId, double regionStart, double regionEnd)
{
    int trackIdx = -1;
    auto clip = findClipById(clipId, trackIdx);
    if (!clip.isValid() || trackIdx < 0) return -1;

    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (trackIdx >= trackList.getNumChildren()) return -1;

    // Validate the region against the clip's audible bounds. regionStart/End
    // are offsets within the clip (0 = clip start). Clamp then reject empty
    // regions so a garbage selection can't store a negative-duration or
    // out-of-source region that misbehaves on paste.
    double clipDur = clip.getProperty(IDs::duration, 0.0);
    double rs = std::clamp(regionStart, 0.0, clipDur);
    double re = std::clamp(regionEnd, 0.0, clipDur);
    if (re <= rs) return -1;

    juce::String sourceFile = clip.getProperty(IDs::sourceFile).toString();
    double clipOffset = clip.getProperty(IDs::offset, 0.0);
    double regOffset = clipOffset + rs;
    double regDuration = re - rs;

    HDAW::RegionClipboard::store({sourceFile, regOffset, regDuration});
    return 0;
}

int AudioEngineCommands::cutAudioClipRegion(int clipId, double regionStart, double regionEnd)
{
    if (regionEnd <= regionStart) return -1;

    copyAudioClipRegion(clipId, regionStart, regionEnd);

    int trackIdx = -1;
    auto clip = findClipById(clipId, trackIdx);
    if (!clip.isValid() || trackIdx < 0) return -1;

    auto& um = engine_.getProjectModel().getUndoManager();
    double startTime = clip.getProperty(IDs::startTime, 0.0);
    double slice1 = startTime + regionStart;
    double slice2 = startTime + regionEnd;

    // sliceClipAtTimes returns the created slices in order. Cutting a region
    // produces up to three slices [head, middle(=the cut region), tail]; the
    // one to delete is the middle slice whose startTime == slice1. We identify
    // it by the returned slice identities rather than by a fuzzy startTime
    // match, which is fragile when multiple clips share near-equal start times.
    auto slices = ProjectModel::sliceClipAtTimes(clip, {slice1, slice2}, &um);

    // Find the slice that starts at slice1 (the cut region) and remove it.
    for (const auto& s : slices)
    {
        double st = s.getProperty(IDs::startTime, 0.0);
        if (std::abs(st - slice1) < 1e-6)
        {
            s.getParent().removeChild(s, &um);
            break;
        }
    }

    if (auto* proc = engine_.getMainProcessor())
        proc->rebuildRoutingGraph();
    return 0;
}

int AudioEngineCommands::pasteAudioClipRegion(int clipId, double pasteTime)
{
    // NOTE: `clipId` is a track LOCATOR (we paste into the source clip's
    // track), not the paste target. The pasted clip is created as a new clip
    // at the absolute timeline `pasteTime` with the cached region's
    // sourceFile/offset/duration. Callers (AudioClipEditorWidget::onPasteRegion)
    // are responsible for choosing a sensible pasteTime.
    if (!HDAW::RegionClipboard::hasContent()) return -1;

    int trackIdx = -1;
    auto srcClip = findClipById(clipId, trackIdx);
    if (!srcClip.isValid() || trackIdx < 0) return -1;

    const auto& reg = HDAW::RegionClipboard::get();
    juce::String clipName = srcClip.getProperty(IDs::name).toString();
    juce::String newName = clipName + " (pasted)";

    int newId = addAudioClip(trackIdx, pasteTime, reg.duration,
                             reg.sourceFile.toStdString(), newName.toStdString());
    if (newId < 0) return -1;

    int newTrackIdx = -1;
    auto newClip = findClipById(newId, newTrackIdx);
    if (newClip.isValid()) {
        auto& um = engine_.getProjectModel().getUndoManager();
        newClip.setProperty(IDs::offset, reg.offset, &um);
    }

    engine_.getMainProcessor()->rebuildRoutingGraph();
    return newId;
}

void AudioEngineCommands::notifyClipGainEnvelopeChanged(int clipId)
{
    auto* proc = engine_.getMainProcessor();
    if (proc)
    {
        auto points = getGainEnvelopePoints(clipId);
        std::vector<HDAW::ClipSourceProcessor::GainPoint> pointsToSend;
        pointsToSend.reserve(points.size());
        for (const auto& p : points)
            pointsToSend.push_back({p.time, p.gain});
        proc->updateClipGainEnvelope(clipId, pointsToSend);
    }
}

// ─── ProjectCommands — Modulation (LFO) ──────────────────────────

void AudioEngineCommands::addLfo(int trackIndex)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren()) return;

    auto trackTree = trackList.getChild(trackIndex);
    auto modList = trackTree.getChildWithName(IDs::MODULATION_LIST);
    if (!modList.isValid())
    {
        modList = juce::ValueTree(IDs::MODULATION_LIST);
        trackTree.addChild(modList, -1, &um);
    }

    auto newMod = juce::ValueTree(IDs::MODULATION);
    newMod.setProperty("id", juce::String("lfo_") + juce::String(modList.getNumChildren() + 1), nullptr);
    newMod.setProperty("type", "lfo", nullptr);
    newMod.setProperty(IDs::name, juce::String("LFO ") + juce::String(modList.getNumChildren() + 1), nullptr);
    newMod.setProperty(IDs::waveform, 0, nullptr);
    newMod.setProperty(IDs::rate, 1.0, nullptr);
    newMod.setProperty(IDs::rateSync, true, nullptr);
    newMod.setProperty(IDs::depth, 0.3, nullptr);
    newMod.setProperty(IDs::bipolar, false, nullptr);
    newMod.setProperty(IDs::phaseOffset, 0.0, nullptr);
    newMod.setProperty(IDs::targetParamID, 1, nullptr);
    newMod.setProperty(IDs::enabled, true, nullptr);
    modList.addChild(newMod, -1, &um);
}

void AudioEngineCommands::removeLfo(int trackIndex, int lfoIndex)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren()) return;

    auto trackTree = trackList.getChild(trackIndex);
    auto modList = trackTree.getChildWithName(IDs::MODULATION_LIST);
    if (!modList.isValid() || lfoIndex < 0 || lfoIndex >= modList.getNumChildren()) return;

    modList.removeChild(modList.getChild(lfoIndex), &um);
}

void AudioEngineCommands::setLfoParam(int trackIndex, int lfoIndex,
                                      const std::string& paramName, double value)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren()) return;

    auto trackTree = trackList.getChild(trackIndex);
    auto modList = trackTree.getChildWithName(IDs::MODULATION_LIST);
    if (!modList.isValid() || lfoIndex < 0 || lfoIndex >= modList.getNumChildren()) return;

    auto modTree = modList.getChild(lfoIndex);

    // Map string parameter name to the corresponding Identifier
    if (paramName == "waveform")
        modTree.setProperty(IDs::waveform, static_cast<int>(value), &um);
    else if (paramName == "rate")
        modTree.setProperty(IDs::rate, value, &um);
    else if (paramName == "rateSync")
        modTree.setProperty(IDs::rateSync, value != 0.0, &um);
    else if (paramName == "depth")
        modTree.setProperty(IDs::depth, value, &um);
    else if (paramName == "bipolar")
        modTree.setProperty(IDs::bipolar, value != 0.0, &um);
    else if (paramName == "phaseOffset")
        modTree.setProperty(IDs::phaseOffset, value, &um);
    else if (paramName == "targetParamID")
        modTree.setProperty(IDs::targetParamID, static_cast<int>(value), &um);
    else if (paramName == "enabled")
        modTree.setProperty(IDs::enabled, value != 0.0, &um);
}

// ─── Missing source-file relinking ─────────────────────────────────

static const juce::StringArray audioExtensions{".wav", ".aiff", ".aif", ".mp3", ".flac", ".ogg"};

static juce::String searchForFile(const juce::String& missingPath, const juce::File& dir)
{
    juce::File missingFile(missingPath);
    auto name = missingFile.getFileName();
    auto baseName = missingFile.getFileNameWithoutExtension();
    auto ext = missingFile.getFileExtension();

    // Exact filename match
    juce::Array<juce::File> results;
    dir.findChildFiles(results, juce::File::findFiles, true, name);
    if (results.size() > 0)
        return results[0].getFullPathName();

    // Same basename, any audio extension
    for (const auto& tryExt : audioExtensions)
    {
        if (tryExt.toLowerCase() == ext.toLowerCase())
            continue;
        auto tryName = baseName + tryExt;
        dir.findChildFiles(results, juce::File::findFiles, true, tryName);
        if (results.size() > 0)
            return results[0].getFullPathName();
    }

    return {};
}

std::string AudioEngineCommands::findMissingClipSourceFile(int clipId, const std::string& searchDir)
{
    juce::File dir(searchDir.empty() ? "." : juce::String(searchDir));
    if (!dir.isDirectory())
        return {};

    int trackIdx = -1;
    auto clip = findClipById(clipId, trackIdx);
    if (!clip.isValid())
        return {};

    auto sourceFile = clip.getProperty(IDs::sourceFile).toString();
    if (sourceFile.isEmpty())
        return {};

    juce::File current(sourceFile);
    if (current.existsAsFile())
        return sourceFile.toStdString();

    auto found = searchForFile(sourceFile, dir);
    if (found.isNotEmpty())
    {
        auto& um = engine_.getProjectModel().getUndoManager();
        um.beginNewTransaction("Find missing clip source file");
        clip.setProperty(IDs::sourceFile, found, &um);
        engine_.getMainProcessor()->rebuildRoutingGraph();
        return found.toStdString();
    }
    return {};
}

ProjectCommands::RelinkResult AudioEngineCommands::relinkAllMissingFiles(const std::string& searchDir)
{
    RelinkResult result{0, 0};
    juce::File dir(searchDir.empty() ? "." : juce::String(searchDir));
    if (!dir.isDirectory())
        return result;

    auto& um = engine_.getProjectModel().getUndoManager();
    um.beginNewTransaction("Relink missing files");

    auto trackList = engine_.getProjectModel().getTrackListTree();
    bool anyRelinked = false;
    for (int t = 0; t < trackList.getNumChildren(); ++t)
    {
        auto clipList = trackList.getChild(t).getChildWithName(IDs::CLIP_LIST);
        if (!clipList.isValid()) continue;
        for (int c = 0; c < clipList.getNumChildren(); ++c)
        {
            auto clip = clipList.getChild(c);
            if (clip.getProperty(IDs::clipType).toString() != "audio")
                continue;
            auto sourceFile = clip.getProperty(IDs::sourceFile).toString();
            if (sourceFile.isEmpty())
                continue;
            juce::File current(sourceFile);
            if (current.existsAsFile())
                continue;

            result.totalMissing++;
            auto found = searchForFile(sourceFile, dir);
            if (found.isNotEmpty())
            {
                clip.setProperty(IDs::sourceFile, found, &um);
                result.found++;
                anyRelinked = true;
            }
        }
    }

    if (anyRelinked)
        engine_.getMainProcessor()->rebuildRoutingGraph();

    return result;
}
