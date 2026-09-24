#include "AudioEngineCommands.h"
#include "AudioEngineCommands_Helpers.h"
#include "AudioEngine.h"
#include "../model/ProjectModel.h"
#include "../common/MasterFxDefs.h"
#include <charconv>
#include <sstream>

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

    trackList.removeChild(trackIndex, &um);
    // Durable positional refs (folder parentId/childIds, SONG_PLAN cellTrack)
    // still hold PRE-removal indices — remap them in ONE indexed walk so
    // mute/solo cascades, timeline hiding and cell fills keep landing on the
    // right track (handoff 7 design A). Same undo manager as the splice, so
    // remove + fixup coalesce into one undo unit like the splice always did.
    HDAW::remapTrackPositionalRefs(trackList, model.getTree(),
                                    HDAW::trackRemovalIndexMap(count, trackIndex), &um);

    result.ok = true;
    result.removed = trackIndex;
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
    // project.moveTrack), so the splice, the range rule and the ref remap can
    // never drift apart again: an out-of-range index (or index == newIndex) is a
    // NO-OP, no clamping, and a forward move inserts before whatever sits at
    // newIndex today (never reaching the last slot).
    if (trackIndex < 0 || trackIndex >= count) return;
    if (newIndex < 0 || newIndex >= count) return;
    if (trackIndex == newIndex) return;
    auto track = trackList.getChild(trackIndex);
    const int to = newIndex;
    trackList.removeChild(trackIndex, &um);
    if (to > trackIndex) --newIndex;
    trackList.addChild(track, newIndex, &um);
    // A reorder shifts every index between the two positions: remap the same
    // durable refs through the move permutation (one indexed walk).
    HDAW::remapTrackPositionalRefs(trackList, model.getTree(),
                                    HDAW::trackMoveIndexMap(count, trackIndex, to), &um);
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

    // ── Durable positional refs: the copy must not inherit the source's ────
    // Folder membership is a PAIR of positional refs (folder childIds CSV <->
    // child parentId). A verbatim copy breaks it in two ways: a duplicated
    // FOLDER keeps the original's childIds, so two folders claim the same
    // children (mute/solo cascade double-counts, folder rendering lies); a
    // duplicated CHILD keeps parentId while the folder's CSV never learns about
    // it, so the two refs disagree (the cascade resolves through parentId,
    // folder semantics read childIds). The copy is APPENDED, so no existing
    // index shifts and no HDAW::remapTrackPositionalRefs walk is needed — just
    // these two writes, under the same &um as the insertion, so ONE undo
    // removes the copy AND reverts the ref change.

    // 1. A copy cannot claim children. createTrackValueTree never sets
    //    childIds (a track only acquires it in moveTrackIntoFolder), so the
    //    exact fresh-track value is the property ABSENT — not "". Guarded:
    //    removeProperty on a missing property is a no-op, and a no-op write
    //    must never churn the undo history.
    if (copy.hasProperty(IDs::childIds))
        copy.removeProperty(IDs::childIds, &um);

    const int newIdx = trackList.getNumChildren();   // the copy's landing index

    // 2. A copy that is itself a child re-links into its folder's CSV exactly
    //    once. parentId is still valid — the copy is appended, so the folder it
    //    points at has not moved. A parentId outside the list is foreign and is
    //    left alone, exactly like HDAW::remapTrackPositionalRefs documents
    //    ("references already outside the original range are left alone").
    const int parentIdx = static_cast<int>(copy.getProperty(IDs::parentId, -1));
    if (parentIdx >= 0 && parentIdx < newIdx)
    {
        auto folder = trackList.getChild(parentIdx);
        const std::string csv = folder.getProperty(IDs::childIds, "").toString().toStdString();
        bool alreadyLinked = false;
        std::istringstream iss(csv);
        std::string token;
        while (std::getline(iss, token, ','))
        {
            int val = 0;
            auto [ptr, ec] = std::from_chars(token.data(), token.data() + token.size(), val);
            if (ec == std::errc() && ptr == token.data() + token.size() && val == newIdx)
            {
                alreadyLinked = true;
                break;
            }
        }
        if (!alreadyLinked)
        {
            std::string updated = csv;
            if (!updated.empty()) updated += ",";
            updated += std::to_string(newIdx);
            folder.setProperty(IDs::childIds, juce::String(updated), &um);
        }
    }

    trackList.addChild(copy, newIdx, &um);
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

    // Remove from old parent if any
    int oldParent = track.getProperty(IDs::parentId, -1);
    if (oldParent >= 0 && oldParent < trackList.getNumChildren())
    {
        auto oldFolder = trackList.getChild(oldParent);
        auto childIds = oldFolder.getProperty(IDs::childIds, "").toString().toStdString();
        std::string newChildIds;
        std::istringstream iss(childIds);
        std::string token;
        bool first = true;
        while (std::getline(iss, token, ','))
        {
            int val = 0;
            auto [ptr, ec] = std::from_chars(token.data(), token.data() + token.size(), val);
            if (ec == std::errc() && ptr == token.data() + token.size() && val != trackIndex)
            {
                if (!first) newChildIds += ",";
                newChildIds += token;
                first = false;
            }
        }
        oldFolder.setProperty(IDs::childIds, juce::String(newChildIds), &project.getUndoManager());
    }

    // Add to new folder
    auto childIds = folder.getProperty(IDs::childIds, "").toString().toStdString();
    if (!childIds.empty()) childIds += ",";
    childIds += std::to_string(trackIndex);
    folder.setProperty(IDs::childIds, juce::String(childIds), &project.getUndoManager());
    track.setProperty(IDs::parentId, folderIndex, &project.getUndoManager());
}

void AudioEngineCommands::moveTrackOutOfFolder(int trackIndex)
{
    auto& project = engine_.getProjectModel();
    auto trackList = project.getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren()) return;

    auto track = trackList.getChild(trackIndex);
    int parentId = track.getProperty(IDs::parentId, -1);
    if (parentId < 0 || parentId >= trackList.getNumChildren()) return;

    auto parent = trackList.getChild(parentId);

    // Remove from parent's childIds
    auto childIds = parent.getProperty(IDs::childIds, "").toString().toStdString();
    std::string newChildIds;
    std::istringstream iss(childIds);
    std::string token;
    bool first = true;
    while (std::getline(iss, token, ','))
    {
        int val = 0;
        auto [ptr, ec] = std::from_chars(token.data(), token.data() + token.size(), val);
        if (ec == std::errc() && ptr == token.data() + token.size() && val != trackIndex)
        {
            if (!first) newChildIds += ",";
            newChildIds += token;
            first = false;
        }
    }
    parent.setProperty(IDs::childIds, juce::String(newChildIds), &project.getUndoManager());
    track.setProperty(IDs::parentId, -1, &project.getUndoManager());
}
