#pragma once
// Send / FX-slot read shaping — shared by the MCP tool layer and the RPC router so
// that get_track_sends / read.getTrackSends and list_fx / read.getFxSlots return THE
// SAME payload by construction: parity is a property of this file, not of two
// hand-written serializers.
//
// Header-only (all inline), so no CMake source registration is needed.
//
// Payload grammar (frozen — the surface twin tests compare it value for value):
//
//   shapeSendsJson      BARE array (no wrapper, like list_tracks), one object per
//                       send, in SEND_LIST order (position IS a send's identity):
//     [{"sendIndex":0,"level":0.5,"isPreFader":false,"bypassed":false},
//      {"sendIndex":1,"level":1,"isPreFader":true,"bypassed":false}]
//
//   shapeFxSlotsJson    BARE array, one object per FX slot, in chain order. The
//                       vocabulary is the one the FX tools already speak: slotIndex /
//                       fxType are the argument names list_fx_params, set_fx_param and
//                       add_fx take, and BusInfo.h's bus rows carry fxType too (the
//                       `list_fx` output's old `slot` / `type` keys contradicted the
//                       tool family's own contract — see the evidence note below).
//                       `paramCount` is carried by EVERY slot (internal = defs-table
//                       size, plugin = live instance count, 0 = not loaded); only the
//                       plugin IDENTITY fields (pluginId / pluginName / pluginFormat)
//                       are emitted for a slot whose fxType is "plugin" — an internal
//                       FX slot has no plugin behind it (it carries no `name`, no
//                       `pluginID`), so reporting empty plugin fields would read as a
//                       loaded plugin with an empty id:
//     [{"slotIndex":0,"fxType":"eq","paramCount":3,"bypassed":false},
//      {"slotIndex":1,"fxType":"plugin","pluginId":"/x.vst3","pluginName":"Delay",
//       "pluginFormat":"VST3","paramCount":4,"bypassed":false}]
//
// Evidence for the FX-slot vocabulary (2026-09-23): the RPC shape (slotIndex / fxType /
// pluginId / pluginName / pluginFormat / bypassed / paramCount) is what the live
// consumers read — tests/unit/frontend/frontend_server_test.cpp locates slots by
// fxType + slotIndex, and the (now deprecated) reference frontend's FxSlotSnapshot
// interface + FXChain.tsx render pluginName. The MCP shape's `slot` / `type` had one
// live consumer (mcp_functionality_test.cpp asserts `type == "eq"`) and one archived
// script. The tie-breaker is AGENTS.md's "argument names are part of the contract":
// every FX tool takes `slotIndex` / `fxType`, so `type` was an outlier even inside
// MCP. Both surfaces now emit the RPC vocabulary, with the MCP side's conditionality.
#include <juce_core/juce_core.h>

#include "ReadModel.h"

#include <string>
#include <vector>

namespace HDAW {

// Compact single-line JSON (the house style for tool payloads: list_tracks,
// list_clips, list_buses ... all emit one line).
inline std::string shapeSendsJson(const std::vector<SendSnapshot>& sends)
{
    juce::Array<juce::var> arr;
    arr.ensureStorageAllocated(static_cast<int>(sends.size()));
    for (const auto& s : sends)
    {
        juce::DynamicObject::Ptr o = new juce::DynamicObject();
        o->setProperty("sendIndex", s.sendIndex);
        o->setProperty("level", static_cast<double>(s.level));
        o->setProperty("isPreFader", s.isPreFader);
        o->setProperty("bypassed", s.bypassed);
        arr.add(juce::var(o.get()));
    }
    return juce::JSON::toString(juce::var(arr), true).toStdString();
}

// NB: the parameter is named `fxSlots`, NOT `slots` — Qt's qtmetamacros.h defines
// `slots` (and `signals`/`emit`/`foreach`) as object-like macros, which erase the
// identifier in every Qt-including TU (the router and several MCP tools). Naming it
// `slots` produced `syntax error: '.'` from `slots.size()` in this header — see
// docs/pitfalls-juce.md, and the same note at Router_Rave.cpp.
inline std::string shapeFxSlotsJson(const std::vector<FxSlotSnapshot>& fxSlots)
{
    juce::Array<juce::var> arr;
    arr.ensureStorageAllocated(static_cast<int>(fxSlots.size()));
    for (const auto& s : fxSlots)
    {
        juce::DynamicObject::Ptr o = new juce::DynamicObject();
        o->setProperty("slotIndex", s.slotIndex);
        o->setProperty("fxType", juce::String(s.fxType));
        // paramCount is meaningful for BOTH kinds — an internal slot reports its
        // defs-table size (TrackFXSlot::paramCount, the 2026-09-23 fix that made it
        // a real "the slot has params" readback), a plugin slot its live instance
        // count, and 0 means "instance not loaded". Only the plugin IDENTITY
        // fields are plugin-only: an internal slot has no plugin behind it, so
        // empty ids would read as a loaded plugin with an empty id.
        o->setProperty("paramCount", s.paramCount);
        if (s.fxType == "plugin")
        {
            o->setProperty("pluginId", juce::String(s.pluginId));
            o->setProperty("pluginName", juce::String(s.pluginName));
            o->setProperty("pluginFormat", juce::String(s.pluginFormat));
        }
        o->setProperty("bypassed", s.bypassed);
        arr.add(juce::var(o.get()));
    }
    return juce::JSON::toString(juce::var(arr), true).toStdString();
}

} // namespace HDAW
