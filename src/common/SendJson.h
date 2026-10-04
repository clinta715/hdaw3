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
//                       send, in SEND_LIST order:
//     [{"sendIndex":0,"level":0.5,"isPreFader":false,"bypassed":false,"sendID":1},
//      {"sendIndex":1,"level":1,"isPreFader":true,"bypassed":false,"sendID":2}]
//                       `sendIndex` stays POSITIONAL (it is the argument every
//                       send tool takes and it renumbers when a lower send is
//                       removed); `sendID` is the STABLE identity (design B1) —
//                       minted at creation from the tree, untouched by
//                       removeSend. Read a send's identity from sendID, its
//                       address from sendIndex.
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
//                       Compressor-sidechain v1: a slot whose fxType is
//                       "compressor" ALSO carries the three sidechain fields
//                       set_fx_sidechain writes, under that tool's own argument
//                       names — sidechainSource (int STABLE source trackID, 0 =
//                       none/unset), sidechainLevel (number) and sidechainEnabled
//                       (bool). No other type emits them (a sidechain belongs to a
//                       compressor's detector alone, and the fields would read as
//                       noise on the rest of the chain):
//     [{"slotIndex":0,"fxType":"compressor","paramCount":4,"bypassed":false,
//       "sidechainSource":1,"sidechainLevel":0.5,"sidechainEnabled":true}]
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
        o->setProperty("sendID", s.sendID);
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
        // Compressor-sidechain v1: the ONLY slots that can carry a sidechain
        // expose the three fields set_fx_sidechain writes — the exact key names
        // and JSON types of that tool's argument vocabulary. Every other type
        // keeps the row it always had (a sidechain would read as noise, and
        // only a compressor's detector is driven by one).
        if (s.fxType == "compressor")
        {
            o->setProperty("sidechainSource", s.sidechainSource);
            o->setProperty("sidechainLevel", static_cast<double>(s.sidechainLevel));
            o->setProperty("sidechainEnabled", s.sidechainEnabled);
        }
        o->setProperty("bypassed", s.bypassed);
        arr.add(juce::var(o.get()));
    }
    return juce::JSON::toString(juce::var(arr), true).toStdString();
}

} // namespace HDAW
