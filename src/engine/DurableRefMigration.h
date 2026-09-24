// DurableRefMigration.h — B3, slice S2: load-time move of the last three
// POSITIONAL durable track references onto stable track ids.
//
// Before B3 the project ValueTree stored folder membership and the song-plan
// cell target as TRACK_LIST indices (child's `parentId`, folder's `childIds`
// CSV, SONG_PLAN/CELLS/CELL `cellTrack`). Every splice that renumbered the list
// — removeTrack / moveTrack — had to patch those indices back up, which is what
// `HDAW::remapTrackPositionalRefs` + `trackRemovalIndexMap` /
// `trackMoveIndexMap` existed for. B3 replaces the STORAGE with stable ids
// (`parentTrackID`, `childTrackIDs`, `cellTrackID` — B1's `trackID` identities)
// so nothing positional is durable any more and the remap machinery is deleted.
//
// This header is the ONE-TIME, LOAD-ONLY conversion for files written before
// B3. It runs at the very end of ProjectSerializer::load, AFTER
// ProjectModel::scanAndSyncTrackIDs — the legacy indices can only be resolved
// once every track in the list carries its stable id (a pre-B1 file has none
// until the backfill runs). Placing it in migrateProjectTree would be WRONG:
// that hook runs BEFORE clearUndoHistory and BEFORE the id backfill, so it would
// convert indices to ids while the ids do not exist yet.
//
// It is idempotent (a node that already carries the new property is left alone)
// and never undoable (nullptr undo manager): it is a format upgrade performed on
// load, not an edit the user made.
//
// OUT OF SCOPE by decision: lane `paramID` / LFO `targetParamID` send targets
// (`2000 + sendIndex`) keep their positional encoding and their fixup walk —
// only the three track references above move.

#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include <vector>

#include "../common/TrackIdRefs.h"   // trackIDForIndex / parseIDList / writeIDList
#include "../model/ProjectModel.h"   // IDs::parentId|childIds|cellTrack (legacy)
                                     // and IDs::parentTrackID|childTrackIDs|cellTrackID (new)

namespace HDAW {

// Convert every legacy positional durable reference in `projectRoot` to its
// stable-id spelling and DROP the legacy property, so a saved file has exactly
// one vocabulary. Safe to call on an already-migrated tree (no-op) and on a tree
// with no legacy properties at all.
inline void migrateDurableRefsToIDs(const juce::ValueTree& projectRoot)
{
    if (!projectRoot.isValid())
        return;

    const auto trackList = projectRoot.getChildWithName(IDs::TRACK_LIST);

    // ONE indexed walk of TRACK_LIST (lesson 30): no per-property tree sweeps.
    // The ids are already backfilled by scanAndSyncTrackIDs before this runs, so
    // an index resolves against the same list this walk is visiting — every
    // folder link and every childIds token converts in a single pass.
    const int numTracks = trackList.isValid() ? trackList.getNumChildren() : 0;
    for (int t = 0; t < numTracks; ++t)
    {
        auto track = trackList.getChild(t);

        // (a) Folder membership, child side: parentId (index) -> parentTrackID (id).
        if (!track.hasProperty(IDs::parentTrackID) && track.hasProperty(IDs::parentId))
        {
            const int parentIndex = static_cast<int>(track.getProperty(IDs::parentId, -1));
            const int parentID = (parentIndex >= 0) ? trackIDForIndex(trackList, parentIndex) : -1;
            track.setProperty(IDs::parentTrackID, parentID, nullptr);
        }
        track.removeProperty(IDs::parentId, nullptr);

        // (b) Folder membership, folder side: childIds (CSV of indices) ->
        // childTrackIDs (CSV of ids). parseIDList keeps the legacy grammar
        // (strict ints, junk dropped); an index that names no track resolves to
        // -1 and is dropped, exactly as remapTrackPositionalRefs dropped a
        // removed child.
        if (!track.hasProperty(IDs::childTrackIDs) && track.hasProperty(IDs::childIds))
        {
            const auto childIndices = parseIDList(track, IDs::childIds);
            std::vector<int> childIDs;
            childIDs.reserve(childIndices.size());
            for (int idx : childIndices)
            {
                const int id = (idx >= 0) ? trackIDForIndex(trackList, idx) : -1;
                if (id > 0)
                    childIDs.push_back(id);
            }
            writeIDList(track, IDs::childTrackIDs, childIDs, nullptr);
        }
        track.removeProperty(IDs::childIds, nullptr);
    }

    // (c) Song plan cell target: cellTrack (index) -> cellTrackID (id). One
    // socket walk over CELLS — the cells are not tracks, so they are outside the
    // TRACK_LIST walk above.
    const auto plan = projectRoot.getChildWithName(IDs::SONG_PLAN);
    if (!plan.isValid())
        return;
    const auto cells = plan.getChildWithName(IDs::CELLS);
    if (!cells.isValid())
        return;
    for (int c = 0; c < cells.getNumChildren(); ++c)
    {
        auto cell = cells.getChild(c);
        if (!cell.hasProperty(IDs::cellTrackID) && cell.hasProperty(IDs::cellTrack))
        {
            const int ref = static_cast<int>(cell.getProperty(IDs::cellTrack, -1));
            const int id = (ref >= 0) ? trackIDForIndex(trackList, ref) : -1;
            cell.setProperty(IDs::cellTrackID, id, nullptr);
        }
        cell.removeProperty(IDs::cellTrack, nullptr);
    }
}

} // namespace HDAW
