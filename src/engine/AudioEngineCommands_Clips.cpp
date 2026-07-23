#include "AudioEngineCommands.h"
#include "AudioEngineCommands_Helpers.h"
#include "AudioEngine.h"
#include "../model/ProjectModel.h"
#include "../common/DebugLog.h"

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

void AudioEngineCommands::moveClipWithOverlap(int clipId, int newTrackIndex, double newStart)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int oldTrackIdx = -1;
    auto clip = findClipById(clipId, oldTrackIdx);
    if (!clip.isValid()) return;
    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (newTrackIndex < 0 || newTrackIndex >= trackList.getNumChildren()) return;

    double clipDur = clip.getProperty(IDs::duration);
    double newEnd = newStart + clipDur;

    // Move to target track first if needed
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

    // Get the clip list on the target track
    auto targetTrack = trackList.getChild(newTrackIndex);
    auto clipList = targetTrack.getChildWithName(IDs::CLIP_LIST);
    if (!clipList.isValid())
    {
        clip.setProperty(IDs::startTime, newStart, &um);
        return;
    }

    // Find all clips that overlap with the new position (excluding self)
    // Collect them first to avoid modifying the list while iterating
    struct OverlapInfo {
        juce::ValueTree clip;
        double start;
        double end;
    };
    std::vector<OverlapInfo> overlapping;

    for (int i = 0; i < clipList.getNumChildren(); ++i)
    {
        auto other = clipList.getChild(i);
        int otherId = static_cast<int>(other.getProperty(IDs::clipID, 0));
        if (otherId == clipId) continue;

        double otherStart = other.getProperty(IDs::startTime);
        double otherDur = other.getProperty(IDs::duration);
        double otherEnd = otherStart + otherDur;

        // Check if there's an overlap
        if (newStart < otherEnd && newEnd > otherStart)
        {
            overlapping.push_back({ other, otherStart, otherEnd });
        }
    }

    // Process each overlapping clip
    for (auto& info : overlapping)
    {
        double otherStart = info.start;
        double otherEnd = info.end;
        double otherDur = otherEnd - otherStart;

        if (newStart <= otherStart && newEnd >= otherEnd)
        {
            // Case 1: Incoming clip fully covers the existing clip → remove it
            clipList.removeChild(info.clip, &um);
        }
        else if (newStart <= otherStart && newEnd > otherStart && newEnd < otherEnd)
        {
            // Case 2: Incoming clip overlaps the left portion → trim existing to the right
            double newOtherStart = newEnd;
            double newOtherDur = otherEnd - newOtherStart;
            double newOtherOffset = static_cast<double>(info.clip.getProperty(IDs::offset)) + (newOtherStart - otherStart);
            info.clip.setProperty(IDs::startTime, newOtherStart, &um);
            info.clip.setProperty(IDs::duration, newOtherDur, &um);
            info.clip.setProperty(IDs::offset, newOtherOffset, &um);
        }
        else if (newStart > otherStart && newEnd >= otherEnd)
        {
            // Case 3: Incoming clip overlaps the right portion → trim existing to the left
            double newOtherDur = newStart - otherStart;
            info.clip.setProperty(IDs::duration, newOtherDur, &um);
        }
        else if (newStart > otherStart && newEnd < otherEnd)
        {
            // Case 4: Incoming clip is in the middle → split existing into two
            // First, create the right portion
            auto rightClip = info.clip.createCopy();
            double rightStart = newEnd;
            double rightDur = otherEnd - newEnd;
            double rightOffset = static_cast<double>(info.clip.getProperty(IDs::offset)) + (newStart - otherStart) + clipDur;
            rightClip.setProperty(IDs::startTime, rightStart, &um);
            rightClip.setProperty(IDs::duration, rightDur, &um);
            rightClip.setProperty(IDs::offset, rightOffset, &um);
            rightClip.setProperty(IDs::clipID, ProjectModel::allocateClipID(), &um);
            clipList.addChild(rightClip, -1, &um);

            // Trim the left portion
            double leftDur = newStart - otherStart;
            info.clip.setProperty(IDs::duration, leftDur, &um);
        }
    }

    // Finally, set the incoming clip's new start position
    clip.setProperty(IDs::startTime, newStart, &um);
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

void AudioEngineCommands::setClipMuted(int clipId, bool muted)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int trackIdx = -1;
    auto clip = findClipById(clipId, trackIdx);
    if (clip.isValid())
        clip.setProperty(IDs::muted, muted, &um);
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
