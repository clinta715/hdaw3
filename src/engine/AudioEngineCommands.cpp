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
    auto& model = engine_.getProjectModel();
    auto& um = model.getUndoManager();
    auto trackList = model.getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren()) return -1;
    auto source = trackList.getChild(trackIndex);
    auto copy = source.createCopy();

    // Append " copy" to the track name (guard against "copy copy" chains)
    auto origName = copy.getProperty(IDs::name).toString();
    if (!origName.endsWith(" copy"))
        copy.setProperty(IDs::name, origName + " copy", &um);

    // Re-assign clip IDs to avoid collisions with the source
    auto clipList = copy.getChildWithName(IDs::CLIP_LIST);
    for (int c = 0; c < clipList.getNumChildren(); ++c)
    {
        auto clip = clipList.getChild(c);
        clip.setProperty(IDs::clipID, model.allocateClipID(), nullptr);

        // Re-assign note IDs inside MIDI clips
        auto noteList = clip.getChildWithName(IDs::MIDI_NOTE_LIST);
        for (int n = 0; n < noteList.getNumChildren(); ++n)
        {
            auto note = noteList.getChild(n);
            note.setProperty(IDs::noteID, model.allocateNoteID(), nullptr);
        }
    }

    int newIdx = trackList.getNumChildren();
    trackList.addChild(copy, newIdx, &um);
    return newIdx;
}

// ─── ProjectCommands — Ghost clips ──────────────────────────────

int AudioEngineCommands::createGhostClip(int sourceClipId, double newStart, int newTrackIndex)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int trackIdx = -1;
    auto clip = findClipById(sourceClipId, trackIdx);
    if (!clip.isValid() || trackIdx < 0) return -1;

    // If the source is itself a ghost, walk the chain to find the root
    int rootId = sourceClipId;
    while (true)
    {
        auto rootClip = findClipById(rootId, trackIdx);
        if (!rootClip.isValid()) break;
        int parentId = static_cast<int>(rootClip.getProperty(IDs::ghostSourceId, -1));
        if (parentId < 0) break;
        rootId = parentId;
    }

    // Deep-copy the clip
    auto newClip = clip.createCopy();
    int newId = ProjectModel::allocateClipID();
    newClip.setProperty(IDs::clipID, newId, nullptr);
    newClip.setProperty(IDs::ghostSourceId, rootId, nullptr);
    newClip.setProperty(IDs::isGhost, 1, nullptr);
    newClip.setProperty(IDs::startTime, newStart, nullptr);

    // Remove MIDI_NOTE_LIST so it gets a fresh one via the listener
    auto noteList = newClip.getChildWithName(IDs::MIDI_NOTE_LIST);
    if (noteList.isValid())
        newClip.removeChild(noteList, nullptr);

    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (newTrackIndex < 0 || newTrackIndex >= trackList.getNumChildren())
        return -1;
    auto clipList = trackList.getChild(newTrackIndex).getChildWithName(IDs::CLIP_LIST);
    if (!clipList.isValid()) return -1;
    clipList.addChild(newClip, -1, &um);

    // Copy MIDI notes from root to ghost
    auto rootClip = findClipById(rootId, trackIdx);
    if (rootClip.isValid())
    {
        auto rootNoteList = rootClip.getChildWithName(IDs::MIDI_NOTE_LIST);
        if (rootNoteList.isValid())
        {
            auto ghostNoteList = juce::ValueTree(IDs::MIDI_NOTE_LIST);
            newClip.addChild(ghostNoteList, -1, &um);
            for (int i = 0; i < rootNoteList.getNumChildren(); ++i)
            {
                auto noteCopy = rootNoteList.getChild(i).createCopy();
                noteCopy.setProperty(IDs::noteID, ProjectModel::allocateClipID(), &um);
                ghostNoteList.addChild(noteCopy, -1, &um);
            }
        }
    }

    return newId;
}

