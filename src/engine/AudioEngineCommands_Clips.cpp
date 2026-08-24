#include "AudioEngineCommands.h"
#include "AudioEngineCommands_Helpers.h"
#include "AudioEngine.h"
#include "ArrangementGenerator.h"
#include "../model/ProjectModel.h"
#include "../common/DebugLog.h"

#include <limits>

// ─── ProjectCommands — Clip operations ────────────────────────────

int AudioEngineCommands::addAudioClip(int trackIndex, double start, double duration,
                                      const std::string& sourceFile, const std::string& name)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren()) return -1;

    // Convert beats → seconds (frontend sends beats, processors expect seconds)
    double bpm = engine_.getTransportManager().getBPM();
    double startSec = HDAW::beatsToSeconds(start, bpm);
    double durSec = HDAW::beatsToSeconds(duration, bpm);

    auto clip = engine_.getProjectModel().createAudioClip(
        juce::String(name), startSec, durSec, juce::String(sourceFile));

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

    // Convert beats → seconds (frontend sends beats, processors expect seconds)
    double bpm = engine_.getTransportManager().getBPM();
    double startSec = HDAW::beatsToSeconds(start, bpm);
    double durSec = HDAW::beatsToSeconds(duration, bpm);

    auto clip = engine_.getProjectModel().createMidiClipEmpty(
        juce::String(name), startSec, durSec);

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

    double newStartSec = HDAW::beatsToSeconds(newStart, engine_.getTransportManager().getBPM());
    clip.setProperty(IDs::startTime, newStartSec, &um);

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

    double newStartSec = HDAW::beatsToSeconds(newStart, engine_.getTransportManager().getBPM());
    double clipDur = clip.getProperty(IDs::duration);
    double newEnd = newStartSec + clipDur;

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
        clip.setProperty(IDs::startTime, newStartSec, &um);
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
        if (newStartSec < otherEnd && newEnd > otherStart)
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

        if (newStartSec <= otherStart && newEnd >= otherEnd)
        {
            // Case 1: Incoming clip fully covers the existing clip → remove it.
            // Overwrite rule: a fully-shadowed clip is discarded so parts never
            // overlap. Undo-safe: the caller wraps batch ops in a transaction and
            // removeChild records on the undo manager, so it is restorable. The
            // `overlapping` vector was collected before any mutation, so removing
            // here is safe.
            clipList.removeChild(info.clip, &um);
        }
        else if (newStartSec <= otherStart && newEnd > otherStart && newEnd < otherEnd)
        {
            // Case 2: Incoming clip overlaps the left portion → trim existing to the right
            double newOtherStart = newEnd;
            double newOtherDur = otherEnd - newOtherStart;
            double newOtherOffset = static_cast<double>(info.clip.getProperty(IDs::offset)) + (newOtherStart - otherStart);
            info.clip.setProperty(IDs::startTime, newOtherStart, &um);
            info.clip.setProperty(IDs::duration, newOtherDur, &um);
            info.clip.setProperty(IDs::offset, newOtherOffset, &um);
        }
        else if (newStartSec > otherStart && newEnd >= otherEnd)
        {
            // Case 3: Incoming clip overlaps the right portion → trim existing to the left
            double newOtherDur = newStartSec - otherStart;
            info.clip.setProperty(IDs::duration, newOtherDur, &um);
        }
        else if (newStartSec > otherStart && newEnd < otherEnd)
        {
            // Case 4: Incoming clip is in the middle → split existing into two
            // First, create the right portion
            auto rightClip = info.clip.createCopy();
            double rightStart = newEnd;
            double rightDur = otherEnd - newEnd;
            double rightOffset = static_cast<double>(info.clip.getProperty(IDs::offset)) + (newStartSec - otherStart) + clipDur;
            rightClip.setProperty(IDs::startTime, rightStart, &um);
            rightClip.setProperty(IDs::duration, rightDur, &um);
            rightClip.setProperty(IDs::offset, rightOffset, &um);
            rightClip.setProperty(IDs::clipID, engine_.getProjectModel().allocateClipID(), &um);
            clipList.addChild(rightClip, -1, &um);

            // Trim the left portion
            double leftDur = newStartSec - otherStart;
            info.clip.setProperty(IDs::duration, leftDur, &um);
        }
    }

    // Finally, set the incoming clip's new start position
    clip.setProperty(IDs::startTime, newStartSec, &um);
}

