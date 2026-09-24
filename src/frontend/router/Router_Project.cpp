// ChainLibrary.h MUST stay the first include: Qt defines a `slots` macro
// (qobjectdefs.h, via QSettings/QObject below) that would otherwise rewrite
// the HDAW::ChainPreset::slots member. This TU uses no Qt signals/slots
// keywords (verified by grep), so the macro is dropped after the includes.
#include "../../engine/ChainLibrary.h"
#include "Router_Project.h"
#include "RouterHelpers.h"

#include "../../common/ProjectCommands.h"
// Shared bodies for the automation/master-FX routes below — the SAME entry
// points the MCP tools call (automation_preset / apply_movement_plan /
// set_master_fx_param / set_master_fx_bypassed), so both surfaces cannot drift:
//   - AutomationPresetRequest.h — automation_preset parse + apply + payload
//   - MasterFxAccess.h          — set_master_fx_param / _bypassed resolve + write
//   - MovementPlanJson.h        — apply_movement_plan parse + apply
#include "../../common/AutomationPresetRequest.h"
#include "../../common/MasterFxAccess.h"
#include "../../common/MovementPlanJson.h"
// The two engine-context routes below (removeTrack's dryRun/force guard,
// addTrackWithFx's composite) run the SAME shared bodies the MCP tools run, so
// both surfaces agree by construction:
//   - TrackRemoveGuard.h  — remove_track / project.removeTrack guard + texts
//   - AddTrackWithFx.h    — the composite (pluginId gate + created-track shape)
//   - TrackJson.h         — the {trackId, routed} creation payload
#include "../../common/AddTrackWithFx.h"
#include "../../common/TrackJson.h"
#include "../../common/TrackRemoveGuard.h"
#include "../../engine/AudioEngine.h"
#include "../../common/SettingsKeys.h"
#include "../../engine/PhraseGenerator.h"
#include "../../engine/ArrangementGenerator.h"
#include "../../engine/EnvelopeGenerator.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSettings>
#include <QString>

#include <algorithm>
#include <string>
#include <vector>

// See note at the top of this file: drop Qt's `slots` macro so
// HDAW::ChainPreset::slots stays a plain member below.
#ifdef slots
#undef slots
#endif

using namespace frontend::router_helpers;

