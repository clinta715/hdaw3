#pragma once
// Track-mutation payload shaping — shared by the MCP tool layer and the RPC
// router so that the creation-shaped answers are THE SAME payload by
// construction (AGENTS.md parity: identical by construction, not by two
// hand-written serializers).
//
// Header-only (all inline), so no CMake source registration is needed.
//
// Payload grammar (frozen — the surface twin tests compare it value for value):
//
//   shapeTrackCreatedJson   {"trackId":3,"routed":1,"trackID":4}
//       `trackId` is the new track's index in TRACK_LIST — the same number the
//       MCP tools' `trackId` argument means (an INDEX, not an identity: it
//       shifts when a track above is removed). `trackID` is the new track's
//       STABLE id (design B1): minted at creation from the tree (max existing
//       id + 1), stored on the TRACK node, untouched by removeTrack/moveTrack.
//       0 = "no track was created" (the shaper is also called with a failure
//       sentinel by duplicateTrack) — a real created track never reports 0.
//       `routed` is 1 when that index names a track inside TRACK_LIST right now,
//       0 otherwise.
//       Emitted by add_track / duplicate_track (and add_track_with_fx, which
//       appends "fxType" — see AddTrackWithFx.h).
//
// NOT covered here: the removal payload
//   {"ok":true,"removed":2,"shifted":[{"from":3,"to":2}]}
// — that shape is owned by the command layer
// (ProjectCommands::TrackRemovalResult), which both surfaces serialize.

#include <juce_core/juce_core.h>

#include <string>

namespace HDAW {

// 1 when `trackId` names a track that exists in a list of `trackCount` tracks.
// The ONE routed rule: a negative index (duplicateTrack's failure sentinel) or
// an index past the end reports 0, everything else 1.
inline int trackRoutedFlag(int trackId, int trackCount)
{
    return (trackId >= 0 && trackId < trackCount) ? 1 : 0;
}

// Compact single-line JSON (the house style for tool payloads: list_tracks,
// list_clips, list_buses ... all emit one line). `trackCount` is the track
// count AFTER the mutation the payload describes; `trackID` is the created
// track's stable id, read off the TRACK node the caller just inserted (0 when
// no track was created).
inline std::string shapeTrackCreatedJson(int trackId, int trackCount, int trackID)
{
    juce::DynamicObject::Ptr o = new juce::DynamicObject();
    o->setProperty("trackId", trackId);
    o->setProperty("trackID", trackID);
    o->setProperty("routed", trackRoutedFlag(trackId, trackCount));
    return juce::JSON::toString(juce::var(o.get()), true).toStdString();
}

} // namespace HDAW