void AudioEngineCommands::setClipStart(int clipId, double start)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int trackIdx = -1;
    auto clip = findClipById(clipId, trackIdx);
    if (clip.isValid())
    {
        double startSec = HDAW::beatsToSeconds(start, engine_.getTransportManager().getBPM());
        clip.setProperty(IDs::startTime, startSec, &um);
    }
}

void AudioEngineCommands::setClipDuration(int clipId, double duration)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int trackIdx = -1;
    auto clip = findClipById(clipId, trackIdx);
    if (clip.isValid())
    {
        double durSec = HDAW::beatsToSeconds(duration, engine_.getTransportManager().getBPM());
        clip.setProperty(IDs::duration, durSec, &um);
    }
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

void AudioEngineCommands::setClipName(int clipId, const std::string& name)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int trackIdx = -1;
    auto clip = findClipById(clipId, trackIdx);
    if (clip.isValid())
        clip.setProperty(IDs::name, juce::String(name), &um);
}

int AudioEngineCommands::duplicateClip(int clipId)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int trackIdx = -1;
    auto clip = findClipById(clipId, trackIdx);
    if (!clip.isValid() || trackIdx < 0) return -1;

    auto newClip = clip.createCopy();
    newClip.setProperty(IDs::clipID, engine_.getProjectModel().allocateClipID(), nullptr);
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

int AudioEngineCommands::duplicateClipTo(int clipId, double newStart, int newTrackIndex)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int trackIdx = -1;
    auto clip = findClipById(clipId, trackIdx);
    if (!clip.isValid() || trackIdx < 0) return -1;

    // Deep-copy the clip and reassign its identity (mirrors duplicateClip).
    auto newClip = clip.createCopy();
    newClip.setProperty(IDs::clipID, engine_.getProjectModel().allocateClipID(), nullptr);
    juce::String origName = newClip.getProperty(IDs::name).toString();
    if (!origName.endsWith(" copy"))
        newClip.setProperty(IDs::name, origName + " copy", nullptr);

    // Place it at the requested position on the target track (mirrors the
    // positioning + target-track logic of createGhostClip, without the ghost
    // re-parenting). Unlike duplicateClip this is a direct placement, so the
    // frontend doesn't need a follow-up moveClipWithOverlap round trip.
    double newStartSec = HDAW::beatsToSeconds(newStart, engine_.getTransportManager().getBPM());
    newClip.setProperty(IDs::startTime, newStartSec, nullptr);

    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (newTrackIndex < 0 || newTrackIndex >= trackList.getNumChildren())
        return -1;
    auto clipList = trackList.getChild(newTrackIndex).getChildWithName(IDs::CLIP_LIST);
    if (!clipList.isValid()) return -1;
    clipList.addChild(newClip, -1, &um);

    // Handle overlaps — same as moveClipWithOverlap.
    int newId = static_cast<int>(newClip.getProperty(IDs::clipID, 0));
    moveClipWithOverlap(newId, newTrackIndex, newStart);
    return newId;
}

