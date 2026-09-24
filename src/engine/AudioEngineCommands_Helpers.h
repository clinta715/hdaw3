// AudioEngineCommands_Helpers.h
#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include <utility>
#include <charconv>
#include <sstream>
#include <string>
#include <vector>

#include "../model/ProjectModel.h"   // IDs:: parentId / childIds / cellTrack / SONG_PLAN / CELLS

namespace HDAW {

// Returns {undoManager, track} or {um, {}} if out of range.
inline std::pair<juce::UndoManager*, juce::ValueTree> getTrack(
    juce::ValueTree trackList, int trackIndex, juce::UndoManager& um)
{
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren())
        return { &um, {} };
    return { &um, trackList.getChild(trackIndex) };
}

// Get or create a child list under parent.
inline juce::ValueTree getOrCreateChild(
    juce::ValueTree parent, const juce::Identifier& childId, juce::UndoManager& um)
{
    auto child = parent.getChildWithName(childId);
    if (!child.isValid())
    {
        child = juce::ValueTree(childId);
        parent.addChild(child, -1, &um);
    }
    return child;
}

// Set a property on a ValueTree with undo.
template<typename T>
inline void setProp(juce::ValueTree tree, const juce::Identifier& prop, T value, juce::UndoManager* um)
{
    if (tree.isValid())
        tree.setProperty(prop, value, um);
}

// Convert beats to seconds using the given BPM.
// The frontend sends clip positions/durations in beats; the audio engine
// stores them in seconds (processors, timeline, auto-stop all use seconds).
inline double beatsToSeconds(double beats, double bpm)
{
    return (bpm > 0) ? beats * 60.0 / bpm : beats;
}

// Convert seconds to beats using the given BPM.
// The reverse of beatsToSeconds — used when building snapshots for the frontend.
inline double secondsToBeats(double seconds, double bpm)
{
    return (bpm > 0) ? seconds * bpm / 60.0 : seconds;
}

// ─── Positional track-reference fixup (shift-aware removal/move) ───────────
// Tracks are POSITIONAL: folder parentId/childIds store TRACK_LIST indices and
// SONG_PLAN cells store cellTrack as a track index. Every splice that shifts
// indices (removeTrack / moveTrack, on BOTH the command path and the MCP inline
// path) must remap those durable references — otherwise the wrong track
// inherits mute/solo, hides in timelines, or receives a cell fill. ONE indexed
// walk per splice handles all three properties (lesson 6/30), never a
// per-property tree scan.

// old->new index map for removing `removed` from `count` tracks:
// removed -> -1 (the folder-less / cell-less sentinel moveTrackOutOfFolder and
// setCellRecipe use), everything above decrements.
inline std::vector<int> trackRemovalIndexMap(int count, int removed)
{
    std::vector<int> oldToNew(static_cast<size_t>(count > 0 ? count : 0));
    for (int i = 0; i < count; ++i)
        oldToNew[static_cast<size_t>(i)] = (i < removed) ? i
                                       : (i == removed) ? -1
                                                        : i - 1;
    return oldToNew;
}

// old->new index map for moving `from` to `to` (both in range, from != to).
// Mirrors the actual splice: remove `from`, re-insert it at
// (to > from) ? to - 1 : to in the post-removal list — so the map is derived
// from the resulting order, not a formula that can drift from the splice.
inline std::vector<int> trackMoveIndexMap(int count, int from, int to)
{
    std::vector<int> order;
    order.reserve(static_cast<size_t>(count > 0 ? count : 0));
    for (int i = 0; i < count; ++i)
        if (i != from) order.push_back(i);
    const int insertAt = (to > from) ? to - 1 : to;
    if (insertAt >= 0 && insertAt < static_cast<int>(order.size()))
        order.insert(order.begin() + insertAt, from);
    else
        order.push_back(from);
    std::vector<int> oldToNew(static_cast<size_t>(count > 0 ? count : 0));
    for (int pos = 0; pos < static_cast<int>(order.size()); ++pos)
        oldToNew[static_cast<size_t>(order[static_cast<size_t>(pos)])] = pos;
    return oldToNew;
}

// The fixup itself: remap parentId, every childIds entry (comma-separated
// ints — the exact vocabulary moveTrackIntoFolder/OutOfFolder write) and every
// SONG_PLAN cellTrack through oldToNew (mapped -1 = referenced track is gone
// -> sentinel -1; childIds entries pointing at it are dropped). References
// already outside the original range are left alone: consumers bounds-check
// them today (ReadModelImpl, fillOneCell) and re-writing foreign values would
// mask other corruption.
inline void remapTrackPositionalRefs(const juce::ValueTree& trackList,
                                      const juce::ValueTree& projectRoot,
                                      const std::vector<int>& oldToNew,
                                      juce::UndoManager* um)
{
    auto mapIdx = [&oldToNew](int v) -> int {
        if (v < 0 || v >= static_cast<int>(oldToNew.size())) return v;
        return oldToNew[static_cast<size_t>(v)];
    };

    for (int t = 0; t < trackList.getNumChildren(); ++t)
    {
        auto track = trackList.getChild(t);

        const int parent = static_cast<int>(track.getProperty(IDs::parentId, -1));
        if (parent >= 0)
        {
            const int mapped = mapIdx(parent);
            if (mapped != parent)
                track.setProperty(IDs::parentId, mapped < 0 ? -1 : mapped, um);
        }

        if (!track.hasProperty(IDs::childIds)) continue; // only folder tracks carry it
        const std::string csv = track.getProperty(IDs::childIds, "").toString().toStdString();
        std::string remapped;
        std::istringstream iss(csv);
        std::string token;
        bool first = true;
        bool changed = false;
        while (std::getline(iss, token, ','))
        {
            int val = 0;
            auto [ptr, ec] = std::from_chars(token.data(), token.data() + token.size(), val);
            if (ec != std::errc() || ptr != token.data() + token.size())
                continue; // not a bare int — dropped on rewrite (same as moveTrackIntoFolder)
            const int mapped = mapIdx(val);
            if (mapped < 0) { changed = true; continue; } // child was removed
            if (mapped != val) changed = true;
            if (!first) remapped += ",";
            remapped += std::to_string(mapped);
            first = false;
        }
        if (changed)
            track.setProperty(IDs::childIds, juce::String(remapped), um);
    }

    auto plan = projectRoot.getChildWithName(IDs::SONG_PLAN);
    if (!plan.isValid()) return;
    auto cells = plan.getChildWithName(IDs::CELLS);
    if (!cells.isValid()) return;
    for (int c = 0; c < cells.getNumChildren(); ++c)
    {
        auto node = cells.getChild(c);
        const int ref = static_cast<int>(node.getProperty(IDs::cellTrack, -1));
        if (ref < 0) continue;
        const int mapped = mapIdx(ref);
        if (mapped != ref)
            node.setProperty(IDs::cellTrack, mapped < 0 ? -1 : mapped, um);
    }
}

} // namespace HDAW
