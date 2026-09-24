#include "AudioEngineCommands.h"
#include "AudioEngineCommands_Helpers.h"
#include "../common/TrackIdRefs.h"
#include "AudioEngine.h"
#include "../model/ProjectModel.h"
#include "../common/MasterFxDefs.h"

// ─── ProjectCommands — Track operations ───────────────────────────

int AudioEngineCommands::addTrack(const std::string& name, int color, int parentBus, int trackType)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto trackList = engine_.getProjectModel().getTrackListTree();
    auto track = createTrackValueTree(name, color, parentBus, trackType);
    int idx = trackList.getNumChildren();
    trackList.addChild(track, idx, &um);
    return idx;
}

ProjectCommands::TrackRemovalResult AudioEngineCommands::removeTrack(int trackIndex)
{
    ProjectCommands::TrackRemovalResult result;
    auto& model = engine_.getProjectModel();
    auto& um = model.getUndoManager();
    auto trackList = model.getTrackListTree();
    const int count = trackList.getNumChildren();
    if (trackIndex < 0 || trackIndex >= count)
        return result;   // ok=false, removed=-1: NO tree change

    // The id of the track about to disappear. It MUST be pruned from every
    // durable ref, not merely left dangling: allocateTrackID() is
    // max(existing trackID) + 1, so removing the highest-id track makes the very
    // next minted id equal this one, and a dangling ref would silently re-point
    // at a brand-new track (a cell fill landing on the wrong track). Pre-B3 the
    // removal remap reset these to the sentinel — same outcome, now one walk.
    const int removedID = HDAW::trackIDForIndex(trackList, trackIndex);

    trackList.removeChild(trackIndex, &um);
    // B3: nothing POSITIONAL is stored any more — folder membership and cell
    // targets are STABLE track ids (src/common/TrackIdRefs.h) — so the splice
    // itself renumbers indices without invalidating a reference. What removal
    // does invalidate is the IDENTITY, and that is pruned below.

    if (removedID > 0)
    {
        // ONE indexed walk over the survivors, in the SAME undo unit: unfiled
        // children of a removed FOLDER go back to -1, and the dead id is dropped
        // from every folder's CSV (no-op when unlisted). O(list), one pass.
        for (int t = 0; t < trackList.getNumChildren(); ++t)
        {
            auto survivor = trackList.getChild(t);
            if (static_cast<int>(survivor.getProperty(IDs::parentTrackID, -1)) == removedID)
                survivor.setProperty(IDs::parentTrackID, -1, &um);
            HDAW::removeChildTrackID(survivor, removedID, &um);
        }

        // SONG_PLAN cells targeting the removed track go to the -1 sentinel
        // (fillOneCell refuses on it), never a reused id.
        auto plan = model.getTree().getChildWithName(IDs::SONG_PLAN);
        auto cells = plan.getChildWithName(IDs::CELLS);
        for (int c = 0; c < cells.getNumChildren(); ++c)
        {
            auto cell = cells.getChild(c);
            if (static_cast<int>(cell.getProperty(IDs::cellTrackID, -1)) == removedID)
                cell.setProperty(IDs::cellTrackID, -1, &um);
        }
    }

    result.ok = true;
    result.removed = trackIndex;
    // Advisory only (B3): indices above the splice DID renumber, but the
    // payload no longer describes a reference anyone must patch up — it is
    // reported for callers that mirror the list by position.
    for (int i = trackIndex + 1; i < count; ++i)
        result.shifted.emplace_back(i, i - 1);
    return result;
}

void AudioEngineCommands::moveTrack(int trackIndex, int newIndex)
{
    auto& model = engine_.getProjectModel();
    auto& um = model.getUndoManager();
    auto trackList = model.getTrackListTree();
    const int count = trackList.getNumChildren();
    // THE reorder contract — both surfaces route here (MCP move_track, RPC
    // project.moveTrack), so the splice and the range rule can never drift
    // apart again: an out-of-range index (or index == newIndex) is a NO-OP, no
    // clamping, and a forward move inserts before whatever sits at newIndex
    // today (never reaching the last slot).
    if (trackIndex < 0 || trackIndex >= count) return;
    if (newIndex < 0 || newIndex >= count) return;
    if (trackIndex == newIndex) return;
    auto track = trackList.getChild(trackIndex);
    const int to = newIndex;
    trackList.removeChild(trackIndex, &um);
    if (to > trackIndex) --newIndex;
    trackList.addChild(track, newIndex, &um);
    // B3: a reorder shifts indices but the durable references are STABLE track
    // ids, so none of them need remapping — the splice above is the whole job.
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

void AudioEngineCommands::setMasterGain(float gain)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    engine_.getProjectModel().getTree().setProperty(IDs::masterGain, static_cast<double>(gain), &um);
}