std::vector<int> AudioEngineCommands::duplicateClips(const std::vector<int>& clipIds, const std::vector<double>& newStarts, const std::vector<int>& newTrackIndices)
{
    std::vector<int> result;
    if (clipIds.size() != newStarts.size() || clipIds.size() != newTrackIndices.size())
        return result;

    auto& um = engine_.getProjectModel().getUndoManager();
    auto trackList = engine_.getProjectModel().getTrackListTree();

    beginTransaction("Duplicate clips");
    for (size_t i = 0; i < clipIds.size(); ++i)
    {
        int trackIdx = -1;
        auto clip = findClipById(clipIds[i], trackIdx);
        if (!clip.isValid() || trackIdx < 0) { result.push_back(-1); continue; }

        int targetTrack = newTrackIndices[i];
        if (targetTrack < 0 || targetTrack >= trackList.getNumChildren()) { result.push_back(-1); continue; }

        auto newClip = clip.createCopy();
        int newId = engine_.getProjectModel().allocateClipID();
        newClip.setProperty(IDs::clipID, newId, nullptr);
        juce::String origName = newClip.getProperty(IDs::name).toString();
        if (!origName.endsWith(" copy"))
            newClip.setProperty(IDs::name, origName + " copy", nullptr);
        double newStartSec = HDAW::beatsToSeconds(newStarts[i], engine_.getTransportManager().getBPM());
        newClip.setProperty(IDs::startTime, newStartSec, nullptr);

        auto clipList = trackList.getChild(targetTrack).getChildWithName(IDs::CLIP_LIST);
        if (!clipList.isValid()) { result.push_back(-1); continue; }
        clipList.addChild(newClip, -1, &um);

        // Handle overlaps — same as moveClipWithOverlap.
        // findClipById will now find the newly added clip.
        moveClipWithOverlap(newId, targetTrack, newStarts[i]);

        result.push_back(newId);
    }
    endTransaction();
    return result;
}

void AudioEngineCommands::moveClips(const std::vector<int>& clipIds, const std::vector<double>& newStarts, const std::vector<int>& newTrackIndices)
{
    if (clipIds.size() != newStarts.size() || clipIds.size() != newTrackIndices.size())
        return;

    beginTransaction("Move clips");
    for (size_t i = 0; i < clipIds.size(); ++i)
    {
        moveClipWithOverlap(clipIds[i], newTrackIndices[i], newStarts[i]);
    }
    endTransaction();
}

void AudioEngineCommands::removeClips(const std::vector<int>& clipIds)
{
    beginTransaction("Remove clips");
    for (int id : clipIds)
    {
        removeClip(id);
    }
    endTransaction();
}

