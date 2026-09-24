#pragma once
// MCP-side argument helpers — design B2, the stable-id half.
//
// The MCP twin of src/frontend/router/RouterHelpers.h's trackIndexArg /
// sendIndexArg: read the two keys of one entity off the tool's `arguments`
// object, hand the numbers to the ONE shared rule (common/StableRefResolve.h —
// header-only, Qt-free) and let that header word the failure. Both surfaces
// therefore resolve identically AND fail identically, which is what the twin
// tests assert byte for byte (tests/unit/frontend/bus_send_rpc_test.cpp,
// tests/unit/frontend/add_fx_parity_test.cpp) — the messages are not two
// hand-written copies, they are one string built once.
//
// Presence is decided with QJsonObject::contains(), never by the value: an
// explicit `trackId: 0` is a real positional argument (track 0), which a value
// test would silently misread as "absent" and then resolve to whatever the id
// happened to name. The key NAMES come from HDAW::k*RefKeys for the same
// reason: a spelling changed on one side alone fails the twin tests instead of
// resolving garbage.
//
// The tool schema is the other half of the contract: every tool that accepts
// these helpers lists `trackID` / `folderID` / `sendID` as optional properties
// AND no longer marks the positional key `required` (one of the two is required
// in fact, but which one is a HANDLER question — the schema validator runs
// first and cannot express "exactly one of").

#include <QJsonObject>

#include "../common/StableRefResolve.h"

#include <string>

namespace mcp {

// The two numbers one resolution needs, or the error for a non-numeric key.
// (The tool schemas type these properties as integers, so this only fires for a
// tool registered without its schema — the message mirrors the RPC half's.)
inline HDAW::StableRefResult refArgs(const QJsonObject& a, HDAW::StableRefKeys keys,
                                     int& index, int& stableID) {
    index = HDAW::kNoRef;
    stableID = 0;
    if (a.contains(keys.index)) {
        if (!a.value(keys.index).isDouble())
            return HDAW::stableRefError(std::string("missing or non-numeric param: ") + keys.index);
        index = static_cast<int>(a.value(keys.index).toDouble());
    }
    if (a.contains(keys.stable)) {
        if (!a.value(keys.stable).isDouble())
            return HDAW::stableRefError(std::string("missing or non-numeric param: ") + keys.stable);
        stableID = static_cast<int>(a.value(keys.stable).toDouble());
    }
    return {};
}

// `trackId` (index) / `trackID` (stable id) → the TRACK_LIST position to act on.
// A folder target resolves through the same call with HDAW::kFolderRefKeys.
// Returns false and fills `error` with the text the tool must report (verbatim:
// it is the message the RPC route reports for the same request).
inline bool trackIndexArg(const QJsonObject& a, const juce::ValueTree& trackList, int& out,
                          std::string& error, HDAW::StableRefKeys keys = HDAW::kTrackRefKeys) {
    int index = HDAW::kNoRef, stableID = 0;
    const auto read = refArgs(a, keys, index, stableID);
    const auto r = read.ok ? HDAW::resolveTrackRef(trackList, index, stableID, keys) : read;
    if (!r.ok) {
        error = r.error;
        return false;
    }
    out = r.index;
    return true;
}

// `sendIndex` / `sendID` → the SEND_LIST position inside the already-resolved
// track `trackIndex`.
inline bool sendIndexArg(const QJsonObject& a, const juce::ValueTree& trackList, int trackIndex,
                         int& out, std::string& error,
                         HDAW::StableRefKeys keys = HDAW::kSendRefKeys) {
    int index = HDAW::kNoRef, stableID = 0;
    const auto read = refArgs(a, keys, index, stableID);
    const auto r = read.ok ? HDAW::resolveSendRef(trackList, trackIndex, index, stableID, keys) : read;
    if (!r.ok) {
        error = r.error;
        return false;
    }
    out = r.index;
    return true;
}

// The rule as ONE sentence, for the tool descriptions (the schema IS the
// contract — an accepted-but-undocumented key is invisible to an agent). Kept
// here so every tool that gained B2's arguments words it identically; the
// surface vocabulary differs per entity, so the caller names its two keys.
inline QString stableRefRuleText(const char* stableKey, const char* positionalKey) {
    return QString("%1 is the stable id; if given it wins over %2, and a "
                   "disagreement is an error.")
        .arg(QString(stableKey), QString(positionalKey));
}

} // namespace mcp
