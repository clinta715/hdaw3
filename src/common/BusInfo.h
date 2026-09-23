#pragma once
// Bus read shaping — shared by the MCP tool layer and the RPC router so that
// list_buses / list_bus_fx_params return THE SAME payload by construction:
// parity is a property of this file, not of two hand-written serializers.
//
// Header-only (all inline), so no CMake source registration is needed. It
// includes the model header for the IDs:: identifiers — the tree schema is the
// single source of truth for property names.
//
// Payload grammar (frozen — the surface twin tests compare it byte for byte):
//
//   shapeBusesJson      BARE array (no wrapper, like list_tracks), one object
//                       per bus, sorted by busID:
//     [{"busID":0,"name":"Master","busType":"master","fxType":"","busTarget":0},
//      {"busID":1,"name":"Reverb","busType":"fx","fxType":"reverb","busTarget":0}]
//
//   shapeBusFxParamsJson  one object for ONE bus. The param fields are named
//                       exactly like list_fx_params's (same parameter space,
//                       same vocabulary), "value" being the durable tree
//                       value and falling back to the def default:
//     {"busID":1,"name":"Reverb","busType":"fx","fxType":"reverb",
//      "params":[{"index":0,"name":"Room Size","minValue":0,"maxValue":1,
//                 "defaultValue":0.5,"value":0.5}, ...]}
//
// Plan: docs/plans/2026-09-22-bus-fx-params.md (slice C1).
#include "../model/ProjectModel.h"
#include "BusFxDefs.h"
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace HDAW {

struct BusInfo
{
    int busID = -1;
    std::string name, busType, fxType;
    int busTarget = 0;
};

// The BUS node with this busID, or an invalid tree when there is none. busID
// is the bus identity (tree position shifts when a bus is removed).
inline juce::ValueTree findBusNode(const juce::ValueTree& busList, int busID)
{
    if (! busList.isValid()) return {};
    for (int i = 0; i < busList.getNumChildren(); ++i)
        if (static_cast<int>(busList.getChild(i).getProperty(IDs::busID, -1)) == busID)
            return busList.getChild(i);
    return {};
}

// Every BUS child of the BUS_LIST node, sorted by busID so the payload is
// stable regardless of the order the tree happens to hold.
inline std::vector<BusInfo> readBuses(const juce::ValueTree& busList)
{
    std::vector<BusInfo> out;
    if (! busList.isValid()) return out;
    out.reserve(static_cast<size_t>(busList.getNumChildren()));
    for (int i = 0; i < busList.getNumChildren(); ++i)
    {
        auto bus = busList.getChild(i);
        BusInfo info;
        info.busID    = static_cast<int>(bus.getProperty(IDs::busID, -1));
        info.name     = bus.getProperty(IDs::name, "").toString().toStdString();
        info.busType  = bus.getProperty(IDs::busType, "").toString().toStdString();
        info.fxType   = bus.getProperty(IDs::fxType, "").toString().toStdString();
        info.busTarget = static_cast<int>(bus.getProperty(IDs::busTarget, 0));
        out.push_back(std::move(info));
    }
    std::sort(out.begin(), out.end(),
              [](const BusInfo& a, const BusInfo& b) { return a.busID < b.busID; });
    return out;
}

// Read-path resolution shared by both surfaces (list_bus_fx_params): the BUS
// node plus, when it cannot be read, the ready-to-report reason. `error` is
// empty exactly when `bus` is valid, so callers forward the text verbatim
// instead of inventing their own (identical failure text on both surfaces).
struct BusFxReadTarget
{
    juce::ValueTree bus;
    std::string error;
};

inline BusFxReadTarget findFxBusForRead(const juce::ValueTree& busList, int busID)
{
    BusFxReadTarget out;
    auto bus = findBusNode(busList, busID);
    if (! bus.isValid())
    {
        out.error = "no bus with id " + std::to_string(busID);
        return out;
    }
    const juce::String busType = bus.getProperty(IDs::busType, "").toString();
    if (busType != "fx")
    {
        out.error = "bus " + std::to_string(busID) + " is not an fx bus (busType \""
                    + busType.toStdString() + "\")";
        return out;
    }
    const juce::String fxType = bus.getProperty(IDs::fxType, "").toString();
    if (busFxParamDefs(fxType).empty())
    {
        out.error = "bus " + std::to_string(busID) + " has unsupported fxType \""
                    + fxType.toStdString() + "\" (expected one of: "
                    + busFxTypesText() + ")";
        return out;
    }
    out.bus = bus;
    return out;
}

// Compact single-line JSON (the house style for tool payloads: list_tracks,
// list_clips, ... all emit one line).
inline std::string shapeBusesJson(const std::vector<BusInfo>& buses)
{
    juce::Array<juce::var> arr;
    arr.ensureStorageAllocated(static_cast<int>(buses.size()));
    for (const auto& b : buses)
    {
        juce::DynamicObject::Ptr o = new juce::DynamicObject();
        o->setProperty("busID", b.busID);
        o->setProperty("name", juce::String(b.name));
        o->setProperty("busType", juce::String(b.busType));
        o->setProperty("fxType", juce::String(b.fxType));
        o->setProperty("busTarget", b.busTarget);
        arr.add(juce::var(o.get()));
    }
    return juce::JSON::toString(juce::var(arr), true).toStdString();
}

// The defs the bus's DSP honors (BusFxDefs.h) plus the current value of each:
// the tree's param_<i> when present, else the def default — the same
// defaulting FxBusProcessor::applyFromTree uses, so the readback always
// describes what the processor will actually run. An unknown/absent fxType
// yields an empty "params" array rather than fake defs.
inline std::string shapeBusFxParamsJson(const juce::ValueTree& busTree)
{
    juce::DynamicObject::Ptr o = new juce::DynamicObject();
    juce::Array<juce::var> params;
    const juce::String fxType = busTree.getProperty(IDs::fxType, "").toString();
    const auto& defs = busFxParamDefs(fxType);
    for (int i = 0; i < static_cast<int>(defs.size()); ++i)
    {
        const auto& d = defs[static_cast<size_t>(i)];
        juce::DynamicObject::Ptr p = new juce::DynamicObject();
        p->setProperty("index", i);
        p->setProperty("name", juce::String(d.name));
        p->setProperty("minValue", static_cast<double>(d.min));
        p->setProperty("maxValue", static_cast<double>(d.max));
        p->setProperty("defaultValue", static_cast<double>(d.def));
        p->setProperty("value", static_cast<double>(
            busTree.getProperty("param_" + juce::String(i), static_cast<double>(d.def))));
        params.add(juce::var(p.get()));
    }
    o->setProperty("busID", static_cast<int>(busTree.getProperty(IDs::busID, -1)));
    o->setProperty("name", busTree.getProperty(IDs::name, "").toString());
    o->setProperty("busType", busTree.getProperty(IDs::busType, "").toString());
    o->setProperty("fxType", fxType);
    o->setProperty("params", params);
    return juce::JSON::toString(juce::var(o.get()), true).toStdString();
}

} // namespace HDAW