void AudioEngineCommands::rippleDelete(double startBeat, double endBeat)
{
    if (endBeat <= startBeat) return;  // empty/invalid range: no-op

    double bpm = engine_.getTransportManager().getBPM();
    double rs = HDAW::beatsToSeconds(startBeat, bpm);   // range start, seconds
    double re = HDAW::beatsToSeconds(endBeat, bpm);     // range end, seconds
    double rangeLen = re - rs;
    if (rangeLen <= 0.0) return;

    auto& um = engine_.getProjectModel().getUndoManager();
    auto trackList = engine_.getProjectModel().getTrackListTree();

    beginTransaction("Ripple delete");

    // Phase 1: slice every clip that straddles a range boundary, so that
    // afterward each clip is fully-inside, fully-before, or fully-after.
    // Use the model-level slice (no per-clip routing rebuild); one rebuild
    // runs at the end of this command.
    for (int t = 0; t < trackList.getNumChildren(); ++t)
    {
        auto clipList = trackList.getChild(t).getChildWithName(IDs::CLIP_LIST);
        if (!clipList.isValid()) continue;

        // Collect first: slicing mutates the tree and invalidates iteration.
        std::vector<std::pair<int, std::vector<double>>> toSlice;
        for (int c = 0; c < clipList.getNumChildren(); ++c)
        {
            auto clip = clipList.getChild(c);
            double cs = clip.getProperty(IDs::startTime);
            double ce = cs + static_cast<double>(clip.getProperty(IDs::duration));
            std::vector<double> times;
            if (cs < rs && rs < ce) times.push_back(rs);  // straddles start
            if (cs < re && re < ce) times.push_back(re);  // straddles end
            if (!times.empty())
                toSlice.push_back({ static_cast<int>(clip.getProperty(IDs::clipID)), times });
        }
        for (auto& [id, times] : toSlice)
        {
            int ti = -1;
            auto clip = findClipById(id, ti);
            if (clip.isValid())
                engine_.getProjectModel().sliceClipAtTimes(clip, times, &um);
        }
    }

    // Phase 2: classify every clip against [rs, re] and act. After phase 1
    // no clip straddles a boundary, so the inside/before/after split is clean.
    std::vector<int> toRemove;
    struct ShiftInfo { juce::ValueTree clip; double newStartSec; };
    std::vector<ShiftInfo> toShift;

    for (int t = 0; t < trackList.getNumChildren(); ++t)
    {
        auto clipList = trackList.getChild(t).getChildWithName(IDs::CLIP_LIST);
        if (!clipList.isValid()) continue;
        for (int c = 0; c < clipList.getNumChildren(); ++c)
        {
            auto clip = clipList.getChild(c);
            double cs = clip.getProperty(IDs::startTime);
            double ce = cs + static_cast<double>(clip.getProperty(IDs::duration));

            if (cs >= rs && ce <= re)
                toRemove.push_back(static_cast<int>(clip.getProperty(IDs::clipID)));
            else if (cs >= re)
                toShift.push_back({ clip, cs - rangeLen });
            // fully-before (ce <= rs): untouched
        }
    }

    for (int id : toRemove)
    {
        int ti = -1;
        auto clip = findClipById(id, ti);
        if (clip.isValid())
            clip.getParent().removeChild(clip, &um);
    }
    for (auto& s : toShift)
        s.clip.setProperty(IDs::startTime, s.newStartSec, &um);

    if (auto* proc = engine_.getMainProcessor())
        proc->rebuildRoutingGraph();

    endTransaction();
}

void AudioEngineCommands::insertSilence(double startBeat, double endBeat)
{
    if (endBeat <= startBeat) return;  // empty/invalid range: no-op

    double bpm = engine_.getTransportManager().getBPM();
    double rs = HDAW::beatsToSeconds(startBeat, bpm);   // insertion point, seconds
    double re = HDAW::beatsToSeconds(endBeat, bpm);
    double gapLen = re - rs;
    if (gapLen <= 0.0) return;

    auto& um = engine_.getProjectModel().getUndoManager();
    auto trackList = engine_.getProjectModel().getTrackListTree();

    beginTransaction("Insert silence");

    // Phase 1: slice clips straddling the insertion point rs (only one slice
    // point -- re just defines the gap length). Model-level slice: no rebuild.
    for (int t = 0; t < trackList.getNumChildren(); ++t)
    {
        auto clipList = trackList.getChild(t).getChildWithName(IDs::CLIP_LIST);
        if (!clipList.isValid()) continue;
        std::vector<int> toSlice;
        for (int c = 0; c < clipList.getNumChildren(); ++c)
        {
            auto clip = clipList.getChild(c);
            double cs = clip.getProperty(IDs::startTime);
            double ce = cs + static_cast<double>(clip.getProperty(IDs::duration));
            if (cs < rs && rs < ce)                       // straddles insertion point
                toSlice.push_back(static_cast<int>(clip.getProperty(IDs::clipID)));
        }
        for (int id : toSlice)
        {
            int ti = -1;
            auto clip = findClipById(id, ti);
            if (clip.isValid())
                engine_.getProjectModel().sliceClipAtTimes(clip, { rs }, &um);
        }
    }

    // Phase 2: shift every clip with start >= rs right by gapLen. After phase 1
    // no clip straddles rs, so "start >= rs" cleanly selects the moved tail.
    std::vector<std::pair<juce::ValueTree, double>> toShift;
    for (int t = 0; t < trackList.getNumChildren(); ++t)
    {
        auto clipList = trackList.getChild(t).getChildWithName(IDs::CLIP_LIST);
        if (!clipList.isValid()) continue;
        for (int c = 0; c < clipList.getNumChildren(); ++c)
        {
            auto clip = clipList.getChild(c);
            double cs = clip.getProperty(IDs::startTime);
            if (cs >= rs)
                toShift.push_back({ clip, cs + gapLen });
        }
    }
    for (auto& s : toShift)
        s.first.setProperty(IDs::startTime, s.second, &um);

    if (auto* proc = engine_.getMainProcessor())
        proc->rebuildRoutingGraph();

    endTransaction();
}