float AudioEngineCommands::setMasterFxParam(int slotIndex, int paramIndex, float value)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto masterFx = engine_.getProjectModel().getTree().getChildWithName(IDs::MASTER_FX);
    if (! masterFx.isValid()) return value;
    if (slotIndex < 0 || slotIndex >= masterFx.getNumChildren()) return value;
    auto slot = masterFx.getChild(slotIndex);
    const juce::String fxType = slot.getProperty(IDs::fxType, "").toString();
    // Gate 9 parity with set_internal_fx_param: an out-of-range index must
    // be a no-op, never a stray param_N property write.
    const auto& defs = HDAW::masterFxParamDefs(fxType);
    if (paramIndex < 0 || paramIndex >= static_cast<int>(defs.size())) return value;
    // Write-side clamp (lesson 23): the tree is re-read verbatim on every
    // rebuild/export, so out-of-range writes must never reach it.
    value = HDAW::clampMasterFxParam(fxType, paramIndex, value);
    slot.setProperty("param_" + juce::String(paramIndex), static_cast<double>(value), &um);
    return value;
}

void AudioEngineCommands::setMasterFxBypassed(int slotIndex, bool bypassed)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto masterFx = engine_.getProjectModel().getTree().getChildWithName(IDs::MASTER_FX);
    if (! masterFx.isValid()) return;
    if (slotIndex < 0 || slotIndex >= masterFx.getNumChildren()) return;
    masterFx.getChild(slotIndex).setProperty("bypassed", bypassed, &um);
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

    auto origName = copy.getProperty(IDs::name).toString();
    if (!origName.endsWith(" copy"))
        copy.setProperty(IDs::name, origName + " copy", &um);

    auto clipList = copy.getChildWithName(IDs::CLIP_LIST);
    for (int c = 0; c < clipList.getNumChildren(); ++c)
    {
        auto clip = clipList.getChild(c);
        clip.setProperty(IDs::clipID, model.allocateClipID(), nullptr);

        auto noteList = clip.getChildWithName(IDs::MIDI_NOTE_LIST);
        for (int n = 0; n < noteList.getNumChildren(); ++n)
        {
            auto note = noteList.getChild(n);
            note.setProperty(IDs::noteID, model.allocateNoteID(), nullptr);
        }
    }

    // ── Durable track refs: the copy must not inherit the source's ─────────
    // Folder membership is a PAIR of STABLE-id refs (folder childTrackIDs CSV
    // <-> child parentTrackID, src/common/TrackIdRefs.h). A verbatim copy breaks
    // it in two ways: a duplicated FOLDER keeps the original's childTrackIDs, so
    // two folders claim the same children (mute/solo cascade double-counts,
    // folder rendering lies); a duplicated CHILD keeps parentTrackID while the
    // folder's CSV never learns about it, so the two refs disagree (the cascade
    // resolves through parentTrackID, folder semantics read childTrackIDs).
    // Because these refs are IDENTITIES, not indices, the copy being APPENDED is
    // the whole reason no remap walk is needed — just the writes below, under
    // the same &um as the insertion, so ONE undo removes the copy AND reverts
    // the ref change.

    // 0. A copy is a NEW entity: mint its trackID BEFORE the fixup writes any
    //    reference to it (the folder CSV must point at the COPY's id, never the
    //    source's). The allocator runs before the caller appends the node, like
    //    createTrackValueTree: the new id is strictly greater than every id
    //    already in TRACK_LIST, and the copy is not in the list yet, so the id
    //    it inherited cannot be handed out a second time.
    const int newTrackID = model.allocateTrackID();
    copy.setProperty(IDs::trackID, newTrackID, &um);

    // 1. A copy cannot claim children. createTrackValueTree never sets
    //    childTrackIDs (a track only acquires it in moveTrackIntoFolder), so the
    //    exact fresh-track value is the property ABSENT — not "". Guarded:
    //    removeProperty on a missing property is a no-op, and a no-op write
    //    must never churn the undo history.
    if (copy.hasProperty(IDs::childTrackIDs))
        copy.removeProperty(IDs::childTrackIDs, &um);

    const int newIdx = trackList.getNumChildren();   // the copy's landing index

    // 2. A copy that is itself a child re-links into its folder's CSV exactly
    //    once, by the COPY's new id. parentTrackID still resolves — the copy is
    //    appended, so the folder it names has not moved. A parent that resolves
    //    at or after the copy's landing index is foreign and is left alone
    //    (today's `parentIdx < newIdx` guard, same meaning).
    const int parentID = static_cast<int>(copy.getProperty(IDs::parentTrackID, -1));
    const int parentIdx = HDAW::trackIndexForID(trackList, parentID);
    if (parentIdx >= 0 && parentIdx < newIdx)
    {
        auto folder = trackList.getChild(parentIdx);
        HDAW::addChildTrackID(folder, newTrackID, &um);
    }

    trackList.addChild(copy, newIdx, &um);

    // ── Stable ids: a copy is a NEW entity, so its SENDS must not inherit the
    // source's sendIDs — two live entities sharing one id makes every id-based
    // reference ambiguous and breaks the allocator's own invariant (Gate 9).
    // createCopy() copies properties, so this is a re-stamp, not an add. Its
    // sendIDs are allocated now, while the copy IS in the list, so each call
    // sees the inherited ids and steps past them (the max grows with every
    // stamp — a scan before insertion would return the same number twice). Same
    // &um as the insertion: ONE undo drops the copy AND its fresh ids.
    auto copiedSends = copy.getChildWithName(IDs::SEND_LIST);
    if (copiedSends.isValid())
        for (int s = 0; s < copiedSends.getNumChildren(); ++s)
            copiedSends.getChild(s).setProperty(IDs::sendID, model.allocateSendID(), &um);

    return newIdx;
}

