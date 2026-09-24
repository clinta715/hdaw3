#pragma once
// remove-track guard — shared by the MCP tool layer and the RPC router so that
// the `dryRun` preview text and the "track has N clips" refusal are produced in
// ONE place (AGENTS.md parity: the surfaces fail with identical text by
// construction, not by two hand-written formatters).
//
// The MCP tool `remove_track` always had this contract; RPC
// `project.removeTrack` did NOT (it destroyed the clips with no preview and no
// refusal), so a frontend/agent caller could silently lose every clip on a
// track. Both surfaces now run this guard before the shared
// ProjectCommands::removeTrack splice.
//
// Header-only (all inline), so no CMake source registration is needed. It
// includes the model header for the IDs:: identifiers — the tree schema is the
// single source of truth for property names.
//
// Text grammar (frozen — the twin test compares it byte for byte; MCP answers
// it as tool text, RPC as a bare JSON string on the payload, and the refusal as
// the -32602 message):
//
//   dryRun, no clips   would remove track 1 (Bass), 0 clips
//   dryRun, with clips would remove track 1 (Bass), 3 clips. Pass force:true to
//                      confirm deletion of clips.
//   no force, clips    track 1 (Bass) has 3 clips. Pass force:true to confirm
//                      deletion.                      (single line, no wrap)

#include "../model/ProjectModel.h"

#include <string>

namespace HDAW {

// What the guard needs to know about the track at `trackIndex`: whether it
// exists at all, its name (for the report) and how many clips the removal
// would destroy.
struct TrackRemovalGuard
{
    bool found = false;   // false => trackIndex names no track (out of range)
    std::string name;     // the TRACK node's name property ("" when not found)
    int clipCount = 0;    // CLIP_LIST children (0 when not found / no clip list)
};

// Inspect the track at `trackIndex` — the LIVE tree, the same node
// ProjectCommands::removeTrack will splice out, so the preview cannot disagree
// with what the removal would do.
inline TrackRemovalGuard inspectTrackForRemoval(const ProjectModel& model, int trackIndex)
{
    TrackRemovalGuard g;
    const auto trackList = model.getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren()) return g;
    const auto track = trackList.getChild(trackIndex);
    g.found = true;
    g.name = track.getProperty(IDs::name, "").toString().toStdString();
    const auto clipList = track.getChildWithName(IDs::CLIP_LIST);
    g.clipCount = clipList.isValid() ? clipList.getNumChildren() : 0;
    return g;
}

// The dryRun preview: what WOULD be removed, plus the force hint when clips are
// at stake. Built with std::string (not QString::arg) so a track name that
// happens to contain "%1" cannot be rewritten by a later substitution.
inline std::string trackRemovalDryRunText(int trackIndex, const TrackRemovalGuard& g)
{
    std::string info = "would remove track " + std::to_string(trackIndex) + " ("
                     + g.name + "), " + std::to_string(g.clipCount) + " clips";
    if (g.clipCount > 0)
        info += ". Pass force:true to confirm deletion of clips.";
    return info;
}

// The refusal for a clip-carrying track removed without `force`.
inline std::string trackRemovalRefusalText(int trackIndex, const TrackRemovalGuard& g)
{
    return "track " + std::to_string(trackIndex) + " (" + g.name + ") has "
         + std::to_string(g.clipCount) + " clips. Pass force:true to confirm deletion.";
}

} // namespace HDAW