std::vector<int> AudioEngineCommands::paintClips(const std::vector<int>& sourceClipIds, double originBeat, double spacing, int targetTrackIndex, int count)
{
    std::vector<int> result;
    auto& um = engine_.getProjectModel().getUndoManager();
    auto& model = engine_.getProjectModel();
    auto trackList = model.getTrackListTree();
    if (targetTrackIndex < 0 || targetTrackIndex >= trackList.getNumChildren())
        return result;
    auto clipList = trackList.getChild(targetTrackIndex).getChildWithName(IDs::CLIP_LIST);
    if (!clipList.isValid()) return result;

    // Get the start time of the first source clip as the pattern origin
    double patternOrigin = 0.0;
    if (!sourceClipIds.empty())
    {
        int tmpIdx = -1;
        auto firstClip = findClipById(sourceClipIds[0], tmpIdx);
        if (firstClip.isValid())
            patternOrigin = firstClip.getProperty(IDs::startTime, 0.0);
    }

    beginTransaction("Paint clips");

    for (int tile = 0; tile < count; ++tile)
    {
        for (size_t s = 0; s < sourceClipIds.size(); ++s)
        {
            int srcId = sourceClipIds[s];
            int tmpIdx = -1;
            auto srcClip = findClipById(srcId, tmpIdx);
            if (!srcClip.isValid()) continue;

            // Walk ghost chain to root
            int rootId = srcId;
            while (true)
            {
                auto rc = findClipById(rootId, tmpIdx);
                if (!rc.isValid()) break;
                int pid = static_cast<int>(rc.getProperty(IDs::ghostSourceId, -1));
                if (pid < 0) break;
                rootId = pid;
            }

            auto newClip = srcClip.createCopy();
            int newId = ProjectModel::allocateClipID();
            newClip.setProperty(IDs::clipID, newId, &um);
            newClip.setProperty(IDs::ghostSourceId, rootId, &um);
            newClip.setProperty(IDs::isGhost, 1, &um);

            // Remove existing MIDI_NOTE_LIST from the copy (we'll add fresh notes)
            auto existingNoteList = newClip.getChildWithName(IDs::MIDI_NOTE_LIST);
            if (existingNoteList.isValid())
                newClip.removeChild(existingNoteList, &um);

            // Compute relative offset within the pattern
            double srcStart = srcClip.getProperty(IDs::startTime, 0.0);
            double relativeOffset = srcStart - patternOrigin;
            newClip.setProperty(IDs::startTime, originBeat + (tile + 1) * spacing + relativeOffset, &um);

            // Copy MIDI notes from root
            int rootIdx = -1;
            auto rootClip = findClipById(rootId, rootIdx);
            if (rootClip.isValid())
            {
                auto rootNoteList = rootClip.getChildWithName(IDs::MIDI_NOTE_LIST);
                if (rootNoteList.isValid())
                {
                    auto ghostNoteList = juce::ValueTree(IDs::MIDI_NOTE_LIST);
                    newClip.addChild(ghostNoteList, -1, &um);
                    for (int n = 0; n < rootNoteList.getNumChildren(); ++n)
                    {
                        auto noteCopy = rootNoteList.getChild(n).createCopy();
                        noteCopy.setProperty(IDs::noteID, ProjectModel::allocateClipID(), &um);
                        ghostNoteList.addChild(noteCopy, -1, &um);
                    }
                }
            }

            clipList.addChild(newClip, -1, &um);
            result.push_back(newId);
        }
    }

    endTransaction();
    return result;
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
    auto* proc = engine_.getMainProcessor();
    if (proc) proc->rebuildRoutingGraph();
}

bool AudioEngineCommands::saveProject(const std::string& filePath)
{
    return HDAW::ProjectSerializer::save(engine_.getProjectModel(), juce::File(filePath));
}

bool AudioEngineCommands::loadProject(const std::string& filePath)
{
    bool ok = HDAW::ProjectSerializer::load(engine_.getProjectModel(), juce::File(filePath));
    if (ok)
    {
        auto* proc = engine_.getMainProcessor();
        HDAW_LOG("DIAG", "loadProject: calling rebuildRoutingGraph after load, trackCount=" + std::to_string(engine_.getProjectModel().getTrackListTree().getNumChildren()));
        if (proc) proc->rebuildRoutingGraph();
    }
    else
    {
        HDAW_LOG("DIAG", "loadProject: load FAILED");
    }
    return ok;
}

// ─── ProjectCommands — Scale ──────────────────────────────────────

void AudioEngineCommands::setScaleRoot(int root) { engine_.getProjectModel().setScaleRoot(root); }
void AudioEngineCommands::setScaleMode(int mode) { engine_.getProjectModel().setScaleMode(mode); }

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

void AudioEngineCommands::setClipGainEnvelope(int clipId,
                                              const std::vector<std::pair<double, double>>& points)
{
    // Replace the whole envelope in one undo step. JUCE's UndoManager treats
    // everything between two beginNewTransaction calls as one step, so we
    // start one here and the next edit anywhere else will close it. Note:
    // ProjectModel::addGainEnvelopePoint inserts in time order, so the input
    // vector doesn't have to be pre-sorted.
    auto& um = engine_.getProjectModel().getUndoManager();
    int trackIdx = -1;
    auto clip = findClipById(clipId, trackIdx);
    if (!clip.isValid()) return;

    um.beginNewTransaction("setClipGainEnvelope");
    auto envelope = clip.getChildWithName(IDs::GAIN_ENVELOPE);
    if (envelope.isValid())
        clip.removeChild(envelope, &um);
    envelope = ProjectModel::ensureGainEnvelope(clip, &um);
    for (const auto& [time, gain] : points)
        ProjectModel::addGainEnvelopePoint(envelope, time, gain, &um);
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
