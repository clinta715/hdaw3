#pragma once
// `list_tracks` row shaping — the READ-side twin of the creation grammar in
// TrackJson.h ({trackId, trackID}).
//
//   {"id":2,"trackID":7,"name":"Bass","color":...,"volume":...,"pan":...,
//    "mute":false,"solo":false,"clipCount":4}
//
// `id` is the POSITIONAL index — the TRACK_LIST child position, the same number
// the tools' `trackId` argument means. It is an ADDRESS, not an identity: it
// shifts when a track above is removed or moved. Kept exactly as it was, for
// compatibility.
// `trackID` is the STABLE identity, read off the TRACK node's IDs::trackID
// property on EVERY call — never recomputed here, never cached (design B1:
// minted once by ProjectModel::allocateTrackID, 1-based, untouched by
// removeTrack/moveTrack).
//
// Header-only (all inline), so no CMake source registration is needed.
// MCP-only payload: there is no read.listTracks route. The parity ledger maps
// list_tracks to read.snapshot as a FAN-OUT note (read.snapshot carries its own
// TrackSnapshot.trackID) — the two shapes are not twins, so nothing here is
// mirrored.

#include "../model/ProjectModel.h"

#include <QJsonObject>
#include <QString>

namespace HDAW {

// One list_tracks entry. `position` is the caller's TRACK_LIST child index (it
// is passed in rather than recomputed so the row always describes the position
// the caller is iterating).
inline QJsonObject trackListRowJson(const juce::ValueTree& track, int position)
{
    return QJsonObject{
        { "id", position },
        { "trackID", static_cast<int>(track.getProperty(IDs::trackID, 0)) },
        { "name", QString::fromUtf8(track.getProperty(IDs::name, "").toString().toRawUTF8()) },
        { "color", static_cast<int>(track.getProperty(IDs::color)) },
        { "volume", static_cast<double>(track.getProperty(IDs::volume)) },
        { "pan", static_cast<double>(track.getProperty(IDs::pan)) },
        { "mute", static_cast<bool>(track.getProperty(IDs::isMuted)) },
        { "solo", static_cast<bool>(track.getProperty(IDs::isSoloed)) },
        { "clipCount", track.getChildWithName(IDs::CLIP_LIST).getNumChildren() }
    };
}

} // namespace HDAW