void AudioEngineCommands::duplicateRegion(double startBeat, double endBeat)
{
    if (endBeat <= startBeat) return;  // empty/invalid range: no-op

    double bpm = engine_.getTransportManager().getBPM();
    double rs = HDAW::beatsToSeconds(startBeat, bpm);
    double re = HDAW::beatsToSeconds(endBeat, bpm);
    double regionLen = re - rs;
    if (regionLen <= 0.0) return;

    auto& um = engine_.getProjectModel().getUndoManager();
    auto trackList = engine_.getProjectModel().getTrackListTree();

    beginTransaction("Duplicate region");

    // Phase 1: slice clips straddling rs or re, so inside/before/after are clean.
    for (int t = 0; t < trackList.getNumChildren(); ++t)
    {
        auto clipList = trackList.getChild(t).getChildWithName(IDs::CLIP_LIST);
        if (!clipList.isValid()) continue;
        std::vector<std::pair<int, std::vector<double>>> toSlice;
        for (int c = 0; c < clipList.getNumChildren(); ++c)
        {
            auto clip = clipList.getChild(c);
            double cs = clip.getProperty(IDs::startTime);
            double ce = cs + static_cast<double>(clip.getProperty(IDs::duration));
            std::vector<double> times;
            if (cs < rs && rs < ce) times.push_back(rs);
            if (cs < re && re < ce) times.push_back(re);
            if (!times.empty())
                toSlice.push_back({ static_cast<int>(clip.getProperty(IDs::clipID)), times });
        }
        for (auto& [id, times] : toSlice)
        {
            int ti = -1;
            auto clip = findClipById(id, ti);
            if (clip.isValid())
                engine_.getProjectModel().sliceClipAtTimes(clip, times, &um);
        }
    }

    // Phase 2: collect inside clips (to copy) and after-clip ids (to shift).
    // Collect BEFORE copying so the copies are never in the shift set.
    struct InsideClip { juce::ValueTree clip; int trackIndex; double startSec; };
    std::vector<InsideClip> insideClips;
    std::vector<int> afterIds;
    for (int t = 0; t < trackList.getNumChildren(); ++t)
    {
        auto clipList = trackList.getChild(t).getChildWithName(IDs::CLIP_LIST);
        if (!clipList.isValid()) continue;
        for (int c = 0; c < clipList.getNumChildren(); ++c)
        {
            auto clip = clipList.getChild(c);
            double cs = clip.getProperty(IDs::startTime);
            double ce = cs + static_cast<double>(clip.getProperty(IDs::duration));
            if (cs >= rs && ce <= re)
                insideClips.push_back({ clip, t, cs });
            else if (cs >= re)
                afterIds.push_back(static_cast<int>(clip.getProperty(IDs::clipID)));
        }
    }

    // Phase 3: shift after-clips right by regionLen (by id, so copies added
    // next are not affected).
    for (int id : afterIds)
    {
        int ti = -1;
        auto clip = findClipById(id, ti);
        if (clip.isValid())
        {
            double cs = clip.getProperty(IDs::startTime);
            clip.setProperty(IDs::startTime, cs + regionLen, &um);
        }
    }

    // Phase 4: copy each inside clip to startSec + regionLen (lands in [re, re+len]).
    // createCopy deep-copies notes/gain-envelope; mint a fresh id.
    for (const auto& ic : insideClips)
    {
        auto newClip = ic.clip.createCopy();
        newClip.setProperty(IDs::clipID, engine_.getProjectModel().allocateClipID(), &um);
        newClip.setProperty(IDs::startTime, ic.startSec + regionLen, &um);
        juce::String origName = newClip.getProperty(IDs::name).toString();
        if (!origName.endsWith(" copy"))
            newClip.setProperty(IDs::name, origName + " copy", &um);
        auto clipList = trackList.getChild(ic.trackIndex).getChildWithName(IDs::CLIP_LIST);
        if (clipList.isValid())
            clipList.addChild(newClip, -1, &um);
    }

    if (auto* proc = engine_.getMainProcessor())
        proc->rebuildRoutingGraph();

    endTransaction();
}