void AudioEngineCommands::setTrackType(int trackIndex, int type)
{
    auto& project = engine_.getProjectModel();
    auto trackList = project.getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren()) return;
    auto track = trackList.getChild(trackIndex);
    track.setProperty(IDs::trackType, type, &project.getUndoManager());
}

void AudioEngineCommands::setTrackCollapsed(int trackIndex, bool collapsed)
{
    auto& project = engine_.getProjectModel();
    auto trackList = project.getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren()) return;
    auto track = trackList.getChild(trackIndex);
    track.setProperty(IDs::isCollapsed, collapsed, &project.getUndoManager());
}

void AudioEngineCommands::setTrackHidden(int trackIndex, bool hidden)
{
    auto& project = engine_.getProjectModel();
    auto trackList = project.getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren()) return;
    auto track = trackList.getChild(trackIndex);
    track.setProperty(IDs::isHidden, hidden, &project.getUndoManager());
}

void AudioEngineCommands::moveTrackIntoFolder(int trackIndex, int folderIndex)
{
    auto& project = engine_.getProjectModel();
    auto trackList = project.getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren()) return;
    if (folderIndex < 0 || folderIndex >= trackList.getNumChildren()) return;
    if (trackIndex == folderIndex) return;

    auto track = trackList.getChild(trackIndex);
    auto folder = trackList.getChild(folderIndex);

    // Check folder is actually a folder
    if (static_cast<int>(folder.getProperty(IDs::trackType, 0)) != 2) return;

    auto& um = project.getUndoManager();

    // B3: all three durable refs are STABLE ids, so the positions above are
    // resolved to ids exactly once, here, and every write below is by identity.
    const int trackID  = HDAW::trackIDForIndex(trackList, trackIndex);
    const int folderID = HDAW::trackIDForIndex(trackList, folderIndex);

    // Detach from the CURRENT folder, found through the child's own
    // parentTrackID — correct even if the child's position moved since it was
    // filed, which is the whole reason an index was the wrong thing to store.
    const int oldFolderIdx = HDAW::trackIndexForID(trackList,
        static_cast<int>(track.getProperty(IDs::parentTrackID, -1)));
    if (oldFolderIdx >= 0)
    {
        auto oldFolder = trackList.getChild(oldFolderIdx);
        HDAW::removeChildTrackID(oldFolder, trackID, &um);
    }

    // Add to new folder, both halves of the membership pair by id.
    HDAW::addChildTrackID(folder, trackID, &um);
    track.setProperty(IDs::parentTrackID, folderID, &um);
}

void AudioEngineCommands::moveTrackOutOfFolder(int trackIndex)
{
    auto& project = engine_.getProjectModel();
    auto trackList = project.getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren()) return;

    auto track = trackList.getChild(trackIndex);
    const int trackID = HDAW::trackIDForIndex(trackList, trackIndex);

    // The parent is named by the child's parentTrackID (B3); not resolving
    // means there is no parent to detach from.
    const int parentIdx = HDAW::trackIndexForID(trackList,
        static_cast<int>(track.getProperty(IDs::parentTrackID, -1)));
    if (parentIdx < 0) return;

    auto& um = project.getUndoManager();
    auto parent = trackList.getChild(parentIdx);
    HDAW::removeChildTrackID(parent, trackID, &um);
    track.setProperty(IDs::parentTrackID, -1, &um);
}
