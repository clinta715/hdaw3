#pragma once
// Durable track references as STABLE IDS — design B3
// (docs/plans/2026-09-23-durable-refs-to-stable-ids.md).
//
// Before B3 the project tree stored three durable references to a track as its
// TRACK_LIST INDEX: a folder carried IDs::childIds (CSV of indices), a child
// carried IDs::parentId, and a SONG_PLAN cell carried IDs::cellTrack. An index
// is a POSITION, not an identity: every splice (removeTrack / moveTrack, on the
// command path AND the MCP inline paths) renumbers everything above it, so each
// splice had to walk the tree and remap all three properties in lockstep — the
// positional-remap machinery B3 deleted — machinery whose only job was patching
// up the fact that the stored value was the wrong kind of thing. Miss one splice
// and the reference silently lands on a DIFFERENT track: the wrong track
// inherits mute/solo, hides in timelines, or receives a cell fill.
//
// B3 stores the IDENTITY instead (B1 minted the ids: every TRACK node carries
// IDs::trackID, tree-derived, 0 = unassigned until scanAndSyncTrackIDs fills it
// on load):
//   IDs::parentTrackID  int  the parent folder's trackID, -1 = folder-less
//   IDs::childTrackIDs  CSV  the child tracks' trackIDs (same comma grammar as
//                            the legacy childIds; only folder tracks, trackType
//                            == 2, carry it)
//   IDs::cellTrackID    int  the cell's target trackID, -1 = none
// With an identity stored, an index shift changes nothing, so removeTrack and
// moveTrack just splice and NO remap walk exists any more.
//
// The id -> POSITION lookup is HDAW::findChildByStableID (src/common/
// StableRefResolve.h) — the SAME primitive B2 resolves track/folder/send
// ARGUMENTS with, reused here so the storage layer and the argument layer can
// never disagree about where an id currently sits (one implementation, not two).
// The public/wire contract keeps the positional spelling it had
// (CellRecipe.trackId and TrackSnapshot.parentId stay TRACK_LIST indices); this
// header is where the conversion happens, at the storage boundary.
//
// The legacy IDs::parentId / IDs::childIds / IDs::cellTrack are NEVER written by
// live code after B3; the load-time migration (slice S2) is their only reader.

#include "../model/ProjectModel.h"
#include "StableRefResolve.h"

#include <algorithm>
#include <charconv>
#include <string>
#include <vector>

namespace HDAW {

// The trackID of the track at `index` in `trackList`, -1 when `index` is out of
// range. (-1 is the "no track" sentinel: an unassigned node carrying trackID 0
// is not addressable by id either, but in the live model ids are minted at
// creation and backfilled on load.)
inline int trackIDForIndex(const juce::ValueTree& trackList, int index)
{
    if (index < 0 || index >= trackList.getNumChildren()) return -1;
    return static_cast<int>(trackList.getChild(index).getProperty(IDs::trackID, -1));
}

// The index in `trackList` of the track whose trackID is `trackID`, or -1 when
// no such track exists. Delegates to the single id -> index primitive.
inline int trackIndexForID(const juce::ValueTree& trackList, int trackID)
{
    return findChildByStableID(trackList, IDs::trackID, trackID);
}

// Parse the comma-separated int list stored under `prop` on `node` — the EXACT
// legacy grammar of childIds (and the one moveTrackIntoFolder/OutOfFolder
// wrote): split on ',', keep a token only when std::from_chars consumes it
// ENTIRELY as an int (a stray or empty token is dropped on the floor, never
// coerced). An absent property is an empty list.
inline std::vector<int> parseIDList(const juce::ValueTree& node, const juce::Identifier& prop)
{
    std::vector<int> out;
    if (!node.hasProperty(prop)) return out;
    const std::string csv = node.getProperty(prop, "").toString().toStdString();
    const char* begin = csv.data();
    const char* const end = csv.data() + csv.size();
    while (begin <= end)
    {
        const char* comma = std::find(begin, end, ',');
        int val = 0;
        const auto [ptr, ec] = std::from_chars(begin, comma, val);
        if (ec == std::errc() && ptr == comma) out.push_back(val);
        if (comma == end) break;
        begin = comma + 1;
    }
    return out;
}

// Store `ids` under `prop` as a comma-separated list — the inverse of
// parseIDList. An empty list writes "" (what moveTrackOutOfFolder always wrote
// for the last child), never removes the property: the grammar's absent ==
// empty equivalence is a READ convenience, not a write convention.
inline void writeIDList(juce::ValueTree& node, const juce::Identifier& prop,
                        const std::vector<int>& ids, juce::UndoManager* um)
{
    if (!node.isValid()) return;
    std::string csv;
    for (size_t i = 0; i < ids.size(); ++i)
    {
        if (i != 0) csv += ',';
        csv += std::to_string(ids[i]);
    }
    node.setProperty(prop, juce::String(csv), um);
}

// Append `childTrackID` to `folder`'s IDs::childTrackIDs — the folder's half of
// the membership pair. Idempotent: a child already listed is not duplicated, and
// a no-change call writes nothing (no undo churn).
inline void addChildTrackID(juce::ValueTree& folder, int childTrackID, juce::UndoManager* um)
{
    if (!folder.isValid() || childTrackID < 0) return;
    auto ids = parseIDList(folder, IDs::childTrackIDs);
    if (std::find(ids.begin(), ids.end(), childTrackID) != ids.end()) return;
    ids.push_back(childTrackID);
    writeIDList(folder, IDs::childTrackIDs, ids, um);
}

// Drop every entry equal to `childTrackID` from `folder`'s IDs::childTrackIDs.
// Not a member -> no write at all (a no-op write must never churn undo history).
inline void removeChildTrackID(juce::ValueTree& folder, int childTrackID, juce::UndoManager* um)
{
    if (!folder.isValid()) return;
    auto ids = parseIDList(folder, IDs::childTrackIDs);
    const auto newEnd = std::remove(ids.begin(), ids.end(), childTrackID);
    if (newEnd == ids.end()) return;
    ids.erase(newEnd, ids.end());
    writeIDList(folder, IDs::childTrackIDs, ids, um);
}

} // namespace HDAW
