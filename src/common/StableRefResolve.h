#pragma once
// Stable-id argument resolution — design B2, the half B1 left open.
//
// B1 shipped the identities: every TRACK node carries IDs::trackID and every
// SEND node IDs::sendID, minted at creation, backfilled on load
// (ProjectModel::scanAndSyncTrackIDs) and echoed on the wire (TrackSnapshot.
// trackID, SendSnapshot.sendID, the creation payloads, the send rows). Nothing
// ACCEPTED them as arguments, so a caller holding `trackID: 3` had to call a
// listing and search for it — and the positional answer could go stale between
// the two calls (an index moves under removeTrack/moveTrack/removeSend).
//
// B2 closes that ADDITIVELY: the positional argument keeps working byte for
// byte, and a second OPTIONAL argument carries the stable id.
//
//   entity        positional (unchanged)   stable (new, optional)
//   track         `trackId`                `trackID`
//   folder target `folderId`               `folderID`
//   send          `sendIndex`              `sendID`
//
// Two arguments, never one ambiguous number: `trackId: 2` and `trackID: 3` can
// be numerically identical and name DIFFERENT entities (track 2 is an index,
// trackID 3 is an identity), so a single key meaning "either" would be
// untestable and would eventually mutate the wrong track (Gate 9's ID/namespace
// hazard, one layer up).
//
// THE RULE (one sentence, mirrored in every tool description on both surfaces):
//   - a stable id > 0 WINS — it names its entity wherever that entity currently
//     sits, which is the whole point of holding an id;
//   - an UNKNOWN stable id is an ERROR naming it, never a silent fall back to
//     the positional index (that is how the wrong track gets mutated: the id
//     was as stale as the index would have been, and the caller is told);
//   - a positional argument that names a DIFFERENT entity than the id is an
//     ERROR naming both ("pick one"), never a pick;
//   - the positional argument alone behaves exactly as it did before B2;
//   - neither given → "<indexKey> required", today's message.
//
// Presence is the CALLER's call and it is decided with QJsonObject::contains(),
// NEVER by the value: an explicit `trackId: 0` is a real positional argument
// (track 0), which a value test ("0 means absent") would silently misread. An
// absent key is passed in as index = -1 / stableID = 0 — the two sentinels
// below — and `index < 0` in the result means "no positional argument".
//
// The rule lives HERE, in src/common (NO Qt — this directory takes none), so
// both surfaces run ONE implementation instead of two. Each surface only reads
// its own two keys off its own QJsonObject and hands the numbers in; every
// failure message is built here too, which is what makes the RPC and MCP
// failures byte-identical for free — the surface twin tests compare them
// verbatim (tests/unit/frontend/bus_send_rpc_test.cpp,
// tests/unit/frontend/add_fx_parity_test.cpp).
//
// NOT in scope (stated so it is a documented boundary, not a discovery trap):
// the fx / automation / plugin / clip tools keep taking the POSITIONAL trackId.
// An id-holding caller reaches a track's properties, its folder membership and
// its sends through B2; anything else still goes through the index.

#include "../model/ProjectModel.h"

#include <string>