std::vector<int> AudioEngineCommands::addClips(int trackIndex, const std::vector<double>& starts, const std::vector<double>& durations, const std::vector<std::string>& names, const std::vector<std::string>& sourceFiles)
{
    std::vector<int> result;
    if (starts.size() != durations.size() || starts.size() != names.size())
        return result;

    auto& um = engine_.getProjectModel().getUndoManager();
    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren())
        return result;

    auto track = trackList.getChild(trackIndex);
    auto clipList = track.getChildWithName(IDs::CLIP_LIST);
    if (!clipList.isValid())
    {
        clipList = juce::ValueTree(IDs::CLIP_LIST);
        track.addChild(clipList, -1, &um);
    }

    // Convert beats → seconds (frontend sends beats, processors expect seconds)
    double bpm = engine_.getTransportManager().getBPM();

    beginTransaction("Add clips");
    for (size_t i = 0; i < starts.size(); ++i)
    {
        double startSec = HDAW::beatsToSeconds(starts[i], bpm);
        double durSec = HDAW::beatsToSeconds(durations[i], bpm);

        juce::ValueTree clip;
        if (i < sourceFiles.size() && !sourceFiles[i].empty())
        {
            clip = engine_.getProjectModel().createAudioClip(
                juce::String(names[i]), startSec, durSec, juce::String(sourceFiles[i]));
        }
        else
        {
            clip = engine_.getProjectModel().createMidiClipEmpty(
                juce::String(names[i]), startSec, durSec);
        }

        int clipId = static_cast<int>(clip.getProperty(IDs::clipID, 0));
        clipList.addChild(clip, -1, &um);

        // Handle overlaps — same as moveClipWithOverlap.
        moveClipWithOverlap(clipId, trackIndex, starts[i]);

        result.push_back(clipId);
    }
    endTransaction();
    return result;
}

ProjectCommands::ArrangementResult AudioEngineCommands::generateArrangement(const HDAW::ArrangementParams& params)
{
    ArrangementResult result;
    auto arr = HDAW::generateArrangement(params);
    result.seed = arr.resolvedSeed;
    if (arr.parts.empty())
        return result;

    const double totalBeats = (std::max)(1, params.bars) * 4.0;
    auto trackList = engine_.getProjectModel().getTrackListTree();

    beginTransaction("Generate arrangement");

    for (const auto& part : arr.parts)
    {
        int trackIdx = -1;
        auto normalizeRoleName = [](const std::string& s) {
            std::string out;
            out.reserve(s.size());
            for (char c : s)
                if (c != ' ') out += c;
            return out;
        };
        const std::string normalizedName = normalizeRoleName(part.name);
        for (const auto& [key, val] : params.targetTrackIds)
        {
            if (normalizeRoleName(key) == normalizedName)
            {
                trackIdx = val;
                break;
            }
        }
        if (trackIdx < 0 || trackIdx >= trackList.getNumChildren())
            trackIdx = -1;
        if (trackIdx < 0) {
            for (int i = 0; i < trackList.getNumChildren(); ++i)
            {
                if (trackList.getChild(i).getProperty(IDs::name, "").toString().toStdString() == part.name)
                {
                    trackIdx = i;
                    break;
                }
            }
        }
        if (trackIdx < 0)
            trackIdx = addTrack(part.name, -1, -1, part.trackType);

        int clipId = addMidiClip(trackIdx, 0.0, totalBeats, part.name);
        if (clipId < 0)
            continue;
        for (const auto& n : part.notes)
            addNote(clipId, n.noteNumber, n.velocity, n.startBeat, n.durationBeats);

        result.trackIndices.push_back(trackIdx);
        result.clipIds.push_back(clipId);
        result.roleNames.push_back(part.name);
        result.noteCount += static_cast<int>(part.notes.size());
    }

    rebuildRoutingGraph();
    endTransaction();
    return result;
}