namespace frontend {

DispatchResult dispatchProject(ProjectCommands& c, const juce::ValueTree& trackList,
                               const QString& m, const QJsonValue& params) {
    const auto o = paramsObject(params);

    // --- Tracks ---
    if (m == "addTrack") {
        std::string name; if (!requireString(o, "name", name, nullptr)) name = "Track";
        int color = optInt(o, "color", -1, nullptr);
        int parentBus = optInt(o, "parentBus", -1, nullptr);
        int trackType = optInt(o, "trackType", 0, nullptr);
        return { false, c.addTrack(name, color, parentBus, trackType) };
    }
    // removeTrack lives in dispatchRemoveTrack (below): its dryRun/force guard
    // needs the ProjectModel, not just the command interface, and FrontendRouter
    // routes the method there before this dispatcher (the addFxSlot precedent).
    // The shift payload it shapes is unchanged:
    //   {"ok":true,"removed":<oldIndex>,"shifted":[{"from":N,"to":N-1},...]}
    // (AGENTS.md parity; the command owns the shape — see
    // common/ProjectCommands.h TrackRemovalResult).
    // moveTrack: the command below IS the single reorder contract — MCP
    // move_track now routes through it too (it used to splice inline at the
    // un-decremented index, so forward moves disagreed with this route's order
    // and ref remap). `newIndex` indexes the CURRENT order; out-of-range or
    // equal to `trackId` is a no-op, exactly like the command. No payload:
    // MCP's twin answers text "ok" (AGENTS.md parity).
    // B2: a track argument is `trackId` (TRACK_LIST index, unchanged) OR the
    // stable `trackID` (design B1) — resolved by the ONE shared rule in
    // common/StableRefResolve.h, whose text IS the -32602 message, so this route
    // and its MCP twin cannot disagree. Everything else in the line is
    // untouched: `newIndex` stays a POSITIONAL destination (it is not an entity),
    // so a forward move never takes an id.
    if (m == "moveTrack")       { int i, n; DispatchResult err; if (!trackIndexArg(o, trackList, i, &err)) return err; if (!requireInt(o, "newIndex", n, nullptr)) return makeError(-32602, "trackId and newIndex required"); c.moveTrack(i, n); return { false, QJsonValue::Null }; }
    // duplicateTrack answers the SAME object as MCP duplicate_track
    // (common/TrackJson.h): {trackId, routed}. It used to answer a bare int
    // while the tool answered "trackId=N routed=1" — two shapes for one
    // operation, which no consumer could compare. trackId is the new index (the
    // copy is appended last), routed is 1 when that index names a track.
    if (m == "duplicateTrack")  {
        int i; DispatchResult err;
        // B2: the SOURCE is named by `trackId` or the stable `trackID`; the
        // payload's `trackId` is still the COPY's index (a new position).
        if (!trackIndexArg(o, trackList, i, &err)) return err;
        const int newIdx = c.duplicateTrack(i);
        // trackID (design B1): the COPY's own stable id, read through the command
        // interface (this dispatcher has no engine handle — the same reason
        // getTrackCount is a command query). duplicateTrack re-stamps the inherited
        // id, so this is a new identity rather than the source's.
        const int newTrackID = c.getTrackID(newIdx);
        return { false, QJsonDocument::fromJson(QString::fromStdString(
                     HDAW::shapeTrackCreatedJson(newIdx, c.getTrackCount(),
                                                 newTrackID)).toUtf8()).object() };
    }
    if (m == "setTrackName")    { int i; DispatchResult err; if (!trackIndexArg(o, trackList, i, &err)) return err; std::string s; if (!requireString(o, "name", s, nullptr)) return makeError(-32602, "trackId and name required"); c.setTrackName(i, s); return { false, QJsonValue::Null }; }
    if (m == "setTrackColor")   { int i, color; DispatchResult err; if (!trackIndexArg(o, trackList, i, &err) || !requireInt(o, "color", color, nullptr)) return err.isError ? err : makeError(-32602, "trackId and color required"); c.setTrackColor(i, color); return { false, QJsonValue::Null }; }
    if (m == "setTrackVolume")  { int i; float v; DispatchResult err; if (!trackIndexArg(o, trackList, i, &err) || !requireFloat(o, "volume", v, nullptr)) return err.isError ? err : makeError(-32602, "trackId and volume required"); c.setTrackVolume(i, v); return { false, QJsonValue::Null }; }
    if (m == "setMasterGain")   { float v;          if (!requireFloat(o, "gain", v, nullptr)) return makeError(-32602, "gain required"); c.setMasterGain(v); return { false, QJsonValue::Null }; }
    // --- MASTER-bus FX (MASTER_FX) ---
    // dispatchProject receives only the TRACK_LIST child; MASTER_FX is its
    // sibling under the same project root (the node
    // AudioEngineCommands::setMasterFxParam reads), so the hop is the full tree.
    // Both routes call the shared entry points in common/MasterFxAccess.h — the
    // same ones src/mcp/McpTools_FxSlot.cpp calls — so their text cannot drift
    // (AGENTS.md parity: identical by construction, not by discipline).
    if (m == "setMasterFxParam" || m == "setMasterFxBypassed") {
        const juce::ValueTree masterFx = trackList.getParent().getChildWithName(IDs::MASTER_FX);
        bool ok = false;
        const QString text = (m == "setMasterFxParam")
            ? HDAW::setMasterFxParamToolText(c, masterFx, o, &ok)
            : HDAW::setMasterFxBypassedToolText(c, masterFx, o, &ok);
        if (! ok) return makeError(-32602, text);
        // The tool's success text IS the payload: it reports the value the command
        // actually wrote (clamped — lesson 23), which a Null payload would drop.
        return { false, QJsonValue(text) };
    }
    if (m == "setTrackPan")     { int i; float v; DispatchResult err; if (!trackIndexArg(o, trackList, i, &err) || !requireFloat(o, "pan", v, nullptr))     return err.isError ? err : makeError(-32602, "trackId and pan required"); c.setTrackPan(i, v); return { false, QJsonValue::Null }; }
    if (m == "setTrackMuted")   { int i; bool b; DispatchResult err; if (!trackIndexArg(o, trackList, i, &err) || !requireBool(o, "muted", b, nullptr))   return err.isError ? err : makeError(-32602, "trackId and muted required"); c.setTrackMuted(i, b); return { false, QJsonValue::Null }; }
    if (m == "setTrackSoloed")  { int i; bool b; DispatchResult err; if (!trackIndexArg(o, trackList, i, &err) || !requireBool(o, "soloed", b, nullptr))  return err.isError ? err : makeError(-32602, "trackId and soloed required"); c.setTrackSoloed(i, b); return { false, QJsonValue::Null }; }
    if (m == "setTrackArmed")   { int i; bool b; DispatchResult err; if (!trackIndexArg(o, trackList, i, &err) || !requireBool(o, "armed", b, nullptr))   return err.isError ? err : makeError(-32602, "trackId and armed required"); c.setTrackArmed(i, b); return { false, QJsonValue::Null }; }
    // `inputMonitor` / `midiChannel` (not `monitor` / `channel`) are the MCP
    // set_track property names — AGENTS.md: argument names are part of the
    // contract, so the route mirrors the tool (renamed 2026-09-23, no alias).
    if (m == "setTrackInputMonitor") { int i; bool b; DispatchResult err; if (!trackIndexArg(o, trackList, i, &err) || !requireBool(o, "inputMonitor", b, nullptr)) return err.isError ? err : makeError(-32602, "trackId and inputMonitor required"); c.setTrackInputMonitor(i, b); return { false, QJsonValue::Null }; }
    if (m == "setTrackHeight")  { int i, h; DispatchResult err; if (!trackIndexArg(o, trackList, i, &err) || !requireInt(o, "height", h, nullptr)) return err.isError ? err : makeError(-32602, "trackId and height required"); c.setTrackHeight(i, h); return { false, QJsonValue::Null }; }
    if (m == "setTrackMidiChannel") { int i, ch; DispatchResult err; if (!trackIndexArg(o, trackList, i, &err) || !requireInt(o, "midiChannel", ch, nullptr)) return err.isError ? err : makeError(-32602, "trackId and midiChannel required"); c.setTrackMidiChannel(i, ch); return { false, QJsonValue::Null }; }
    if (m == "setTrackType") { int i, t; DispatchResult err; if (!trackIndexArg(o, trackList, i, &err) || !requireInt(o, "trackType", t, nullptr)) return err.isError ? err : makeError(-32602, "trackId and trackType required"); c.setTrackType(i, t); return { false, QJsonValue::Null }; }
    if (m == "setTrackCollapsed") { int i; bool b; DispatchResult err; if (!trackIndexArg(o, trackList, i, &err) || !requireBool(o, "collapsed", b, nullptr)) return err.isError ? err : makeError(-32602, "trackId and collapsed required"); c.setTrackCollapsed(i, b); return { false, QJsonValue::Null }; }
    if (m == "setTrackHidden") { int i; bool b; DispatchResult err; if (!trackIndexArg(o, trackList, i, &err) || !requireBool(o, "hidden", b, nullptr)) return err.isError ? err : makeError(-32602, "trackId and hidden required"); c.setTrackHidden(i, b); return { false, QJsonValue::Null }; }
    // Folder moves: `trackId` / `folderId` are the MCP tools' argument names
    // (move_track_into_folder / move_track_out_of_folder in McpTools_Track.cpp),
    // which call these SAME commands — the route's old `folderIndex` spelling
    // was retired in the same pass (hard rename, no alias).
    // B2: `folderId` resolves through the SAME track lookup with the folder
    // spellings (HDAW::kFolderRefKeys): a folder is a TRACK in TRACK_LIST, so
    // `folderID` is its stable id. `trackId`/`trackID` name the track being
    // moved. Resolution order (track, then folder) is the argument order every
    // caller already writes, so the reported failure is the FIRST thing missing.
    if (m == "moveTrackIntoFolder") { int i, f; DispatchResult err; if (!trackIndexArg(o, trackList, i, &err)) return err; if (!trackIndexArg(o, trackList, f, &err, HDAW::kFolderRefKeys)) return err; c.moveTrackIntoFolder(i, f); return { false, QJsonValue::Null }; }
    if (m == "moveTrackOutOfFolder") { int i; DispatchResult err; if (!trackIndexArg(o, trackList, i, &err)) return err; c.moveTrackOutOfFolder(i); return { false, QJsonValue::Null }; }

    // --- Send operations ---
    // B2: the track half is `trackId`/`trackID`, the send half `sendIndex`/`sendID`
    // — both through the shared resolver, so the send mutators address a send by
    // its identity after a splice renumbered it (the property B1 gave sendID).
    if (m == "setTrackSendLevel")    { int i, si; float v; DispatchResult err; if (!trackIndexArg(o, trackList, i, &err) || !sendIndexArg(o, trackList, i, si, &err) || !requireFloat(o, "level", v, nullptr)) return err.isError ? err : makeError(-32602, "trackId, sendIndex, level required"); c.setTrackSendLevel(i, si, v); return { false, QJsonValue::Null }; }
    if (m == "setTrackSendMode")     { int i, si; bool b; DispatchResult err; if (!trackIndexArg(o, trackList, i, &err) || !sendIndexArg(o, trackList, i, si, &err) || !requireBool(o, "isPreFader", b, nullptr)) return err.isError ? err : makeError(-32602, "trackId, sendIndex, isPreFader required"); c.setTrackSendMode(i, si, b); return { false, QJsonValue::Null }; }
    if (m == "setTrackSendBypassed") { int i, si; bool b; DispatchResult err; if (!trackIndexArg(o, trackList, i, &err) || !sendIndexArg(o, trackList, i, si, &err) || !requireBool(o, "bypassed", b, nullptr)) return err.isError ? err : makeError(-32602, "trackId, sendIndex, bypassed required"); c.setTrackSendBypassed(i, si, b); return { false, QJsonValue::Null }; }

    // --- Bus / send creation (docs/plans/2026-09-22-bus-send-surface.md, slice B) ---
    // The routes above only shape sends that already exist. Argument names mirror the
    // MCP tools (add_bus / remove_bus / add_send / remove_send in McpTools_Send.cpp)
    // exactly, and every payload below is byte-identical to what those tools return —
    // the twin test in tests/unit/frontend/bus_send_rpc_test.cpp asserts both.
    if (m == "addBus") {
        std::string busType;
        if (!requireString(o, "busType", busType, nullptr)) return makeError(-32602, "busType required");
        const std::string name = optString(o, "name", "");
        const std::string fxType = optString(o, "fxType", "");
        const int busTarget = optInt<int>(o, "busTarget", 0, nullptr);
        auto r = c.createBus(busType, name, fxType, busTarget);
        if (!r.ok) return makeError(-32602, QString::fromStdString(r.error));
        return { false, QJsonObject{{ "ok", true }, { "busID", r.busID }} };
    }
    if (m == "removeBus") {
        int id; if (!requireInt(o, "busID", id, nullptr)) return makeError(-32602, "busID required");
        std::string error;
        if (!c.removeBus(id, error)) return makeError(-32602, QString::fromStdString(error));
        return { false, QStringLiteral("ok") };
    }
    // The MCP twin (set_bus_target in McpTools_Send.cpp) takes exactly these keys
    // (`busID` / `busTarget`) and calls the SAME command, so an accepted
    // re-parent is {"ok":true} on both and a refusal carries the command's text
    // verbatim on both (tests/unit/frontend/bus_send_rpc_test.cpp drives both).
    if (m == "setBusTarget") {
        int id, target;
        if (!requireInt(o, "busID", id, nullptr) || !requireInt(o, "busTarget", target, nullptr))
            return makeError(-32602, "busID and busTarget required");
        auto r = c.setBusTarget(id, target);
        if (!r.ok) return makeError(-32602, QString::fromStdString(r.error));
        return { false, QJsonObject{{ "ok", true }} };
    }
    if (m == "addSend") {
        int i, busTarget; DispatchResult err;
        // B2: the source track may be named by `trackID` instead of `trackId`.
        // `busTarget` stays a busID (it already had one stable spelling).
        if (!trackIndexArg(o, trackList, i, &err)) return err;
        if (!requireInt(o, "busTarget", busTarget, nullptr))
            return makeError(-32602, "trackId and busTarget required");
        const float level = optFloat(o, "level", 1.0f, nullptr);
        const bool pre = optBool(o, "isPreFader", false, nullptr);
        auto r = c.createSend(i, busTarget, level, pre);
        if (!r.ok) return makeError(-32602, QString::fromStdString(r.error));
        return { false, QJsonObject{{ "ok", true }, { "sendIndex", r.sendIndex }} };
    }
    if (m == "removeSend") {
        int i, si; DispatchResult err;
        // B2: the track by `trackId`/`trackID`, the send by `sendIndex`/`sendID`.
        // `removed` below is the RESOLVED index — the position the splice took
        // out, which is what the shift report describes.
        if (!trackIndexArg(o, trackList, i, &err)) return err;
        if (!sendIndexArg(o, trackList, i, si, &err)) return err;
        std::string error;
        std::vector<std::pair<int, int>> shifted;
        if (!c.removeSend(i, si, error, &shifted)) return makeError(-32602, QString::fromStdString(error));
        // Was the bare string "ok"; extended ADDITIVELY on both surfaces
        // (byte-identical to MCP remove_send — bus_send_rpc_test.cpp twins it):
        //   {"ok":true,"removed":<sendIndex>,"shifted":[{"from":N,"to":N-1},...]}
        // `shifted` is [] when the last send was removed.
        QJsonArray shiftedArr;
        for (const auto& p : shifted)
            shiftedArr.append(QJsonObject{ { "from", p.first }, { "to", p.second } });
        return { false, QJsonObject{ { "ok", true }, { "removed", si },
                                     { "shifted", shiftedArr } } };
    }

    // --- Bus FX params (docs/plans/2026-09-22-bus-fx-params.md, slice C) ---
    // The MCP twin (set_bus_fx_param in McpTools_Send.cpp) takes exactly these keys
    // (`busID` / `paramIndex` / `value`) and calls the SAME command, so a success is
    // "ok" on both and a refusal carries the command's text verbatim on both
    // (tests/unit/frontend/bus_send_rpc_test.cpp drives both surfaces with one object).
    if (m == "setBusFxParam") {
        int id, paramIndex; float v;
        if (!requireInt(o, "busID", id, nullptr) || !requireInt(o, "paramIndex", paramIndex, nullptr)
            || !requireFloat(o, "value", v, nullptr))
            return makeError(-32602, "busID, paramIndex, value required");
        std::string error;
        if (!c.setBusFxParam(id, paramIndex, v, error))
            return makeError(-32602, QString::fromStdString(error));
        return { false, QStringLiteral("ok") };
    }

    // Session methods live in dispatchSession below; they are dispatched via
    // the method::Session namespace branch in dispatch().

    // --- Clips ---
    if (m == "addAudioClip") {
        int i; double start, dur; std::string src, name;
        if (!requireInt(o, "trackIndex", i, nullptr) || !requireDouble(o, "start", start, nullptr) || !requireDouble(o, "duration", dur, nullptr) || !requireString(o, "sourceFile", src, nullptr) || !requireString(o, "name", name, nullptr))
            return makeError(-32602, "trackIndex, start, duration, sourceFile, name required");
        return { false, c.addAudioClip(i, start, dur, src, name) };
    }
    if (m == "addMidiClip") {
        int i; double start, dur; std::string name;
        if (!requireInt(o, "trackIndex", i, nullptr) || !requireDouble(o, "start", start, nullptr) || !requireDouble(o, "duration", dur, nullptr) || !requireString(o, "name", name, nullptr))
            return makeError(-32602, "trackIndex, start, duration, name required");
        return { false, c.addMidiClip(i, start, dur, name) };
    }
    if (m == "removeClip")      { int i; if (!requireInt(o, "clipId", i, nullptr)) return makeError(-32602, "clipId required"); c.removeClip(i); return { false, QJsonValue::Null }; }
    if (m == "moveClip")        { int i, t; double s; if (!requireInt(o, "clipId", i, nullptr) || !requireInt(o, "newTrackIndex", t, nullptr) || !requireDouble(o, "newStart", s, nullptr)) return makeError(-32602, "clipId, newTrackIndex, newStart required"); c.moveClip(i, t, s); return { false, QJsonValue::Null }; }
    if (m == "moveClipWithOverlap") { int i, t; double s; if (!requireInt(o, "clipId", i, nullptr) || !requireInt(o, "newTrackIndex", t, nullptr) || !requireDouble(o, "newStart", s, nullptr)) return makeError(-32602, "clipId, newTrackIndex, newStart required"); c.moveClipWithOverlap(i, t, s); return { false, QJsonValue::Null }; }
    if (m == "duplicateClip")   { int i; if (!requireInt(o, "clipId", i, nullptr)) return makeError(-32602, "clipId required"); return { false, c.duplicateClip(i) }; }
    if (m == "duplicateClipTo") { int i, t; double s; if (!requireInt(o, "clipId", i, nullptr) || !requireDouble(o, "newStart", s, nullptr) || !requireInt(o, "newTrackIndex", t, nullptr)) return makeError(-32602, "clipId, newStart, newTrackIndex required"); return { false, c.duplicateClipTo(i, s, t) }; }
    if (m == "createGhostClip") { int i, t; double s; if (!requireInt(o, "sourceClipId", i, nullptr) || !requireDouble(o, "newStart", s, nullptr) || !requireInt(o, "newTrackIndex", t, nullptr)) return makeError(-32602, "sourceClipId, newStart, newTrackIndex required"); return { false, c.createGhostClip(i, s, t) }; }
    if (m == "mergeClips") {
        auto idsArr = o.value("clipIds");
        if (!idsArr.isArray()) return makeError(-32602, "clipIds array required");
        std::vector<int> ids;
        for (const auto& e : idsArr.toArray()) {
            if (!e.isDouble()) return makeError(-32602, "clipIds element not a number");
            ids.push_back(static_cast<int>(e.toDouble()));
        }
        int result = c.mergeClips(ids);
        if (result < 0) return makeError(-32602, "merge failed: clips must be ≥2 MIDI clips on the same track");
        return { false, result };
    }
    if (m == "paintClips") {
        auto srcArr = o.value("sourceClipIds");
        if (!srcArr.isArray()) return makeError(-32602, "sourceClipIds array required");
        std::vector<int> srcIds;
        for (const auto& e : srcArr.toArray()) {
            if (!e.isDouble()) return makeError(-32602, "sourceClipIds element is not a number");
            srcIds.push_back(static_cast<int>(e.toDouble()));
        }
        double origin; int target, count;
        double spacing;
        if (!requireDouble(o, "originBeat", origin, nullptr) || !requireDouble(o, "spacing", spacing, nullptr) || !requireInt(o, "targetTrackIndex", target, nullptr) || !requireInt(o, "count", count, nullptr))
            return makeError(-32602, "originBeat, spacing, targetTrackIndex, count required");
        auto ids = c.paintClips(srcIds, origin, spacing, target, count);
        QJsonArray arr;
        for (int id : ids) arr.append(id);
        return { false, arr };
    }
    if (m == "duplicateClips") {
        auto idsArr = o.value("clipIds");
        auto startsArr = o.value("newStarts");
        auto tracksArr = o.value("newTrackIndices");
        if (!idsArr.isArray() || !startsArr.isArray() || !tracksArr.isArray())
            return makeError(-32602, "clipIds, newStarts, newTrackIndices arrays required");
        std::vector<int> ids;
        std::vector<double> starts;
        std::vector<int> tracks;
        for (const auto& e : idsArr.toArray()) { if (!e.isDouble()) return makeError(-32602, "clipIds element not a number"); ids.push_back(static_cast<int>(e.toDouble())); }
        for (const auto& e : startsArr.toArray()) { if (!e.isDouble()) return makeError(-32602, "newStarts element not a number"); starts.push_back(e.toDouble()); }
        for (const auto& e : tracksArr.toArray()) { if (!e.isDouble()) return makeError(-32602, "newTrackIndices element not a number"); tracks.push_back(static_cast<int>(e.toDouble())); }
        if (ids.size() != starts.size() || ids.size() != tracks.size())
            return makeError(-32602, "array lengths must match");
        auto newIds = c.duplicateClips(ids, starts, tracks);
        QJsonArray arr;
        for (int id : newIds) arr.append(id);
        return { false, arr };
    }
    if (m == "moveClips") {
        auto idsArr = o.value("clipIds");
        auto startsArr = o.value("newStarts");
        auto tracksArr = o.value("newTrackIndices");
        if (!idsArr.isArray() || !startsArr.isArray() || !tracksArr.isArray())
            return makeError(-32602, "clipIds, newStarts, newTrackIndices arrays required");
        std::vector<int> ids;
        std::vector<double> starts;
        std::vector<int> tracks;
        for (const auto& e : idsArr.toArray()) { if (!e.isDouble()) return makeError(-32602, "clipIds element not a number"); ids.push_back(static_cast<int>(e.toDouble())); }
        for (const auto& e : startsArr.toArray()) { if (!e.isDouble()) return makeError(-32602, "newStarts element not a number"); starts.push_back(e.toDouble()); }
        for (const auto& e : tracksArr.toArray()) { if (!e.isDouble()) return makeError(-32602, "newTrackIndices element not a number"); tracks.push_back(static_cast<int>(e.toDouble())); }
        if (ids.size() != starts.size() || ids.size() != tracks.size())
            return makeError(-32602, "array lengths must match");
        c.moveClips(ids, starts, tracks);
        return { false, QJsonValue::Null };
    }
    if (m == "removeClips") {
        auto idsArr = o.value("clipIds");
        if (!idsArr.isArray()) return makeError(-32602, "clipIds array required");
        std::vector<int> ids;
        for (const auto& e : idsArr.toArray()) { if (!e.isDouble()) return makeError(-32602, "clipIds element not a number"); ids.push_back(static_cast<int>(e.toDouble())); }
        c.removeClips(ids);
        return { false, QJsonValue::Null };
    }
    if (m == "rippleDelete") {
        double sb, eb;
        if (!requireDouble(o, "startBeat", sb, nullptr) || !requireDouble(o, "endBeat", eb, nullptr))
            return makeError(-32602, "startBeat and endBeat required");
        c.rippleDelete(sb, eb);
        return { false, QJsonValue::Null };
    }
    if (m == "insertSilence") {
        double sb, eb;
        if (!requireDouble(o, "startBeat", sb, nullptr) || !requireDouble(o, "endBeat", eb, nullptr))
            return makeError(-32602, "startBeat and endBeat required");
        c.insertSilence(sb, eb);
        return { false, QJsonValue::Null };
    }
    if (m == "duplicateRegion") {
        double sb, eb;
        if (!requireDouble(o, "startBeat", sb, nullptr) || !requireDouble(o, "endBeat", eb, nullptr))
            return makeError(-32602, "startBeat and endBeat required");
        c.duplicateRegion(sb, eb);
        return { false, QJsonValue::Null };
    }
    if (m == "addClips") {
        int trackIndex;
        if (!requireInt(o, "trackIndex", trackIndex, nullptr))
            return makeError(-32602, "trackIndex required");
        auto startsArr = o.value("starts");
        auto durationsArr = o.value("durations");
        auto namesArr = o.value("names");
        if (!startsArr.isArray() || !durationsArr.isArray() || !namesArr.isArray())
            return makeError(-32602, "starts, durations, names arrays required");
        std::vector<double> starts;
        std::vector<double> durations;
        std::vector<std::string> names;
        for (const auto& e : startsArr.toArray()) { if (!e.isDouble()) return makeError(-32602, "starts element not a number"); starts.push_back(e.toDouble()); }
        for (const auto& e : durationsArr.toArray()) { if (!e.isDouble()) return makeError(-32602, "durations element not a number"); durations.push_back(e.toDouble()); }
        for (const auto& e : namesArr.toArray()) { if (!e.isString()) return makeError(-32602, "names element not a string"); names.push_back(e.toString().toStdString()); }
        auto sourceFilesArr = o.value("sourceFiles");
        std::vector<std::string> sourceFiles;
        if (sourceFilesArr.isArray()) {
            for (const auto& e : sourceFilesArr.toArray()) {
                sourceFiles.push_back(e.isString() ? e.toString().toStdString() : std::string());
            }
        }
        if (starts.size() != durations.size() || starts.size() != names.size())
            return makeError(-32602, "array lengths must match");
        auto newIds = c.addClips(trackIndex, starts, durations, names, sourceFiles);
        QJsonArray arr;
        for (int id : newIds) arr.append(id);
        return { false, arr };
    }
    if (m == "setClipStart")    { int i; double v; if (!requireInt(o, "clipId", i, nullptr) || !requireDouble(o, "start", v, nullptr)) return makeError(-32602, "clipId and start required"); c.setClipStart(i, v); return { false, QJsonValue::Null }; }
    if (m == "setClipDuration") { int i; double v; if (!requireInt(o, "clipId", i, nullptr) || !requireDouble(o, "duration", v, nullptr)) return makeError(-32602, "clipId and duration required"); c.setClipDuration(i, v); return { false, QJsonValue::Null }; }
    if (m == "setClipGain")     { int i; float v;  if (!requireInt(o, "clipId", i, nullptr) || !requireFloat(o, "gain", v, nullptr)) return makeError(-32602, "clipId and gain required"); c.setClipGain(i, v); return { false, QJsonValue::Null }; }
    if (m == "setClipFadeIn")   { int i; double v; if (!requireInt(o, "clipId", i, nullptr) || !requireDouble(o, "fadeIn", v, nullptr)) return makeError(-32602, "clipId and fadeIn required"); c.setClipFadeIn(i, v); return { false, QJsonValue::Null }; }
    if (m == "setClipFadeOut")  { int i; double v; if (!requireInt(o, "clipId", i, nullptr) || !requireDouble(o, "fadeOut", v, nullptr)) return makeError(-32602, "clipId and fadeOut required"); c.setClipFadeOut(i, v); return { false, QJsonValue::Null }; }
    if (m == "setClipOffset")   { int i; double v; if (!requireInt(o, "clipId", i, nullptr) || !requireDouble(o, "offset", v, nullptr)) return makeError(-32602, "clipId and offset required"); c.setClipOffset(i, v); return { false, QJsonValue::Null }; }
    if (m == "setClipLooping")  { int i; bool b;   if (!requireInt(o, "clipId", i, nullptr) || !requireBool(o, "looping", b, nullptr)) return makeError(-32602, "clipId and looping required"); c.setClipLooping(i, b); return { false, QJsonValue::Null }; }
    if (m == "setClipMuted")    { int i; bool b;   if (!requireInt(o, "clipId", i, nullptr) || !requireBool(o, "muted", b, nullptr)) return makeError(-32602, "clipId and muted required"); c.setClipMuted(i, b); return { false, QJsonValue::Null }; }

    // --- Timestretch ---
    if (m == "setClipSourceBpm")    { int i; double v; if (!requireInt(o, "clipId", i, nullptr) || !requireDouble(o, "bpm", v, nullptr)) return makeError(-32602, "clipId and bpm required"); c.setClipSourceBpm(i, v); return { false, QJsonValue::Null }; }
    if (m == "setClipStretchMode")  { int i, mode;   if (!requireInt(o, "clipId", i, nullptr) || !requireInt(o, "mode", mode, nullptr)) return makeError(-32602, "clipId and mode required"); c.setClipStretchMode(i, mode); return { false, QJsonValue::Null }; }
    if (m == "setClipStretchRatio") { int i; double v; if (!requireInt(o, "clipId", i, nullptr) || !requireDouble(o, "ratio", v, nullptr)) return makeError(-32602, "clipId and ratio required"); c.setClipStretchRatio(i, v); return { false, QJsonValue::Null }; }
    if (m == "tempoMatchClip")      { int i; if (!requireInt(o, "clipId", i, nullptr)) return makeError(-32602, "clipId required"); c.tempoMatchClip(i); return { false, QJsonValue::Null }; }
    if (m == "fitClipToLoop")       { int i; if (!requireInt(o, "clipId", i, nullptr)) return makeError(-32602, "clipId required"); c.fitClipToLoop(i); return { false, QJsonValue::Null }; }
    if (m == "importAudioFile") {
        int i; double start; std::string path; bool align = true;
        if (!requireInt(o, "trackIndex", i, nullptr) || !requireDouble(o, "start", start, nullptr) || !requireString(o, "path", path, nullptr)) return makeError(-32602, "trackIndex, start, path required");
        if (o.contains("alignToGrid")) align = o.value("alignToGrid").toBool();
        auto r = c.importAudioFile(i, start, path, align);
        QJsonObject out;
        out["clipId"] = r.clipId; out["aligned"] = r.aligned;
        out["bpm"] = r.bpm; out["confidence"] = r.confidence;
        out["bars"] = r.bars; out["beatsPerBar"] = r.beatsPerBar;
        out["ratio"] = r.ratio; out["offset"] = r.offset; out["duration"] = r.duration;
        if (!r.error.empty()) out["error"] = QString::fromStdString(r.error);
        return { false, out };
    }
    if (m == "alignClipToGrid") {
        int id; if (!requireInt(o, "clipId", id, nullptr)) return makeError(-32602, "clipId required");
        auto r = c.alignClipToGrid(id);
        QJsonObject out;
        out["ok"] = r.ok; out["bpm"] = r.bpm; out["confidence"] = r.confidence;
        out["bars"] = r.bars; out["beatsPerBar"] = r.beatsPerBar;
        out["ratio"] = r.ratio; out["offset"] = r.offset; out["duration"] = r.duration;
        if (!r.error.empty()) out["error"] = QString::fromStdString(r.error);
        return { false, out };
    }

    // --- MIDI notes ---
    if (m == "addNote") {
        int clip, pitch, vel; double start, dur;
        if (!requireInt(o, "clipId", clip, nullptr) || !requireInt(o, "pitch", pitch, nullptr) || !requireInt(o, "velocity", vel, nullptr) || !requireDouble(o, "startBeat", start, nullptr) || !requireDouble(o, "durationBeats", dur, nullptr))
            return makeError(-32602, "clipId, pitch, velocity, startBeat, durationBeats required");
        return { false, c.addNote(clip, pitch, vel, start, dur) };
    }
    if (m == "removeNote")      { int i; if (!requireInt(o, "noteId", i, nullptr)) return makeError(-32602, "noteId required"); c.removeNote(i); return { false, QJsonValue::Null }; }
    if (m == "setNotePitch")    { int i, v; if (!requireInt(o, "noteId", i, nullptr) || !requireInt(o, "pitch", v, nullptr)) return makeError(-32602, "noteId and pitch required"); c.setNotePitch(i, v); return { false, QJsonValue::Null }; }
    if (m == "setNoteVelocity") { int i, v; if (!requireInt(o, "noteId", i, nullptr) || !requireInt(o, "velocity", v, nullptr)) return makeError(-32602, "noteId and velocity required"); c.setNoteVelocity(i, v); return { false, QJsonValue::Null }; }
    if (m == "setNoteStart")    { int i; double v; if (!requireInt(o, "noteId", i, nullptr) || !requireDouble(o, "startBeat", v, nullptr)) return makeError(-32602, "noteId and startBeat required"); c.setNoteStart(i, v); return { false, QJsonValue::Null }; }
    if (m == "setNoteDuration") { int i; double v; if (!requireInt(o, "noteId", i, nullptr) || !requireDouble(o, "durationBeats", v, nullptr)) return makeError(-32602, "noteId and durationBeats required"); c.setNoteDuration(i, v); return { false, QJsonValue::Null }; }
    if (m == "setNoteChance")      { int i; double v; if (!requireInt(o, "noteId", i, nullptr) || !requireDouble(o, "chance", v, nullptr)) return makeError(-32602, "noteId and chance required"); c.setNoteChance(i, static_cast<float>(v)); return { false, QJsonValue::Null }; }
    if (m == "setNoteRepeatCount") { int i, v; if (!requireInt(o, "noteId", i, nullptr) || !requireInt(o, "repeatCount", v, nullptr)) return makeError(-32602, "noteId and repeatCount required"); c.setNoteRepeatCount(i, v); return { false, QJsonValue::Null }; }
    if (m == "setNoteRepeatRate")  { int i; double v; if (!requireInt(o, "noteId", i, nullptr) || !requireDouble(o, "repeatRate", v, nullptr)) return makeError(-32602, "noteId and repeatRate required"); c.setNoteRepeatRate(i, static_cast<float>(v)); return { false, QJsonValue::Null }; }
    if (m == "setNoteRepeatCurve") { int i; double v; if (!requireInt(o, "noteId", i, nullptr) || !requireDouble(o, "repeatCurve", v, nullptr)) return makeError(-32602, "noteId and repeatCurve required"); c.setNoteRepeatCurve(i, static_cast<float>(v)); return { false, QJsonValue::Null }; }
    if (m == "setNoteOccurrence")  { int i, v; if (!requireInt(o, "noteId", i, nullptr) || !requireInt(o, "occurrence", v, nullptr)) return makeError(-32602, "noteId and occurrence required"); c.setNoteOccurrence(i, v); return { false, QJsonValue::Null }; }
    if (m == "setNoteRecurrence")  { int i, v; if (!requireInt(o, "noteId", i, nullptr) || !requireInt(o, "recurrence", v, nullptr)) return makeError(-32602, "noteId and recurrence required"); c.setNoteRecurrence(i, v); return { false, QJsonValue::Null }; }
    if (m == "setNoteGain")       { int i; double v; if (!requireInt(o, "noteId", i, nullptr) || !requireDouble(o, "gain", v, nullptr)) return makeError(-32602, "noteId and gain required"); c.setNoteGain(i, static_cast<float>(v)); return { false, QJsonValue::Null }; }
    if (m == "setNotePan")        { int i; double v; if (!requireInt(o, "noteId", i, nullptr) || !requireDouble(o, "pan", v, nullptr)) return makeError(-32602, "noteId and pan required"); c.setNotePan(i, static_cast<float>(v)); return { false, QJsonValue::Null }; }
    if (m == "setNotePitchOffset"){ int i; double v; if (!requireInt(o, "noteId", i, nullptr) || !requireDouble(o, "pitchOffset", v, nullptr)) return makeError(-32602, "noteId and pitchOffset required"); c.setNotePitchOffset(i, static_cast<float>(v)); return { false, QJsonValue::Null }; }
    if (m == "setNoteTimbre")     { int i; double v; if (!requireInt(o, "noteId", i, nullptr) || !requireDouble(o, "timbre", v, nullptr)) return makeError(-32602, "noteId and timbre required"); c.setNoteTimbre(i, static_cast<float>(v)); return { false, QJsonValue::Null }; }
    if (m == "setNotePressure")   { int i; double v; if (!requireInt(o, "noteId", i, nullptr) || !requireDouble(o, "pressure", v, nullptr)) return makeError(-32602, "noteId and pressure required"); c.setNotePressure(i, static_cast<float>(v)); return { false, QJsonValue::Null }; }
    if (m == "setNotesExpression"){
        int noteId; double gain, pan, pitchOffset, timbre, pressure;
        if (!requireInt(o, "noteId", noteId, nullptr)
            || !requireDouble(o, "gain", gain, nullptr) || !requireDouble(o, "pan", pan, nullptr)
            || !requireDouble(o, "pitchOffset", pitchOffset, nullptr) || !requireDouble(o, "timbre", timbre, nullptr)
            || !requireDouble(o, "pressure", pressure, nullptr))
            return makeError(-32602, "noteId, gain, pan, pitchOffset, timbre, pressure required");
        c.setNotesExpression(noteId, static_cast<float>(gain), static_cast<float>(pan), static_cast<float>(pitchOffset), static_cast<float>(timbre), static_cast<float>(pressure));
        return { false, QJsonValue::Null };
    }
    if (m == "setClipSeed")        { int i; double v; if (!requireInt(o, "clipId", i, nullptr) || !requireDouble(o, "seed", v, nullptr)) return makeError(-32602, "clipId and seed required"); c.setClipSeed(i, static_cast<uint64_t>(v)); return { false, QJsonValue::Null }; }
    if (m == "setNotesOperator")   {
        int clipId, noteId; double chance, repeatRate, repeatCurve; int repeatCount, occurrence, recurrence;
        if (!requireInt(o, "clipId", clipId, nullptr) || !requireInt(o, "noteId", noteId, nullptr)
            || !requireDouble(o, "chance", chance, nullptr) || !requireInt(o, "repeatCount", repeatCount, nullptr)
            || !requireDouble(o, "repeatRate", repeatRate, nullptr) || !requireDouble(o, "repeatCurve", repeatCurve, nullptr)
            || !requireInt(o, "occurrence", occurrence, nullptr) || !requireInt(o, "recurrence", recurrence, nullptr))
            return makeError(-32602, "clipId, noteId, chance, repeatCount, repeatRate, repeatCurve, occurrence, recurrence required");
        c.setNotesOperator(clipId, noteId, static_cast<float>(chance), repeatCount, static_cast<float>(repeatRate), static_cast<float>(repeatCurve), occurrence, recurrence);
        return { false, QJsonValue::Null };
    }
    if (m == "clearNotes")      { int i; if (!requireInt(o, "clipId", i, nullptr)) return makeError(-32602, "clipId required"); c.clearNotes(i); return { false, QJsonValue::Null }; }
    if (m == "addCcPoint")      { int clip, cc; double beat; int val; if (!requireInt(o, "clipId", clip, nullptr) || !requireInt(o, "controllerNumber", cc, nullptr) || !requireDouble(o, "beat", beat, nullptr) || !requireInt(o, "value", val, nullptr)) return makeError(-32602, "clipId, controllerNumber, beat, value required"); c.addCcPoint(clip, cc, beat, val); return { false, QJsonValue::Null }; }
    if (m == "setCcPoint")      { int id; double beat; int val; if (!requireInt(o, "ccId", id, nullptr) || !requireDouble(o, "beat", beat, nullptr) || !requireInt(o, "value", val, nullptr)) return makeError(-32602, "ccId, beat, value required"); c.setCcPoint(id, beat, val); return { false, QJsonValue::Null }; }
    if (m == "removeCcPoint")   { int id; if (!requireInt(o, "ccId", id, nullptr)) return makeError(-32602, "ccId required"); c.removeCcPoint(id); return { false, QJsonValue::Null }; }
    if (m == "setCcRecordArmed") { bool b; if (!requireBool(o, "armed", b, nullptr)) return makeError(-32602, "armed required"); c.setCcRecordArmed(b); return { false, QJsonValue::Null }; }
    if (m == "setMidiNoteRecordArmed") { bool b; if (!requireBool(o, "armed", b, nullptr)) return makeError(-32602, "armed required"); c.setMidiNoteRecordArmed(b); return { false, QJsonValue::Null }; }

    // --- FX ---
    if (m == "addFxSlot") {
        int i; std::string type; int pos; std::string pluginId;
        if (!requireInt(o, "trackIndex", i, nullptr))
            return makeError(-32602, "trackIndex required");
        // Accept either `type` (canonical) or `fxType` (frontend spelling) for
        // the FX-type string. Accept either `position` (canonical) or
        // `slotIndex` (frontend spelling) for the insertion index; default
        // -1 = append. Both spellings are tolerated silently.
        if (o.contains("type") && o.value("type").isString())
            type = o.value("type").toString().toStdString();
        else if (o.contains("fxType") && o.value("fxType").isString())
            type = o.value("fxType").toString().toStdString();
        else
            return makeError(-32602, "type (or fxType) required");
        if (o.contains("position") && o.value("position").isDouble())
            pos = static_cast<int>(o.value("position").toDouble());
        else if (o.contains("slotIndex") && o.value("slotIndex").isDouble())
            pos = static_cast<int>(o.value("slotIndex").toDouble());
        else
            pos = -1;
        pluginId = optString(o, "pluginId", "");
        c.addFxSlot(i, type, pos, pluginId);  // string overload
        return { false, QJsonValue::Null };
    }
    if (m == "addMidiFxSlot") {
        int i; std::string type; int pos = -1;
        if (!requireInt(o, "trackIndex", i, nullptr))
            return makeError(-32602, "trackIndex required");
        if (o.contains("type") && o.value("type").isString())
            type = o.value("type").toString().toStdString();
        else if (o.contains("fxType") && o.value("fxType").isString())
            type = o.value("fxType").toString().toStdString();
        else
            return makeError(-32602, "type (or fxType) required");
        if (o.contains("position") && o.value("position").isDouble())
            pos = static_cast<int>(o.value("position").toDouble());
        c.addMidiFxSlot(i, type, pos);
        return { false, QJsonValue::Null };
    }
    if (m == "removeMidiFxSlot")    { int i, s; if (!requireInt(o, "trackIndex", i, nullptr) || !requireInt(o, "slotIndex", s, nullptr)) return makeError(-32602, "trackIndex and slotIndex required"); c.removeMidiFxSlot(i, s); return { false, QJsonValue::Null }; }
    if (m == "setMidiFxSlotBypassed") { int i, s; bool b; if (!requireInt(o, "trackIndex", i, nullptr) || !requireInt(o, "slotIndex", s, nullptr) || !requireBool(o, "bypassed", b, nullptr)) return makeError(-32602, "trackIndex, slotIndex, bypassed required"); c.setMidiFxSlotBypassed(i, s, b); return { false, QJsonValue::Null }; }
    if (m == "setMidiFxSlotParam") {
        int i, s; std::string paramName; double v;
        if (!requireInt(o, "trackIndex", i, nullptr) || !requireInt(o, "slotIndex", s, nullptr)
            || !requireString(o, "paramName", paramName, nullptr) || !requireDouble(o, "value", v, nullptr))
            return makeError(-32602, "trackIndex, slotIndex, paramName, value required");
        c.setMidiFxSlotParam(i, s, paramName, v);
        return { false, QJsonValue::Null };
    }
    if (m == "removeFxSlot")        { int i, s; if (!requireInt(o, "trackIndex", i, nullptr) || !requireInt(o, "slotIndex", s, nullptr)) return makeError(-32602, "trackIndex and slotIndex required"); c.removeFxSlot(i, s); return { false, QJsonValue::Null }; }
    if (m == "setFxSlotBypassed")   { int i, s; bool b; if (!requireInt(o, "trackIndex", i, nullptr) || !requireInt(o, "slotIndex", s, nullptr) || !requireBool(o, "bypassed", b, nullptr)) return makeError(-32602, "trackIndex, slotIndex, bypassed required"); c.setFxSlotBypassed(i, s, b); return { false, QJsonValue::Null }; }
    if (m == "setFxSlotParam")      { int i, s, p; float v; if (!requireInt(o, "trackIndex", i, nullptr) || !requireInt(o, "slotIndex", s, nullptr) || !requireInt(o, "paramIndex", p, nullptr) || !requireFloat(o, "value", v, nullptr)) return makeError(-32602, "trackIndex, slotIndex, paramIndex, value required"); c.setFxSlotParam(i, s, p, v); return { false, QJsonValue::Null }; }
    if (m == "clearPluginParamOverrides") {
        int i, s;
        if (!requireInt(o, "trackIndex", i, nullptr) || !requireInt(o, "slotIndex", s, nullptr))
            return makeError(-32602, "trackIndex and slotIndex required");
        const int removed = c.clearPluginParamOverrides(i, s);
        if (removed < 0) return makeError(-32602, "slot not found");
        return { false, QJsonObject{ { "removed", removed } } };
    }
    if (m == "getPluginParamOverrides") {
        int i, s;
        if (!requireInt(o, "trackIndex", i, nullptr) || !requireInt(o, "slotIndex", s, nullptr))
            return makeError(-32602, "trackIndex and slotIndex required");
        QJsonArray arr;
        for (const auto& ov : c.getPluginParamOverrides(i, s))
            arr.append(QJsonObject{ { "paramIndex", ov.first },
                                    { "value", static_cast<double>(ov.second) } });
        return { false, QJsonObject{ { "overrides", arr } } };
    }
    if (m == "applySubSynthModPreset") {
        int i, s; std::string presetId;
        if (!requireInt(o, "trackIndex", i, nullptr) || !requireInt(o, "slotIndex", s, nullptr)
            || !requireString(o, "presetId", presetId, nullptr))
            return makeError(-32602, "trackIndex, slotIndex, presetId required");
        std::string err;
        if (!c.applySubSynthModPreset(i, s, presetId, &err))
            return makeError(-32602, QString::fromStdString(err.empty() ? "applySubSynthModPreset failed" : err));
        return { false, QJsonObject{{"ok", true}, {"presetId", QString::fromStdString(presetId)}} };
    }
    if (m == "setLayerHandoff") {
        int i; if (!requireInt(o, "trackIndex", i, nullptr)) return makeError(-32602, "trackIndex required");
        ProjectCommands::LayerHandoff h;
        if (o.value("role").isString())          h.role          = o.value("role").toString().toStdString();
        if (o.value("soundIntent").isString())   h.soundIntent   = o.value("soundIntent").toString().toStdString();
        if (o.value("patternIntent").isString()) h.patternIntent = o.value("patternIntent").toString().toStdString();
        if (o.value("modulation").isObject())
            h.modulation = QString::fromUtf8(QJsonDocument(o.value("modulation").toObject())
                .toJson(QJsonDocument::Compact)).toStdString();
        if (o.value("verify").isObject())
            h.verify = QString::fromUtf8(QJsonDocument(o.value("verify").toObject())
                .toJson(QJsonDocument::Compact)).toStdString();
        std::string err;
        if (!c.setLayerHandoff(i, h, &err))
            return makeError(-32602, QString::fromStdString(err.empty() ? "setLayerHandoff failed" : err));
        return { false, QJsonObject{{ "ok", true }, { "trackIndex", i }} };
    }
    if (m == "clearLayerHandoff") {
        int i; if (!requireInt(o, "trackIndex", i, nullptr)) return makeError(-32602, "trackIndex required");
        std::string err;
        if (!c.clearLayerHandoff(i, &err))
            return makeError(-32602, QString::fromStdString(err.empty() ? "clearLayerHandoff failed" : err));
        return { false, QJsonObject{{ "ok", true }} };
    }
    if (m == "reorderFxSlots")      { int i, f, t; if (!requireInt(o, "trackIndex", i, nullptr) || !requireInt(o, "fromSlot", f, nullptr) || !requireInt(o, "toSlot", t, nullptr)) return makeError(-32602, "trackIndex, fromSlot, toSlot required"); c.reorderFxSlots(i, f, t); return { false, QJsonValue::Null }; }
    if (m == "setFxSlotPlugin") {
        int i, s; std::string fxType, pluginID, fmt, path;
        if (!requireInt(o, "trackIndex", i, nullptr) || !requireInt(o, "slotIndex", s, nullptr) || !requireString(o, "fxType", fxType, nullptr) || !requireString(o, "pluginID", pluginID, nullptr) || !requireString(o, "pluginFormat", fmt, nullptr) || !requireString(o, "pluginPath", path, nullptr))
            return makeError(-32602, "trackIndex, slotIndex, fxType, pluginID, pluginFormat, pluginPath required");
        c.setFxSlotPlugin(i, s, fxType, pluginID, fmt, path); return { false, QJsonValue::Null };
    }
    if (m == "respawnPlugin") {
        int i, s;
        if (!requireInt(o, "trackIndex", i, nullptr) || !requireInt(o, "slotIndex", s, nullptr))
            return makeError(-32602, "trackIndex and slotIndex required");
        c.respawnFxSlot(i, s);
        return { false, QJsonValue::Null };
    }
    // --- FX chain presets (plan 2026-09-02-fx-chain-presets, Task 3) ---
    if (m == "saveFxChainPreset") {
        int i; std::string name;
        if (!requireInt(o, "trackIndex", i, nullptr) || !requireString(o, "name", name, nullptr))
            return makeError(-32602, "trackIndex and name required");
        // exportFxChain returns an empty preset for an out-of-range index,
        // which would save a 0-slot junk file as success. Reject both bounds
        // here: getTrackCount() < 0 means unknown (skip upper-bound check).
        int n = c.getTrackCount();
        if (i < 0 || (n >= 0 && i >= n))
            return makeError(-32602, "track not found");
        HDAW::ChainPreset p = c.exportFxChain(i);
        p.name = juce::String(name);
        juce::String id = HDAW::ChainLibrary::userLibrary().savePreset(p);
        if (id.isEmpty())
            return makeError(-32602, "failed to save chain preset");
        QJsonObject out;
        out["id"] = QString::fromStdString(id.toStdString());
        return { false, out };
    }
    if (m == "listFxChainPresets") {
        QJsonArray arr;
        for (const auto& p : HDAW::ChainLibrary::userLibrary().listPresets()) {
            QJsonObject e;
            e["id"] = QString::fromStdString(p.id.toStdString());
            e["name"] = QString::fromStdString(p.name.toStdString());
            e["slotCount"] = static_cast<int>(p.slots.size());
            e["source"] = p.isFactory ? QString("factory") : QString("user");
            arr.append(e);
        }
        return { false, arr };
    }
    if (m == "loadFxChainPreset") {
        int i;
        if (!requireInt(o, "trackIndex", i, nullptr))
            return makeError(-32602, "trackIndex required");
        bool hasId = o.contains("id") && o.value("id").isString();
        bool hasName = o.contains("name") && o.value("name").isString();
        if (!hasId && !hasName)
            return makeError(-32602, "id or name required");
        const auto& lib = HDAW::ChainLibrary::userLibrary();
        HDAW::ChainPreset preset;
        if (hasId) {
            // Both given: id wins (deterministic); name-only resolves below.
            preset = lib.loadPreset(juce::String(o.value("id").toString().toStdString()));
            if (preset.id.isEmpty())
                return makeError(-32602, "chain preset not found");
        } else {
            juce::String want(o.value("name").toString().toStdString());
            int matches = 0;
            for (const auto& p : lib.listPresets()) {
                if (p.name == want) { preset = p; ++matches; }
            }
            if (matches == 0)
                return makeError(-32602, "chain preset not found");
            if (matches > 1)
                return makeError(-32602, "ambiguous chain preset name");
        }
        juce::String error;
        if (!c.applyFxChain(i, preset, &error))
            return makeError(-32602, QString::fromStdString(error.toStdString()));
        // NOTE: warnings is always empty. applyFxChain has no warnings
        // channel by design (Task 2 contract): a missing sampler sample is
        // an HDAW_LOG plus a slot applied without its sample — never a
        // silent pass, never a hard failure. The array exists for schema
        // stability so a future warnings channel needs no shape change.
        QJsonObject out;
        out["ok"] = true;
        out["warnings"] = QJsonArray{};
        return { false, out };
    }
    if (m == "deleteFxChainPreset") {
        std::string id;
        if (!requireString(o, "id", id, nullptr))
            return makeError(-32602, "id required");
        if (!HDAW::ChainLibrary::userLibrary().deletePreset(juce::String(id)))
            return makeError(-32602, "chain preset not found or not deletable");
        QJsonObject out;
        out["ok"] = true;
        return { false, out };
    }
    if (m == "sampler.setSample") {
        int ti, si; std::string filePath; int root = 60;
        if (!requireInt(o, "trackIndex", ti, nullptr) || !requireInt(o, "slotIndex", si, nullptr)
            || !requireString(o, "filePath", filePath, nullptr))
            return makeError(-32602, "trackIndex, slotIndex, filePath required");
        if (o.contains("rootNote")) root = static_cast<int>(o.value("rootNote").toDouble(60));
        c.setSamplerSample(ti, si, filePath, root);
        return { false, QJsonValue::Null };
    }

    // --- Automation ---
    if (m == "addAutomationLane")       { int i; std::string lane; if (!requireInt(o, "trackIndex", i, nullptr) || !requireString(o, "laneName", lane, nullptr)) return makeError(-32602, "trackIndex and laneName required"); int paramID = optInt(o, "paramID", 0, nullptr); bool replace = optBool(o, "replace", false, nullptr); if (!c.addAutomationLane(i, lane, paramID, replace)) return makeError(-32602, "lane name or paramID already exists"); return { false, QJsonValue::Null }; }
    if (m == "removeAutomationLane")    { int i; std::string lane; if (!requireInt(o, "trackIndex", i, nullptr) || !requireString(o, "laneName", lane, nullptr)) return makeError(-32602, "trackIndex and laneName required"); c.removeAutomationLane(i, lane); return { false, QJsonValue::Null }; }
    if (m == "addAutomationPoint")      { int i; std::string lane; double t; float v; if (!requireInt(o, "trackIndex", i, nullptr) || !requireString(o, "lane", lane, nullptr) || !requireDouble(o, "time", t, nullptr) || !requireFloat(o, "value", v, nullptr)) return makeError(-32602, "trackIndex, lane, time, value required"); c.addAutomationPoint(i, lane, t, v); return { false, QJsonValue::Null }; }
    if (m == "removeAutomationPoint")   { int i; std::string lane; double t; if (!requireInt(o, "trackIndex", i, nullptr) || !requireString(o, "lane", lane, nullptr) || !requireDouble(o, "time", t, nullptr)) return makeError(-32602, "trackIndex, lane, time required"); c.removeAutomationPoint(i, lane, t); return { false, QJsonValue::Null }; }
    if (m == "setAutomationEnabled")    { int i; std::string lane; bool b; if (!requireInt(o, "trackIndex", i, nullptr) || !requireString(o, "lane", lane, nullptr) || !requireBool(o, "enabled", b, nullptr)) return makeError(-32602, "trackIndex, lane, enabled required"); c.setAutomationEnabled(i, lane, b); return { false, QJsonValue::Null }; }
    if (m == "setFaderAuthoritative") { int i; bool b; if (!requireInt(o, "trackIndex", i, nullptr) || !requireBool(o, "authoritative", b, nullptr)) return makeError(-32602, "trackIndex, authoritative required"); c.setFaderAuthoritative(i, b); return { false, QJsonValue::Null }; }
    if (m == "setAutomationPointValue") { int i; std::string lane; double t; float v; if (!requireInt(o, "trackIndex", i, nullptr) || !requireString(o, "lane", lane, nullptr) || !requireDouble(o, "time", t, nullptr) || !requireFloat(o, "value", v, nullptr)) return makeError(-32602, "trackIndex, lane, time, value required"); c.setAutomationPointValue(i, lane, t, v); return { false, QJsonValue::Null }; }
    if (m == "setAutomationMode")       { int i; std::string ln, md; if (!requireInt(o, "trackIndex", i, nullptr) || !requireString(o, "laneName", ln, nullptr) || !requireString(o, "mode", md, nullptr)) return makeError(-32602, "trackIndex, laneName, mode required"); c.setAutomationMode(i, ln, md); return { false, QJsonValue::Null }; }
    if (m == "notifyAutomationTouch")   { int i, pid; bool t; if (!requireInt(o, "trackIndex", i, nullptr) || !requireInt(o, "paramID", pid, nullptr) || !requireBool(o, "touching", t, nullptr)) return makeError(-32602, "trackIndex, paramID, touching required"); c.notifyAutomationTouch(i, pid, t); return { false, QJsonValue::Null }; }
    // Automation preset / movement plan: the MCP twins (automation_preset /
    // apply_movement_plan in McpTools_Automation.cpp) call these SAME shared
    // entry points, so an accepted write and a refusal carry one text on both
    // surfaces by construction. The success payload is the tool's compact JSON,
    // parsed here (read.getFxSlots precedent) rather than passed as a string.
    if (m == "applyAutomationPreset") {
        bool ok = false;
        const QString text = HDAW::automationPresetToolText(c, trackList, o, &ok);
        if (! ok) return makeError(-32602, text);
        // Same compact JSON the tool emits; the client gets the parsed structure,
        // not a string-quoted document (read.getFxSlots precedent).
        return { false, QJsonDocument::fromJson(text.toUtf8()).object() };
    }
    if (m == "applyMovementPlan") {
        bool ok = false;
        const QString text = HDAW::applyMovementPlanToolText(c, o, &ok);
        if (! ok) return makeError(-32602, text);
        return { false, QJsonDocument::fromJson(text.toUtf8()).object() };
    }

    // --- Transport properties ---
    if (m == "setTempo")            { double v; if (!requireDouble(o, "bpm", v, nullptr)) return makeError(-32602, "bpm required"); c.setTempo(v); return { false, QJsonValue::Null }; }
    if (m == "addTempoPoint") {
        double time, bpm;
        if (!requireDouble(o, "timeSeconds", time, nullptr) || !requireDouble(o, "bpm", bpm, nullptr))
            return makeError(-32602, "timeSeconds and bpm required");
        int idx = c.addTempoPoint(time, bpm);
        return { false, idx };
    }
    if (m == "removeTempoPoint") {
        int idx;
        if (!requireInt(o, "index", idx, nullptr))
            return makeError(-32602, "index required");
        c.removeTempoPoint(idx);
        return { false, QJsonValue::Null };
    }
    if (m == "setTempoPointBpm") {
        int idx; double bpm;
        if (!requireInt(o, "index", idx, nullptr) || !requireDouble(o, "bpm", bpm, nullptr))
            return makeError(-32602, "index and bpm required");
        c.setTempoPointBpm(idx, bpm);
        return { false, QJsonValue::Null };
    }
    if (m == "setTempoPointTime") {
        int idx; double time;
        if (!requireInt(o, "index", idx, nullptr) || !requireDouble(o, "timeSeconds", time, nullptr))
            return makeError(-32602, "index and timeSeconds required");
        c.setTempoPointTime(idx, time);
        return { false, QJsonValue::Null };
    }
    if (m == "setLoopStart")        { double v; if (!requireDouble(o, "beat", v, nullptr)) return makeError(-32602, "beat required"); c.setLoopStart(v); return { false, QJsonValue::Null }; }
    if (m == "setLoopEnd")          { double v; if (!requireDouble(o, "beat", v, nullptr)) return makeError(-32602, "beat required"); c.setLoopEnd(v); return { false, QJsonValue::Null }; }
    if (m == "setLooping")          { bool b; if (!requireBool(o, "looping", b, nullptr)) return makeError(-32602, "looping required"); c.setLooping(b); return { false, QJsonValue::Null }; }
    if (m == "setMetronomeEnabled") { bool b; if (!requireBool(o, "enabled", b, nullptr)) return makeError(-32602, "enabled required"); c.setMetronomeEnabled(b); return { false, QJsonValue::Null }; }
    if (m == "setTimeSignature")    { int n, d; if (!requireInt(o, "numerator", n, nullptr) || !requireInt(o, "denominator", d, nullptr)) return makeError(-32602, "numerator and denominator required"); c.setTimeSignature(n, d); return { false, QJsonValue::Null }; }

    // --- Markers ---
    if (m == "addMarker")      { std::string name; double t; if (!requireString(o, "name", name, nullptr) || !requireDouble(o, "time", t, nullptr)) return makeError(-32602, "name and time required"); int color = optInt<int>(o, "color", 0xFF59e0c4, nullptr); return { false, c.addMarker(name, t, color) }; }
    if (m == "removeMarker")   { int i; if (!requireInt(o, "index", i, nullptr)) return makeError(-32602, "index required"); c.removeMarker(i); return { false, QJsonValue::Null }; }
    if (m == "setMarkerName")  { int i; std::string s; if (!requireInt(o, "index", i, nullptr) || !requireString(o, "name", s, nullptr)) return makeError(-32602, "index and name required"); c.setMarkerName(i, s); return { false, QJsonValue::Null }; }
    if (m == "setMarkerTime")  { int i; double t; if (!requireInt(o, "index", i, nullptr) || !requireDouble(o, "time", t, nullptr)) return makeError(-32602, "index and time required"); c.setMarkerTime(i, t); return { false, QJsonValue::Null }; }
    if (m == "setClipName")    { int i; std::string s; if (!requireInt(o, "clipId", i, nullptr) || !requireString(o, "name", s, nullptr)) return makeError(-32602, "clipId and name required"); c.setClipName(i, s); return { false, QJsonValue::Null }; }

    // --- Arranger Regions ---
    if (m == "addArrangerRegion") {
        std::string name; double start, dur;
        if (!requireString(o, "name", name, nullptr) || !requireDouble(o, "startTime", start, nullptr) || !requireDouble(o, "duration", dur, nullptr))
            return makeError(-32602, "name, startTime, duration required");
        int color = optInt<int>(o, "color", 0xFFd97706, nullptr);
        return { false, QString::fromStdString(c.addArrangerRegion(name, start, dur, color)) };
    }
    if (m == "removeArrangerRegion") {
        std::string rid;
        if (!requireString(o, "regionID", rid, nullptr))
            return makeError(-32602, "regionID required");
        c.removeArrangerRegion(rid);
        return { false, QJsonValue::Null };
    }
    if (m == "setArrangerRegionName") {
        std::string rid, name;
        if (!requireString(o, "regionID", rid, nullptr) || !requireString(o, "name", name, nullptr))
            return makeError(-32602, "regionID and name required");
        c.setArrangerRegionName(rid, name);
        return { false, QJsonValue::Null };
    }
    if (m == "setArrangerRegionBounds") {
        std::string rid; double start, dur;
        if (!requireString(o, "regionID", rid, nullptr) || !requireDouble(o, "startTime", start, nullptr) || !requireDouble(o, "duration", dur, nullptr))
            return makeError(-32602, "regionID, startTime, duration required");
        c.setArrangerRegionBounds(rid, start, dur);
        return { false, QJsonValue::Null };
    }
    if (m == "setArrangerRegionColor") {
        std::string rid; int color;
        if (!requireString(o, "regionID", rid, nullptr) || !requireInt(o, "color", color, nullptr))
            return makeError(-32602, "regionID and color required");
        c.setArrangerRegionColor(rid, color);
        return { false, QJsonValue::Null };
    }
    // --- Arranger Chains ---
    if (m == "addArrangerChain") {
        std::string name;
        if (!requireString(o, "name", name, nullptr))
            return makeError(-32602, "name required");
        return { false, QString::fromStdString(c.addArrangerChain(name)) };
    }
    if (m == "removeArrangerChain") {
        std::string cid;
        if (!requireString(o, "chainID", cid, nullptr))
            return makeError(-32602, "chainID required");
        c.removeArrangerChain(cid);
        return { false, QJsonValue::Null };
    }
    if (m == "setArrangerChainName") {
        std::string cid, name;
        if (!requireString(o, "chainID", cid, nullptr) || !requireString(o, "name", name, nullptr))
            return makeError(-32602, "chainID and name required");
        c.setArrangerChainName(cid, name);
        return { false, QJsonValue::Null };
    }
    if (m == "setArrangerChainActive") {
        std::string cid;
        if (!requireString(o, "chainID", cid, nullptr))
            return makeError(-32602, "chainID required");
        c.setArrangerChainActive(cid);
        return { false, QJsonValue::Null };
    }
    // --- Chain Entries ---
    if (m == "addChainEntry") {
        std::string cid, rid;
        if (!requireString(o, "chainID", cid, nullptr) || !requireString(o, "regionID", rid, nullptr))
            return makeError(-32602, "chainID and regionID required");
        int repeat = optInt<int>(o, "repeatCount", 1, nullptr);
        return { false, c.addChainEntry(cid, rid, repeat) };
    }
    if (m == "removeChainEntry") {
        std::string cid; int idx;
        if (!requireString(o, "chainID", cid, nullptr) || !requireInt(o, "entryIndex", idx, nullptr))
            return makeError(-32602, "chainID and entryIndex required");
        c.removeChainEntry(cid, idx);
        return { false, QJsonValue::Null };
    }
    if (m == "reorderChainEntry") {
        std::string cid; int from, to;
        if (!requireString(o, "chainID", cid, nullptr) || !requireInt(o, "fromIndex", from, nullptr) || !requireInt(o, "toIndex", to, nullptr))
            return makeError(-32602, "chainID, fromIndex, toIndex required");
        c.reorderChainEntry(cid, from, to);
        return { false, QJsonValue::Null };
    }
    if (m == "setChainEntryRepeat") {
        std::string cid; int idx, repeat;
        if (!requireString(o, "chainID", cid, nullptr) || !requireInt(o, "entryIndex", idx, nullptr) || !requireInt(o, "repeatCount", repeat, nullptr))
            return makeError(-32602, "chainID, entryIndex, repeatCount required");
        c.setChainEntryRepeat(cid, idx, repeat);
        return { false, QJsonValue::Null };
    }
    // --- Flatten ---
    if (m == "flattenArranger") {
        c.flattenArranger();
        return { false, QJsonValue::Null };
    }

    // --- Gain envelope ---
    if (m == "addGainEnvelopePoint")    { int i; double t, g; if (!requireInt(o, "clipId", i, nullptr) || !requireDouble(o, "time", t, nullptr) || !requireDouble(o, "gain", g, nullptr)) return makeError(-32602, "clipId, time, gain required"); c.addGainEnvelopePoint(i, t, g); return { false, QJsonValue::Null }; }
    if (m == "moveGainEnvelopePoint")   { int i, idx; double t, g; if (!requireInt(o, "clipId", i, nullptr) || !requireInt(o, "pointIndex", idx, nullptr) || !requireDouble(o, "time", t, nullptr) || !requireDouble(o, "gain", g, nullptr)) return makeError(-32602, "clipId, pointIndex, time, gain required"); c.moveGainEnvelopePoint(i, idx, t, g); return { false, QJsonValue::Null }; }
    if (m == "removeGainEnvelopePoint") { int i, idx; if (!requireInt(o, "clipId", i, nullptr) || !requireInt(o, "pointIndex", idx, nullptr)) return makeError(-32602, "clipId and pointIndex required"); c.removeGainEnvelopePoint(i, idx); return { false, QJsonValue::Null }; }
    if (m == "clearGainEnvelope")       { int i; if (!requireInt(o, "clipId", i, nullptr)) return makeError(-32602, "clipId required"); c.clearGainEnvelope(i); return { false, QJsonValue::Null }; }
    if (m == "setClipGainEnvelope") {
        int i; if (!requireInt(o, "clipId", i, nullptr)) return makeError(-32602, "clipId required");
        DispatchResult e;
        auto pointPairs = [](const QJsonValue& v, DispatchResult* err) -> std::vector<std::pair<double, double>> {
            std::vector<std::pair<double, double>> out;
            if (!v.isArray()) { if (err) *err = makeError(-32602, "points must be an array"); return out; }
            for (const auto& el : v.toArray()) {
                if (!el.isObject()) { if (err) *err = makeError(-32602, "each point must be an object {time, gain}"); return {}; }
                auto obj = el.toObject();
                if (!obj.contains("time") || !obj.value("time").isDouble() ||
                    !obj.contains("gain") || !obj.value("gain").isDouble()) {
                    if (err) *err = makeError(-32602, "each point must have numeric time and gain");
                    return {};
                }
                out.emplace_back(obj.value("time").toDouble(), obj.value("gain").toDouble());
            }
            return out;
        }(o.value("points"), &e);
        if (e.isError) return e;
        c.setClipGainEnvelope(i, pointPairs);
        return { false, QJsonValue::Null };
    }
    if (m == "notifyClipGainEnvelopeChanged") { int i; if (!requireInt(o, "clipId", i, nullptr)) return makeError(-32602, "clipId required"); c.notifyClipGainEnvelopeChanged(i); return { false, QJsonValue::Null }; }

    // --- Envelope generation ---
    if (m == "generateAutomationEnvelope") {
        int trackIndex;
        std::string lane, shapeStr;
        if (!requireInt(o, "trackIndex", trackIndex, nullptr))
            return makeError(-32602, "trackIndex required");
        if (!requireString(o, "lane", lane, nullptr))
            return makeError(-32602, "lane required");
        if (!requireString(o, "shape", shapeStr, nullptr))
            return makeError(-32602, "shape required");

        auto shape = parseShape(shapeStr);
        if (!shape) return makeError(-32602, QString::fromStdString("unknown shape: " + shapeStr));

        HDAW::EnvelopeGenerator::Params params;
        params.shape = *shape;
        params.startTime = optDouble(o, "start", 0.0, nullptr);
        params.endTime = optDouble(o, "end", 16.0, nullptr);
        params.startValue = optDouble(o, "startValue", 0.0, nullptr);
        params.endValue = optDouble(o, "endValue", 1.0, nullptr);
        params.cycles = optDouble(o, "cycles", 1.0, nullptr);
        params.steps = optInt(o, "steps", 8, nullptr);
        params.phase = optDouble(o, "phase", 0.0, nullptr);
        params.densityPerSec = optDouble(o, "density", 8.0, nullptr);
        params.smooth = optDouble(o, "smooth", 0.0, nullptr);
        params.seed = optInt<uint64_t>(o, "seed", 0, nullptr);

        c.generateAutomationEnvelope(trackIndex, lane, params);
        return { false, QJsonObject{} };
    }
    if (m == "generateClipGainEnvelope") {
        int clipId;
        std::string shapeStr;
        if (!requireInt(o, "clipId", clipId, nullptr))
            return makeError(-32602, "clipId required");
        if (!requireString(o, "shape", shapeStr, nullptr))
            return makeError(-32602, "shape required");

        auto shape = parseShape(shapeStr);
        if (!shape) return makeError(-32602, QString::fromStdString("unknown shape: " + shapeStr));

        HDAW::EnvelopeGenerator::Params params;
        params.shape = *shape;
        params.startTime = optDouble(o, "start", 0.0, nullptr);
        params.endTime = optDouble(o, "end", 16.0, nullptr);
        params.startValue = optDouble(o, "startValue", 0.0, nullptr);
        params.endValue = optDouble(o, "endValue", 2.0, nullptr);
        params.cycles = optDouble(o, "cycles", 1.0, nullptr);
        params.steps = optInt(o, "steps", 8, nullptr);
        params.phase = optDouble(o, "phase", 0.0, nullptr);
        params.densityPerSec = optDouble(o, "density", 8.0, nullptr);
        params.smooth = optDouble(o, "smooth", 0.0, nullptr);
        params.seed = optInt<uint64_t>(o, "seed", 0, nullptr);

        c.generateClipGainEnvelope(clipId, params);
        return { false, QJsonObject{} };
    }
    if (m == "generateClipCcLane") {
        int clipId, controllerNumber;
        std::string shapeStr;
        if (!requireInt(o, "clipId", clipId, nullptr))
            return makeError(-32602, "clipId required");
        if (!requireInt(o, "controllerNumber", controllerNumber, nullptr))
            return makeError(-32602, "controllerNumber required");
        if (!requireString(o, "shape", shapeStr, nullptr))
            return makeError(-32602, "shape required");

        auto shape = parseShape(shapeStr);
        if (!shape) return makeError(-32602, QString::fromStdString("unknown shape: " + shapeStr));

        HDAW::EnvelopeGenerator::Params params;
        params.shape = *shape;
        params.startTime = optDouble(o, "start", 0.0, nullptr);
        params.endTime = optDouble(o, "end", 16.0, nullptr);
        params.startValue = optDouble(o, "startValue", 0.0, nullptr);
        params.endValue = optDouble(o, "endValue", 127.0, nullptr);
        params.cycles = optDouble(o, "cycles", 1.0, nullptr);
        params.steps = optInt(o, "steps", 8, nullptr);
        params.phase = optDouble(o, "phase", 0.0, nullptr);
        params.densityPerSec = optDouble(o, "density", 8.0, nullptr);
        params.smooth = optDouble(o, "smooth", 0.0, nullptr);
        params.seed = optInt<uint64_t>(o, "seed", 0, nullptr);

        c.generateClipCcLane(clipId, controllerNumber, params);
        return { false, QJsonObject{} };
    }

    // --- Modulation (LFO) ---
    if (m == "addLfo")         { int i; if (!requireInt(o, "trackIndex", i, nullptr)) return makeError(-32602, "trackIndex required"); c.addLfo(i); return { false, QJsonValue::Null }; }
    if (m == "removeLfo")      { int i, idx; if (!requireInt(o, "trackIndex", i, nullptr) || !requireInt(o, "lfoIndex", idx, nullptr)) return makeError(-32602, "trackIndex and lfoIndex required"); c.removeLfo(i, idx); return { false, QJsonValue::Null }; }
    if (m == "setLfoParam")    { int i, idx; std::string name; double v; if (!requireInt(o, "trackIndex", i, nullptr) || !requireInt(o, "lfoIndex", idx, nullptr) || !requireString(o, "paramName", name, nullptr) || !requireDouble(o, "value", v, nullptr)) return makeError(-32602, "trackIndex, lfoIndex, paramName, value required"); c.setLfoParam(i, idx, name, v); return { false, QJsonValue::Null }; }

    // --- Slicing ---
    if (m == "sliceClipAtTimes")     { int i; if (!requireInt(o, "clipId", i, nullptr)) return makeError(-32602, "clipId required"); DispatchResult e; auto times = toDoubleVector(o.value("times"), &e); if (e.isError) return e; c.sliceClipAtTimes(i, times); return { false, QJsonValue::Null }; }
    if (m == "sliceClipAtTransients"){ int i; if (!requireInt(o, "clipId", i, nullptr)) return makeError(-32602, "clipId required"); c.sliceClipAtTransients(i); return { false, QJsonValue::Null }; }
    if (m == "sliceClipAtPlayhead")  { int i; if (!requireInt(o, "clipId", i, nullptr)) return makeError(-32602, "clipId required"); c.sliceClipAtPlayhead(i); return { false, QJsonValue::Null }; }
    if (m == "sliceClipsAtPlayhead") {
        auto idsArr = o.value("clipIds");
        if (!idsArr.isArray()) return makeError(-32602, "clipIds array required");
        std::vector<int> ids;
        for (const auto& e : idsArr.toArray()) {
            if (!e.isDouble()) return makeError(-32602, "clipIds element not a number");
            ids.push_back(static_cast<int>(e.toDouble()));
        }
        c.sliceClipsAtPlayhead(ids);
        return { false, QJsonValue::Null };
    }
    if (m == "sliceClipsAtTransients") {
        auto idsArr = o.value("clipIds");
        if (!idsArr.isArray()) return makeError(-32602, "clipIds array required");
        std::vector<int> ids;
        for (const auto& e : idsArr.toArray()) {
            if (!e.isDouble()) return makeError(-32602, "clipIds element not a number");
            ids.push_back(static_cast<int>(e.toDouble()));
        }
        c.sliceClipsAtTransients(ids);
        return { false, QJsonValue::Null };
    }

    // --- Region cut/copy/paste ---
    if (m == "copyAudioClipRegion") { int i; double s, e; if (!requireInt(o, "clipId", i, nullptr) || !requireDouble(o, "regionStart", s, nullptr) || !requireDouble(o, "regionEnd", e, nullptr)) return makeError(-32602, "clipId, regionStart, regionEnd required"); return { false, c.copyAudioClipRegion(i, s, e) }; }
    if (m == "cutAudioClipRegion")  { int i; double s, e; if (!requireInt(o, "clipId", i, nullptr) || !requireDouble(o, "regionStart", s, nullptr) || !requireDouble(o, "regionEnd", e, nullptr)) return makeError(-32602, "clipId, regionStart, regionEnd required"); return { false, c.cutAudioClipRegion(i, s, e) }; }
    if (m == "pasteAudioClipRegion"){ int i; double t; if (!requireInt(o, "clipId", i, nullptr) || !requireDouble(o, "pasteTime", t, nullptr)) return makeError(-32602, "clipId and pasteTime required"); return { false, c.pasteAudioClipRegion(i, t) }; }

    // --- Undo/redo & transactions ---
    if (m == "undo")  { c.undo();  return { false, QJsonValue::Null }; }
    if (m == "redo")  { c.redo();  return { false, QJsonValue::Null }; }
    if (m == "canUndo")  { return { false, c.canUndo() }; }
    if (m == "canRedo")  { return { false, c.canRedo() }; }
    if (m == "getUndoDescriptions") {
        QJsonArray arr;
        for (const auto& d : c.getUndoDescriptions()) arr.append(QString::fromStdString(d));
        return { false, arr };
    }
    if (m == "getRedoDescriptions") {
        QJsonArray arr;
        for (const auto& d : c.getRedoDescriptions()) arr.append(QString::fromStdString(d));
        return { false, arr };
    }
    if (m == "beginTransaction") { std::string name = optString(o, "name", "edit"); c.beginTransaction(name); return { false, QJsonValue::Null }; }
    if (m == "endTransaction")   { c.endTransaction(); return { false, QJsonValue::Null }; }

    // --- Project lifecycle ---
    if (m == "newProject")  { c.newProject(); return { false, QJsonValue::Null }; }
    if (m == "saveProject") { std::string p; if (!requireString(o, "filePath", p, nullptr)) return makeError(-32602, "filePath required"); return { false, c.saveProject(p) }; }
    if (m == "loadProject") { std::string p; if (!requireString(o, "filePath", p, nullptr)) return makeError(-32602, "filePath required"); return { false, c.loadProject(p) }; }

    // --- Scale ---
    if (m == "setScaleRoot") { int r; if (!requireInt(o, "root", r, nullptr)) return makeError(-32602, "root required"); c.setScaleRoot(r); return { false, QJsonValue::Null }; }
    if (m == "setScaleMode") { int mo; if (!requireInt(o, "mode", mo, nullptr)) return makeError(-32602, "mode required"); c.setScaleMode(mo); return { false, QJsonValue::Null }; }

    // --- Missing-file relinking ---
    if (m == "findMissingClipSourceFile") { int i; std::string d; if (!requireInt(o, "clipId", i, nullptr) || !requireString(o, "searchDir", d, nullptr)) return makeError(-32602, "clipId and searchDir required"); return { false, QString::fromStdString(c.findMissingClipSourceFile(i, d)) }; }
    if (m == "relinkAllMissingFiles") {
        std::string d; if (!requireString(o, "searchDir", d, nullptr)) return makeError(-32602, "searchDir required");
        auto r = c.relinkAllMissingFiles(d);
        return { false, QJsonObject{ { "found", r.found }, { "totalMissing", r.totalMissing } } };
    }

    return makeError(-32601, "unknown project method: " + m);
}

DispatchResult dispatchSettings(AudioEngine& engine, const QString& m, const QJsonValue& params) {
    const auto o = paramsObject(params);

    if (m == "getMaxBackups") { QSettings s; return { false, s.value(SettingsKeys::kKeyMaxBackups, 10).toInt() }; }
    if (m == "setMaxBackups") { int v; if (!requireInt(o, "value", v, nullptr)) return makeError(-32602, "value required"); QSettings s; s.setValue(SettingsKeys::kKeyMaxBackups, v); return { false, QJsonValue::Null }; }
    if (m == "getDefaultTempo") { QSettings s; return { false, s.value(SettingsKeys::kKeyDefaultTempo, 120.0).toDouble() }; }
    if (m == "setDefaultTempo") { double v; if (!requireDouble(o, "value", v, nullptr)) return makeError(-32602, "value required"); QSettings s; s.setValue(SettingsKeys::kKeyDefaultTempo, v); return { false, QJsonValue::Null }; }
    if (m == "getDefaultTimeSignature") {
        QSettings s;
        QJsonObject ts;
        ts["numerator"] = s.value(SettingsKeys::kKeyDefaultTimeSigNum, 4).toInt();
        ts["denominator"] = s.value(SettingsKeys::kKeyDefaultTimeSigDen, 4).toInt();
        return { false, ts };
    }
    if (m == "setDefaultTimeSignature") {
        int num, den;
        if (!requireInt(o, "numerator", num, nullptr) || !requireInt(o, "denominator", den, nullptr))
            return makeError(-32602, "numerator and denominator required");
        QSettings s;
        s.setValue(SettingsKeys::kKeyDefaultTimeSigNum, num);
        s.setValue(SettingsKeys::kKeyDefaultTimeSigDen, den);
        return { false, QJsonValue::Null };
    }

    if (m == "getMcpHttpConfig") {
        const auto cfg = engine.getMcpHttpConfig();
        return { false, QJsonObject{
            { "enabled", cfg.enabled },
            { "host", cfg.host },
            { "port", static_cast<int>(cfg.port) },
            { "running", cfg.running },
            { "lastError", cfg.lastError },
        } };
    }
    if (m == "setMcpHttpConfig") {
        bool enabled = false;
        std::string host;
        int port = 0;
        if (!requireBool(o, "enabled", enabled, nullptr)
            || !requireString(o, "host", host, nullptr)
            || !requireInt(o, "port", port, nullptr))
            return makeError(-32602, "enabled, host, port required");
        if (port < 1 || port > 65535)
            return makeError(-32602, "port must be between 1 and 65535");
        QString error;
        if (!engine.setMcpHttpConfig(enabled, QString::fromStdString(host), static_cast<quint16>(port), &error))
            return makeError(-32000, error.isEmpty() ? QStringLiteral("failed to update MCP HTTP") : error);
        const auto cfg = engine.getMcpHttpConfig();
        return { false, QJsonObject{
            { "enabled", cfg.enabled },
            { "host", cfg.host },
            { "port", static_cast<int>(cfg.port) },
            { "running", cfg.running },
            { "lastError", cfg.lastError },
        } };
    }

    // RAVE #5: persisted RAVE config (model dirs, default model, python/script
    // paths, timeout). Read/write QSettings rave/* via the RaveService helpers
    // shared with the rave_get_config / rave_set_config MCP tools so both
    // surfaces return the identical JSON shape.
    if (m == "getRaveConfig") {
        return { false, HDAW::RaveService::persistedConfigJson() };
    }
    if (m == "setRaveConfig") {
        QString error;
        if (!HDAW::RaveService::savePersistedConfig(o, &error))
            return makeError(-32602, error.isEmpty() ? QStringLiteral("invalid RAVE config") : error);
        return { false, QJsonValue::Null };
    }

    return makeError(-32601, "unknown settings method: " + m);
}

// ── Routes that need engine context (the ProjectModel), not just the command
// interface. FrontendRouter routes these two methods here BEFORE dispatchProject
// — the same engine-context precedent as project.addFxSlot (the pluginId gate)
// and project.importMidiFile. Each one runs the SHARED body its MCP twin runs,
// so the surfaces cannot drift:
//   removeTrack     — common/TrackRemoveGuard.h  (MCP remove_track)
//   addTrackWithFx  — common/AddTrackWithFx.h    (MCP add_track_with_fx)

// project.removeTrack {trackId, dryRun?, force?} — the MCP remove_track contract
// (which this route did not have: it destroyed a track's clips with no preview
// and no refusal). `dryRun:true` answers the identical preview text as a bare
// JSON string and mutates nothing; a track that still carries clips refuses with
// the identical text as the -32602 message unless `force:true`. An out-of-range
// trackId keeps this route's long-standing NON-error contract and reports
// ok:false (the pre-existing failure-shape split with the tool's "track not
// found" — unchanged, and the guard simply does not fire for a missing track).
DispatchResult dispatchRemoveTrack(AudioEngine& engine, const QJsonValue& params)
{
    auto& c = engine.getProjectCommands();
    const auto o = paramsObject(params);
    // B2: the track may be named by its stable `trackID` instead of the
    // positional `trackId`. The guard below then runs against the RESOLVED
    // index, so a preview/refusal names the track the caller actually meant.
    int i; DispatchResult err;
    if (!trackIndexArg(o, engine.getProjectModel().getTrackListTree(), i, &err)) return err;

    const auto guard = HDAW::inspectTrackForRemoval(engine.getProjectModel(), i);
    if (guard.found)
    {
        if (optBool(o, "dryRun", false, nullptr))
            return { false, QString::fromStdString(HDAW::trackRemovalDryRunText(i, guard)) };
        if (guard.clipCount > 0 && !optBool(o, "force", false, nullptr))
            return makeError(-32602,
                             QString::fromStdString(HDAW::trackRemovalRefusalText(i, guard)));
    }

    // Shift payload, byte-identical to MCP remove_track's text payload
    // (AGENTS.md parity; the command owns the shape — see
    // common/ProjectCommands.h TrackRemovalResult):
    //   {"ok":true,"removed":<oldIndex>,"shifted":[{"from":N,"to":N-1},...]}
    // `shifted` is [] when the last track was removed.
    const auto r = c.removeTrack(i);
    QJsonArray shifted;
    for (const auto& p : r.shifted)
        shifted.append(QJsonObject{ { "from", p.first }, { "to", p.second } });
    return { false, QJsonObject{ { "ok", r.ok }, { "removed", r.removed },
                                 { "shifted", shifted } } };
}

// project.addTrackWithFx {name, fxType?, pluginId?, color?, parentBus?} — the
// RPC twin MCP add_track_with_fx never had (rpc_parity_map.inc: "unresolved").
// Argument names are the tool's property names, and the payload IS the tool's
// text (parsed here), so the twin test compares them value for value; a refused
// pluginId comes back as -32602 with the identical HDAW::fxPluginIdError text
// and leaves NO track behind (the gate runs before the track exists).
DispatchResult dispatchAddTrackWithFx(AudioEngine& engine, const QJsonValue& params)
{
    const auto o = paramsObject(params);
    std::string name;
    if (!requireString(o, "name", name, nullptr)) return makeError(-32602, "name required");
    const auto r = HDAW::addTrackWithFx(engine.getProjectModel(),
                                        name,
                                        optString(o, "fxType", ""),
                                        optString(o, "pluginId", ""),
                                        optInt<int>(o, "color", -1, nullptr),
                                        optInt<int>(o, "parentBus", 0, nullptr));
    if (!r.ok) return makeError(-32602, QString::fromStdString(r.error));
    return { false, QJsonDocument::fromJson(QString::fromStdString(
                 HDAW::shapeAddTrackWithFxJson(r)).toUtf8()).object() };
}

} // namespace frontend
