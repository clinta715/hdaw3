#pragma once
// add_track_with_fx — the ONE composite body behind both surfaces.
//
// The MCP tool `add_track_with_fx` existed with NO RPC twin (rpc_parity_map.inc:
// "unresolved — no name-derived route"), so the frontend/agent RPC path had to
// compose add_track + add_fx by hand and could not reach the composite's
// pluginId gate at all. Both surfaces now call THIS function:
//
//   - MCP `add_track_with_fx`      — src/mcp/McpTools_Track.cpp
//   - RPC `project.addTrackWithFx` — dispatchAddTrackWithFx in
//     src/frontend/router/Router_Project.cpp (FrontendRouter routes the method
//     there because the gate needs the ProjectModel, not just ProjectCommands)
//
// so the gate order, the created track's shape, the inferred fxType and the
// returned payload cannot drift apart (AGENTS.md parity: identical by
// construction).
//
// Header-only (all inline), so no CMake source registration is needed. It
// includes the model header for the IDs:: identifiers and ProjectModel's
// addFxSlot — the tree schema and the FX-slot creator are the single source of
// truth, so this body never writes an FX_CHAIN child by hand.
//
// Payload grammar (frozen — the surface twin tests compare it value for value):
//
//   shapeAddTrackWithFxJson  {"trackId":3,"routed":1,"trackID":4,"fxType":"eq"}
//       Same creation shape as add_track (TrackJson.h), plus `fxType` echoed
//       because it is INFERRED ("plugin") when only pluginId was given.
//       `trackID` is the created track's STABLE id (design B1) — an identity
//       that survives moveTrack/removeTrack, where `trackId` is a position.
//
//   the refusal            the pluginId gate's text, verbatim
//       (HDAW::fxPluginIdError, src/common/FxPluginIdCheck.h) — MCP answers it
//       as tool text, RPC as the -32602 message.

#include "../model/ProjectModel.h"
#include "FxPluginIdCheck.h"
#include "TrackJson.h"

#include <string>

namespace HDAW {

struct AddTrackWithFxResult
{
    bool ok = true;            // false => `error` carries the shared gate text
    std::string error;         // non-empty exactly when ok == false
    int trackId = -1;          // the new track's TRACK_LIST index
    int trackID = 0;           // the new track's STABLE id (0 = nothing created)
    int trackCount = 0;        // TRACK_LIST size AFTER the insertion (routed flag)
    std::string fxType;        // echoed; inferred "plugin" when only pluginId was given
};

// Create a track (name / color / parentBus / empty CLIP_LIST + FX_CHAIN +
// automation list) and, when an fxType or a pluginId was given, one FX slot.
// `color` < 0 means "next palette colour" (ProjectModel::trackColorForIndex of
// the new index) — the add_track convention.
//
// The pluginId gate runs BEFORE the track exists: a shadow-edition or
// unresolvable id must not silently slot a 'none' placeholder through this
// composite (item-1 hole). The SAME shared validator as add_fx formats the
// text, so both surfaces fail byte-identically. Empty pluginId (a track without
// a plugin) passes untouched — the add_fx accept rules.
inline AddTrackWithFxResult addTrackWithFx(ProjectModel& model,
                                           const std::string& name,
                                           const std::string& fxType,
                                           const std::string& pluginId,
                                           int color,
                                           int parentBus)
{
    AddTrackWithFxResult r;
    if (const auto err = fxPluginIdError(pluginId, model); !err.empty())
    {
        r.ok = false;
        r.error = err;
        return r;
    }

    auto& um = model.getUndoManager();
    const int idx = model.getTrackListTree().getNumChildren();

    juce::ValueTree t(IDs::TRACK);
    // Stable identity, minted before the node is appended — the same
    // tree-derived allocator every other TRACK constructor uses (design B1).
    t.setProperty(IDs::trackID, model.allocateTrackID(), &um);
    t.setProperty(IDs::name, juce::String(name), &um);
    t.setProperty(IDs::volume, 0.85, &um);
    t.setProperty(IDs::pan, 0.0, &um);
    t.setProperty(IDs::isMuted, false, &um);
    t.setProperty(IDs::isSoloed, false, &um);
    t.setProperty(IDs::parentBus, parentBus, &um);
    t.setProperty(IDs::color,
                  color < 0 ? static_cast<int>(ProjectModel::trackColorForIndex(idx)) : color,
                  &um);
    t.addChild(juce::ValueTree(IDs::CLIP_LIST), -1, &um);
    t.addChild(juce::ValueTree(IDs::FX_CHAIN), -1, &um);
    t.addChild(ProjectModel::createTrackAutomationList(), -1, &um);
    model.getTrackListTree().addChild(t, -1, &um);

    r.fxType = fxType;
    if (r.fxType.empty() && !pluginId.empty()) r.fxType = "plugin";
    if (!r.fxType.empty())
        model.addFxSlot(idx, r.fxType, -1, pluginId);

    r.trackId = idx;
    // Read the id back off the node that was just inserted (never re-derived):
    // the payload then describes the tree the caller asked to mutate.
    r.trackID = static_cast<int>(t.getProperty(IDs::trackID, 0));
    r.trackCount = model.getTrackListTree().getNumChildren();
    return r;
}

// Compact single-line JSON — see the grammar note above.
inline std::string shapeAddTrackWithFxJson(const AddTrackWithFxResult& r)
{
    juce::DynamicObject::Ptr o = new juce::DynamicObject();
    o->setProperty("trackId", r.trackId);
    o->setProperty("trackID", r.trackID);
    o->setProperty("routed", trackRoutedFlag(r.trackId, r.trackCount));
    o->setProperty("fxType", juce::String(r.fxType));
    return juce::JSON::toString(juce::var(o.get()), true).toStdString();
}

} // namespace HDAW