int AudioEngineCommands::mergeClips(const std::vector<int>& clipIds)
{
    if (clipIds.size() < 2) return -1;

    auto& model = engine_.getProjectModel();
    auto& um = model.getUndoManager();

    struct ClipInfo { juce::ValueTree tree; int trackIndex; };
    std::vector<ClipInfo> infos;
    infos.reserve(clipIds.size());

    for (int id : clipIds)
    {
        int trackIdx = -1;
        auto clip = findClipById(id, trackIdx);
        if (!clip.isValid()) return -1;
        if (clip.getProperty(IDs::clipType).toString() != "midi") return -1;
        infos.push_back({ clip, trackIdx });
    }

    int targetTrack = infos[0].trackIndex;
    for (const auto& info : infos)
    {
        if (info.trackIndex != targetTrack) return -1;
    }

    beginTransaction("Merge clips");

    double newStart = std::numeric_limits<double>::max();
    double newEnd = std::numeric_limits<double>::lowest();
    for (const auto& info : infos)
    {
        double start = static_cast<double>(info.tree.getProperty(IDs::startTime));
        double dur = static_cast<double>(info.tree.getProperty(IDs::duration));
        newStart = std::min(newStart, start);
        newEnd = std::max(newEnd, start + dur);
    }
    double newDuration = newEnd - newStart;

    auto mergedClip = model.createMidiClipEmpty(
        juce::String("Merged"), newStart, newDuration);
    int mergedId = static_cast<int>(mergedClip.getProperty(IDs::clipID, 0));

    auto mergedNoteList = mergedClip.getChildWithName(IDs::MIDI_NOTE_LIST);
    double bpm = engine_.getTransportManager().getBPM();
    for (const auto& info : infos)
    {
        double clipStart = static_cast<double>(info.tree.getProperty(IDs::startTime));
        double offsetSec = clipStart - newStart;
        double offsetBeats = HDAW::secondsToBeats(offsetSec, bpm);

        auto noteList = info.tree.getChildWithName(IDs::MIDI_NOTE_LIST);
        for (int n = 0; n < noteList.getNumChildren(); ++n)
        {
            auto srcNote = noteList.getChild(n);
            auto newNote = model.createMidiNote(
                static_cast<int>(srcNote.getProperty(IDs::noteNumber)),
                static_cast<float>(static_cast<double>(srcNote.getProperty(IDs::velocity))),
                static_cast<double>(srcNote.getProperty(IDs::startBeat)) + offsetBeats,
                static_cast<double>(srcNote.getProperty(IDs::durationBeats)));
            mergedNoteList.addChild(newNote, -1, &um);
        }
    }

    auto trackList = model.getTrackListTree();
    auto track = trackList.getChild(targetTrack);
    auto clipList = track.getChildWithName(IDs::CLIP_LIST);
    if (!clipList.isValid())
    {
        clipList = juce::ValueTree(IDs::CLIP_LIST);
        track.addChild(clipList, -1, &um);
    }
    clipList.addChild(mergedClip, -1, &um);

    for (int i = static_cast<int>(infos.size()) - 1; i >= 0; --i)
    {
        auto parent = infos[i].tree.getParent();
        if (parent.isValid())
            parent.removeChild(infos[i].tree, &um);
    }

    endTransaction();
    return mergedId;
}