namespace HDAW {

// The argument spellings of one entity. One place per entity, so the two
// surfaces cannot drift on a NAME (the vocabulary is part of the contract) nor
// on the message built out of it.
struct StableRefKeys
{
    const char* index;    // positional argument name ("trackId")
    const char* stable;   // stable-id argument name ("trackID")
};

// A folder target is a TRACK in TRACK_LIST (membership is the folder's childIds
// CSV plus the child's parentId), so it resolves through the very same lookup —
// only the argument spellings differ.
inline constexpr StableRefKeys kTrackRefKeys  {"trackId",   "trackID"};
inline constexpr StableRefKeys kFolderRefKeys {"folderId",  "folderID"};
inline constexpr StableRefKeys kSendRefKeys   {"sendIndex", "sendID"};

// "No positional argument was supplied" — the sentinel IN and OUT (it is never
// a valid TRACK_LIST / SEND_LIST position, and the commands it is handed to are
// always called with the resolved value, never with this one).
inline constexpr int kNoRef = -1;

struct StableRefResult
{
    bool ok = true;
    std::string error;      // the -32602 / tool-error text when !ok
    int index = kNoRef;     // the resolved POSITIONAL index
};

inline StableRefResult stableRefError(std::string message)
{
    return StableRefResult{ false, std::move(message), kNoRef };
}

// The child of `list` whose `idProperty` equals `stableID`, or -1. An invalid
// list (a track with no SEND_LIST, a project with no TRACK_LIST) has no children
// and reports -1, so no call site needs to special-case it.
inline int findChildByStableID(const juce::ValueTree& list,
                               const juce::Identifier& idProperty, int stableID)
{
    for (int i = 0; i < list.getNumChildren(); ++i)
        if (static_cast<int>(list.getChild(i).getProperty(idProperty, 0)) == stableID)
            return i;
    return -1;
}

// Resolve a track — or a folder target, with HDAW::kFolderRefKeys — argument.
// `index` is the positional value (-1 when its key was absent), `stableID` the
// stable id (0 when its key was absent).
//
// The positional path does NOT range-check `index`: an out-of-range index keeps
// today's outcome exactly (the command's own refusal, or the route's/tool's
// "track not found"), which is what makes B2 additive on the wire rather than
// only in the happy path. An out-of-range index WITH a stable id cannot occur —
// the id's own position is the answer.
inline StableRefResult resolveTrackRef(const juce::ValueTree& trackList, int index, int stableID,
                                       StableRefKeys keys = kTrackRefKeys)
{
    if (stableID > 0)
    {
        const int found = findChildByStableID(trackList, IDs::trackID, stableID);
        if (found < 0)
            return stableRefError(std::string("unknown ") + keys.stable + " "
                                  + std::to_string(stableID));
        if (index >= 0 && index != found)
            return stableRefError(std::string(keys.index) + " " + std::to_string(index)
                                  + " and " + keys.stable + " " + std::to_string(stableID)
                                  + " disagree");
        return StableRefResult{ true, {}, found };
    }
    if (index < 0) return stableRefError(std::string(keys.index) + " required");
    return StableRefResult{ true, {}, index };
}

// Resolve a send argument within the track `trackIndex` names (the track itself
// is resolved by resolveTrackRef first — a send is addressed as (track, send) on
// the wire, so the track argument stays part of the question).
//
// The search is confined to THAT track's SEND_LIST: a trackIndex that names no
// track cannot name one of its sends, so a stable sendID given against a bogus
// track index reports `unknown sendID N` rather than finding the send somewhere
// else in the project. (The sendID space is project-wide — ProjectModel::
// allocateSendID — but the ADDRESS is not: resolving across tracks would let a
// typo'd track argument mutate a send on a track the caller never named.)
inline StableRefResult resolveSendRef(const juce::ValueTree& trackList, int trackIndex,
                                      int index, int stableID, StableRefKeys keys = kSendRefKeys)
{
    if (stableID > 0)
    {
        const auto sendList = (trackIndex >= 0 && trackIndex < trackList.getNumChildren())
                                  ? trackList.getChild(trackIndex).getChildWithName(IDs::SEND_LIST)
                                  : juce::ValueTree();
        const int found = findChildByStableID(sendList, IDs::sendID, stableID);
        if (found < 0)
            return stableRefError(std::string("unknown ") + keys.stable + " "
                                  + std::to_string(stableID));
        if (index >= 0 && index != found)
            return stableRefError(std::string(keys.index) + " " + std::to_string(index)
                                  + " and " + keys.stable + " " + std::to_string(stableID)
                                  + " disagree");
        return StableRefResult{ true, {}, found };
    }
    if (index < 0) return stableRefError(std::string(keys.index) + " required");
    return StableRefResult{ true, {}, index };
}

} // namespace HDAW
