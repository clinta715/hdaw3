# Arranger Track Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement a Cubase-style arranger track — named timeline regions, chain editor, non-linear playback, multiple chains, and flatten.

**Architecture:** ValueTree-native data model (ARRANGER_LIST + ARRANGER_CHAIN_LIST under project root), following the exact marker system pattern. Commands via ProjectCommands interface, ReadModel for snapshots, RPC routes for frontend, Zustand store for state, dedicated timeline lane + bottom panel tab for UI, modified processBlock for arranger-mode playback.

**Tech Stack:** C++ (JUCE ValueTree, AudioProcessorGraph), TypeScript (React 19, Zustand, Vite), JSON-RPC 2.0 over WebSocket.

**Spec:** `docs/superpowers/specs/2026-08-10-arranger-track-design.md`

---

## File Map

### New Files
| File | Purpose |
|------|---------|
| `src/engine/AudioEngineCommands_Arranger.cpp` | Command implementations (14 commands) |
| `frontend/src/store/arrangerStore.ts` | Zustand store for regions + chains |
| `frontend/src/components/ArrangerLane.tsx` | Timeline arranger lane component |
| `frontend/src/components/ArrangerLane.css` | Arranger lane styles |
| `frontend/src/components/ArrangerChainEditor.tsx` | Bottom panel chain editor tab |
| `frontend/src/components/ArrangerChainEditor.css` | Chain editor styles |
| `tests/arranger_test.cpp` | C++ gtest suite |

### Modified Files
| File | Changes |
|------|---------|
| `src/model/ProjectModel.h` | Add 12 DECLARE_ID entries |
| `src/common/ProjectCommands.h` | Add 14 pure virtual methods |
| `src/engine/AudioEngineCommands.h` | Add 14 override declarations |
| `src/common/ReadModel.h` | Add 3 snapshot structs + 2 virtual methods |
| `src/engine/ReadModelImpl.h` | Add 2 override declarations |
| `src/engine/ReadModelImpl.cpp` | Implement getArrangerRegions/getArrangerChains |
| `src/frontend/FrontendRpc.h` | Add toJson for 3 structs, update namespace list |
| `src/frontend/router/Router_Project.cpp` | Add 14 route dispatches |
| `src/frontend/router/Router_Read.cpp` | Add 2 read routes |
| `src/frontend/TreeDeltaAccumulator.cpp` | No change needed (already escalates non-clip/track to fullSync) |
| `src/engine/TransportManager.h` | Add arranger atomics |
| `src/engine/MainAudioProcessor.cpp` | Add arranger-mode logic to processBlock |
| `frontend/src/store/projectStore.ts` | Wire arranger sync on fullSync |
| `frontend/src/components/BottomTabs.tsx` | Add Arranger tab |
| `frontend/src/components/TimelineMinimal/TimelineMinimal.tsx` | Add arranger lane |
| `frontend/src/utils/tabRegistry.ts` | Register arranger tab (if exists) |

---

## Task 1: ValueTree IDs

**Files:**
- Modify: `src/model/ProjectModel.h:80-90`

- [ ] **Step 1: Add arranger ID declarations**

After the marker IDs block (line ~85), add:

```cpp
// Arranger Regions (named timeline sections)
DECLARE_ID(ARRANGER_LIST)
DECLARE_ID(ARRANGER_REGION)
DECLARE_ID(regionID)
DECLARE_ID(regionName)
DECLARE_ID(startTime)
DECLARE_ID(duration)

// Arranger Chains (playback order)
DECLARE_ID(ARRANGER_CHAIN_LIST)
DECLARE_ID(ARRANGER_CHAIN)
DECLARE_ID(chainID)
DECLARE_ID(chainName)
DECLARE_ID(isActive)
DECLARE_ID(CHAIN_ENTRY)
DECLARE_ID(repeatCount)
```

Note: `startTime` and `duration` may already exist in the IDs namespace (used by clips). If so, reuse them — don't declare duplicates. Check with grep first.

- [ ] **Step 2: Verify build compiles**

Run: `cmake --build build --config Debug`
Expected: Compiles without errors (new IDs are unused, no conflicts).

- [ ] **Step 3: Commit**

```bash
git add src/model/ProjectModel.h
git commit -m "feat(arranger): add ValueTree ID declarations"
```

---

## Task 2: Command Interface

**Files:**
- Modify: `src/common/ProjectCommands.h:190-200`

- [ ] **Step 1: Add arranger command declarations**

After the marker commands block, add:

```cpp
// Arranger Regions
virtual std::string addArrangerRegion(const std::string& name, double startTime, double duration, int color = 0xFFd97706) = 0;
virtual void removeArrangerRegion(const std::string& regionID) = 0;
virtual void setArrangerRegionName(const std::string& regionID, const std::string& name) = 0;
virtual void setArrangerRegionBounds(const std::string& regionID, double startTime, double duration) = 0;
virtual void setArrangerRegionColor(const std::string& regionID, int color) = 0;

// Arranger Chains
virtual std::string addArrangerChain(const std::string& name) = 0;
virtual void removeArrangerChain(const std::string& chainID) = 0;
virtual void setArrangerChainName(const std::string& chainID, const std::string& name) = 0;
virtual void setArrangerChainActive(const std::string& chainID) = 0;

// Chain Entries
virtual int addChainEntry(const std::string& chainID, const std::string& regionID, int repeatCount = 1) = 0;
virtual void removeChainEntry(const std::string& chainID, int entryIndex) = 0;
virtual void reorderChainEntry(const std::string& chainID, int fromIndex, int toIndex) = 0;
virtual void setChainEntryRepeat(const std::string& chainID, int entryIndex, int repeatCount) = 0;

// Flatten
virtual void flattenArranger() = 0;
```

- [ ] **Step 2: Add override declarations to AudioEngineCommands.h**

In `src/engine/AudioEngineCommands.h`, in the public section after the marker overrides:

```cpp
// ProjectCommands — Arranger Regions
std::string addArrangerRegion(const std::string& name, double startTime, double duration, int color) override;
void removeArrangerRegion(const std::string& regionID) override;
void setArrangerRegionName(const std::string& regionID, const std::string& name) override;
void setArrangerRegionBounds(const std::string& regionID, double startTime, double duration) override;
void setArrangerRegionColor(const std::string& regionID, int color) override;

// ProjectCommands — Arranger Chains
std::string addArrangerChain(const std::string& name) override;
void removeArrangerChain(const std::string& chainID) override;
void setArrangerChainName(const std::string& chainID, const std::string& name) override;
void setArrangerChainActive(const std::string& chainID) override;

// ProjectCommands — Chain Entries
int addChainEntry(const std::string& chainID, const std::string& regionID, int repeatCount) override;
void removeChainEntry(const std::string& chainID, int entryIndex) override;
void reorderChainEntry(const std::string& chainID, int fromIndex, int toIndex) override;
void setChainEntryRepeat(const std::string& chainID, int entryIndex, int repeatCount) override;

// ProjectCommands — Flatten
void flattenArranger() override;
```

- [ ] **Step 3: Verify build compiles (linker will fail — no implementations yet)**

Run: `cmake --build build --config Debug`
Expected: Linker errors for unimplemented arranger methods (expected — implementations come next).

- [ ] **Step 4: Commit**

```bash
git add src/common/ProjectCommands.h src/engine/AudioEngineCommands.h
git commit -m "feat(arranger): add command interface declarations"
```

---

## Task 3: Command Implementations

**Files:**
- Create: `src/engine/AudioEngineCommands_Arranger.cpp`
- Modify: `CMakeLists.txt` (add new .cpp to build)

- [ ] **Step 1: Create AudioEngineCommands_Arranger.cpp**

```cpp
#include "AudioEngineCommands.h"
#include "../model/ProjectModel.h"
#include <juce_core/juce_core.h>

static juce::String generateID()
{
    return juce::Uuid().toString();
}

static juce::ValueTree findRegionByID(juce::ValueTree arrangerList, const juce::String& id)
{
    for (int i = 0; i < arrangerList.getNumChildren(); ++i)
    {
        auto child = arrangerList.getChild(i);
        if (child.getProperty(IDs::regionID, "") == id)
            return child;
    }
    return {};
}

static juce::ValueTree findChainByID(juce::ValueTree chainList, const juce::String& id)
{
    for (int i = 0; i < chainList.getNumChildren(); ++i)
    {
        auto child = chainList.getChild(i);
        if (child.getProperty(IDs::chainID, "") == id)
            return child;
    }
    return {};
}

// --- Arranger Regions ---

std::string AudioEngineCommands::addArrangerRegion(const std::string& name, double start, double dur, int color)
{
    auto& um = model_.getUndoManager();
    auto root = model_.getTree();
    auto arrangerList = root.getChildWithName(IDs::ARRANGER_LIST);
    if (!arrangerList.isValid())
    {
        arrangerList = { IDs::ARRANGER_LIST, {} };
        root.addChild(arrangerList, -1, &um);
    }

    juce::String id = generateID();
    juce::ValueTree region { IDs::ARRANGER_REGION };
    region.setProperty(IDs::regionID, id, &um);
    region.setProperty(IDs::regionName, juce::String(name), &um);
    region.setProperty(IDs::startTime, start, &um);
    region.setProperty(IDs::duration, dur, &um);
    region.setProperty(IDs::color, color, &um);
    arrangerList.addChild(region, -1, &um);
    return id.toStdString();
}

void AudioEngineCommands::removeArrangerRegion(const std::string& rid)
{
    auto& um = model_.getUndoManager();
    auto root = model_.getTree();
    auto arrangerList = root.getChildWithName(IDs::ARRANGER_LIST);
    if (!arrangerList.isValid()) return;

    auto region = findRegionByID(arrangerList, juce::String(rid));
    if (region.isValid())
        arrangerList.removeChild(region, &um);

    // Cascade: remove chain entries referencing this region
    auto chainList = root.getChildWithName(IDs::ARRANGER_CHAIN_LIST);
    if (!chainList.isValid()) return;
    for (int c = 0; c < chainList.getNumChildren(); ++c)
    {
        auto chain = chainList.getChild(c);
        for (int e = chain.getNumChildren() - 1; e >= 0; --e)
        {
            auto entry = chain.getChild(e);
            if (entry.getProperty(IDs::regionID, "") == juce::String(rid))
                chain.removeChild(entry, &um);
        }
    }
}

void AudioEngineCommands::setArrangerRegionName(const std::string& rid, const std::string& name)
{
    auto& um = model_.getUndoManager();
    auto arrangerList = model_.getTree().getChildWithName(IDs::ARRANGER_LIST);
    if (!arrangerList.isValid()) return;
    auto region = findRegionByID(arrangerList, juce::String(rid));
    if (region.isValid())
        region.setProperty(IDs::regionName, juce::String(name), &um);
}

void AudioEngineCommands::setArrangerRegionBounds(const std::string& rid, double start, double dur)
{
    auto& um = model_.getUndoManager();
    auto arrangerList = model_.getTree().getChildWithName(IDs::ARRANGER_LIST);
    if (!arrangerList.isValid()) return;
    auto region = findRegionByID(arrangerList, juce::String(rid));
    if (region.isValid())
    {
        region.setProperty(IDs::startTime, start, &um);
        region.setProperty(IDs::duration, dur, &um);
    }
}

void AudioEngineCommands::setArrangerRegionColor(const std::string& rid, int color)
{
    auto& um = model_.getUndoManager();
    auto arrangerList = model_.getTree().getChildWithName(IDs::ARRANGER_LIST);
    if (!arrangerList.isValid()) return;
    auto region = findRegionByID(arrangerList, juce::String(rid));
    if (region.isValid())
        region.setProperty(IDs::color, color, &um);
}

// --- Arranger Chains ---

std::string AudioEngineCommands::addArrangerChain(const std::string& name)
{
    auto& um = model_.getUndoManager();
    auto root = model_.getTree();
    auto chainList = root.getChildWithName(IDs::ARRANGER_CHAIN_LIST);
    if (!chainList.isValid())
    {
        chainList = { IDs::ARRANGER_CHAIN_LIST, {} };
        root.addChild(chainList, -1, &um);
    }

    juce::String id = generateID();
    bool isFirst = chainList.getNumChildren() == 0;

    juce::ValueTree chain { IDs::ARRANGER_CHAIN };
    chain.setProperty(IDs::chainID, id, &um);
    chain.setProperty(IDs::chainName, juce::String(name), &um);
    chain.setProperty(IDs::isActive, isFirst, &um); // auto-activate first chain
    chainList.addChild(chain, -1, &um);
    return id.toStdString();
}

void AudioEngineCommands::removeArrangerChain(const std::string& cid)
{
    auto& um = model_.getUndoManager();
    auto chainList = model_.getTree().getChildWithName(IDs::ARRANGER_CHAIN_LIST);
    if (!chainList.isValid()) return;

    auto chain = findChainByID(chainList, juce::String(cid));
    if (!chain.isValid()) return;

    bool wasActive = static_cast<bool>(chain.getProperty(IDs::isActive, false));
    chainList.removeChild(chain, &um);

    // If removed chain was active, activate another
    if (wasActive && chainList.getNumChildren() > 0)
        chainList.getChild(0).setProperty(IDs::isActive, true, &um);
}

void AudioEngineCommands::setArrangerChainName(const std::string& cid, const std::string& name)
{
    auto& um = model_.getUndoManager();
    auto chainList = model_.getTree().getChildWithName(IDs::ARRANGER_CHAIN_LIST);
    if (!chainList.isValid()) return;
    auto chain = findChainByID(chainList, juce::String(cid));
    if (chain.isValid())
        chain.setProperty(IDs::chainName, juce::String(name), &um);
}

void AudioEngineCommands::setArrangerChainActive(const std::string& cid)
{
    auto& um = model_.getUndoManager();
    auto chainList = model_.getTree().getChildWithName(IDs::ARRANGER_CHAIN_LIST);
    if (!chainList.isValid()) return;

    // Deactivate all
    for (int i = 0; i < chainList.getNumChildren(); ++i)
        chainList.getChild(i).setProperty(IDs::isActive, false, &um);

    // Activate target
    auto chain = findChainByID(chainList, juce::String(cid));
    if (chain.isValid())
        chain.setProperty(IDs::isActive, true, &um);
}

// --- Chain Entries ---

int AudioEngineCommands::addChainEntry(const std::string& cid, const std::string& rid, int repeatCount)
{
    auto& um = model_.getUndoManager();
    auto chainList = model_.getTree().getChildWithName(IDs::ARRANGER_CHAIN_LIST);
    if (!chainList.isValid()) return -1;
    auto chain = findChainByID(chainList, juce::String(cid));
    if (!chain.isValid()) return -1;

    juce::ValueTree entry { IDs::CHAIN_ENTRY };
    entry.setProperty(IDs::regionID, juce::String(rid), &um);
    entry.setProperty(IDs::repeatCount, juce::jmax(1, repeatCount), &um);
    int index = chain.getNumChildren();
    chain.addChild(entry, index, &um);
    return index;
}

void AudioEngineCommands::removeChainEntry(const std::string& cid, int entryIndex)
{
    auto& um = model_.getUndoManager();
    auto chainList = model_.getTree().getChildWithName(IDs::ARRANGER_CHAIN_LIST);
    if (!chainList.isValid()) return;
    auto chain = findChainByID(chainList, juce::String(cid));
    if (!chain.isValid()) return;
    if (entryIndex >= 0 && entryIndex < chain.getNumChildren())
        chain.removeChild(entryIndex, &um);
}

void AudioEngineCommands::reorderChainEntry(const std::string& cid, int fromIndex, int toIndex)
{
    auto& um = model_.getUndoManager();
    auto chainList = model_.getTree().getChildWithName(IDs::ARRANGER_CHAIN_LIST);
    if (!chainList.isValid()) return;
    auto chain = findChainByID(chainList, juce::String(cid));
    if (!chain.isValid()) return;
    if (fromIndex < 0 || fromIndex >= chain.getNumChildren()) return;
    if (toIndex < 0 || toIndex >= chain.getNumChildren()) return;

    auto entry = chain.getChild(fromIndex);
    chain.moveChild(fromIndex, toIndex, &um);
}

void AudioEngineCommands::setChainEntryRepeat(const std::string& cid, int entryIndex, int repeatCount)
{
    auto& um = model_.getUndoManager();
    auto chainList = model_.getTree().getChildWithName(IDs::ARRANGER_CHAIN_LIST);
    if (!chainList.isValid()) return;
    auto chain = findChainByID(chainList, juce::String(cid));
    if (!chain.isValid()) return;
    if (entryIndex >= 0 && entryIndex < chain.getNumChildren())
        chain.getChild(entryIndex).setProperty(IDs::repeatCount, juce::jmax(1, repeatCount), &um);
}

// --- Flatten ---

void AudioEngineCommands::flattenArranger()
{
    // TODO: Implement in Task 12 (Flatten)
    jassertfalse; // not yet implemented
}
```

- [ ] **Step 2: Add the new .cpp to CMakeLists.txt**

Find the source file list in `CMakeLists.txt` and add `src/engine/AudioEngineCommands_Arranger.cpp` alongside the existing `AudioEngineCommands_Markers.cpp`.

- [ ] **Step 3: Verify build compiles**

Run: `cmake --build build --config Debug`
Expected: Compiles (flattenArranger has jassertfalse but links).

- [ ] **Step 4: Commit**

```bash
git add src/engine/AudioEngineCommands_Arranger.cpp src/engine/AudioEngineCommands.h CMakeLists.txt
git commit -m "feat(arranger): implement command mutations"
```

---

## Task 4: ReadModel & RPC Routes

**Files:**
- Modify: `src/common/ReadModel.h`
- Modify: `src/engine/ReadModelImpl.h`
- Modify: `src/engine/ReadModelImpl.cpp`
- Modify: `src/frontend/FrontendRpc.h`
- Modify: `src/frontend/router/Router_Project.cpp`
- Modify: `src/frontend/router/Router_Read.cpp`

- [ ] **Step 1: Add snapshot structs to ReadModel.h**

After the `MarkerSnapshot` struct (line ~144), add:

```cpp
struct ArrangerRegionSnapshot {
    std::string regionID;
    std::string name;
    double startTime = 0.0;
    double duration = 0.0;
    int color = 0;
};

struct ChainEntrySnapshot {
    std::string regionID;
    int repeatCount = 1;
};

struct ArrangerChainSnapshot {
    std::string chainID;
    std::string name;
    bool isActive = false;
    std::vector<ChainEntrySnapshot> entries;
};
```

After the `getMarkers()` virtual (line ~217), add:

```cpp
virtual std::vector<ArrangerRegionSnapshot> getArrangerRegions() const = 0;
virtual std::vector<ArrangerChainSnapshot> getArrangerChains() const = 0;
```

- [ ] **Step 2: Add overrides to ReadModelImpl.h**

```cpp
std::vector<ArrangerRegionSnapshot> getArrangerRegions() const override;
std::vector<ArrangerChainSnapshot> getArrangerChains() const override;
```

- [ ] **Step 3: Implement in ReadModelImpl.cpp**

After the `getMarkers()` implementation:

```cpp
std::vector<ArrangerRegionSnapshot> ReadModelImpl::getArrangerRegions() const
{
    std::vector<ArrangerRegionSnapshot> result;
    auto arrangerList = model_.getTree().getChildWithName(IDs::ARRANGER_LIST);
    if (!arrangerList.isValid())
        return result;

    for (int i = 0; i < arrangerList.getNumChildren(); ++i)
    {
        auto region = arrangerList.getChild(i);
        ArrangerRegionSnapshot rs;
        rs.regionID = region.getProperty(IDs::regionID, "").toString().toStdString();
        rs.name = region.getProperty(IDs::regionName, "").toString().toStdString();
        rs.startTime = static_cast<double>(region.getProperty(IDs::startTime, 0.0));
        rs.duration = static_cast<double>(region.getProperty(IDs::duration, 0.0));
        rs.color = static_cast<int>(region.getProperty(IDs::color, 0));
        result.push_back(rs);
    }
    return result;
}

std::vector<ArrangerChainSnapshot> ReadModelImpl::getArrangerChains() const
{
    std::vector<ArrangerChainSnapshot> result;
    auto chainList = model_.getTree().getChildWithName(IDs::ARRANGER_CHAIN_LIST);
    if (!chainList.isValid())
        return result;

    for (int i = 0; i < chainList.getNumChildren(); ++i)
    {
        auto chain = chainList.getChild(i);
        ArrangerChainSnapshot cs;
        cs.chainID = chain.getProperty(IDs::chainID, "").toString().toStdString();
        cs.name = chain.getProperty(IDs::chainName, "").toString().toStdString();
        cs.isActive = static_cast<bool>(chain.getProperty(IDs::isActive, false));

        for (int e = 0; e < chain.getNumChildren(); ++e)
        {
            auto entry = chain.getChild(e);
            ChainEntrySnapshot es;
            es.regionID = entry.getProperty(IDs::regionID, "").toString().toStdString();
            es.repeatCount = static_cast<int>(entry.getProperty(IDs::repeatCount, 1));
            cs.entries.push_back(es);
        }
        result.push_back(cs);
    }
    return result;
}
```

- [ ] **Step 4: Add toJson serializers to FrontendRpc.h**

After the `toJson(MarkerSnapshot)` function:

```cpp
inline QJsonObject toJson(const ArrangerRegionSnapshot& r) {
    return QJsonObject{
        { "regionID",  QString::fromStdString(r.regionID) },
        { "name",      QString::fromStdString(r.name) },
        { "startTime", r.startTime },
        { "duration",  r.duration },
        { "color",     r.color },
    };
}

inline QJsonObject toJson(const ChainEntrySnapshot& e) {
    return QJsonObject{
        { "regionID",    QString::fromStdString(e.regionID) },
        { "repeatCount", e.repeatCount },
    };
}

inline QJsonObject toJson(const ArrangerChainSnapshot& c) {
    QJsonArray entries;
    for (const auto& e : c.entries)
        entries.append(toJson(e));
    return QJsonObject{
        { "chainID",  QString::fromStdString(c.chainID) },
        { "name",     QString::fromStdString(c.name) },
        { "isActive", c.isActive },
        { "entries",  entries },
    };
}
```

- [ ] **Step 5: Add write routes to Router_Project.cpp**

After the marker routes block, add:

```cpp
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
```

- [ ] **Step 6: Add read routes to Router_Read.cpp**

After the `getMarkers` route:

```cpp
if (m == "getArrangerRegions") {
    QJsonArray arr;
    for (const auto& rs : r.getArrangerRegions())
        arr.append(toJson(rs));
    return { false, arr };
}
if (m == "getArrangerChains") {
    QJsonArray arr;
    for (const auto& cs : r.getArrangerChains())
        arr.append(toJson(cs));
    return { false, arr };
}
```

- [ ] **Step 7: Verify build compiles**

Run: `cmake --build build --config Debug`
Expected: Compiles and links.

- [ ] **Step 8: Commit**

```bash
git add src/common/ReadModel.h src/engine/ReadModelImpl.h src/engine/ReadModelImpl.cpp src/frontend/FrontendRpc.h src/frontend/router/Router_Project.cpp src/frontend/router/Router_Read.cpp
git commit -m "feat(arranger): add ReadModel, RPC routes, and JSON serialization"
```

---

## Task 5: C++ Tests

**Files:**
- Create: `tests/arranger_test.cpp`
- Modify: `CMakeLists.txt` (add test file)

- [ ] **Step 1: Create arranger_test.cpp**

```cpp
#include <gtest/gtest.h>
#include "../src/model/ProjectModel.h"
#include "../src/engine/AudioEngineCommands.h"
#include "../src/engine/ReadModelImpl.h"

class ArrangerTest : public ::testing::Test {
protected:
    std::unique_ptr<ProjectModel> model;
    std::unique_ptr<AudioEngineCommands> commands;
    std::unique_ptr<ReadModelImpl> readModel;

    void SetUp() override {
        model = std::make_unique<ProjectModel>();
        model->createDefaultProject();
        commands = std::make_unique<AudioEngineCommands>(*model);
        readModel = std::make_unique<ReadModelImpl>(*model);
    }
};

// --- Region CRUD ---

TEST_F(ArrangerTest, AddRegion) {
    auto rid = commands->addArrangerRegion("Intro", 0.0, 8.0, 0xFF0000FF);
    EXPECT_FALSE(rid.empty());

    auto regions = readModel->getArrangerRegions();
    ASSERT_EQ(regions.size(), 1u);
    EXPECT_EQ(regions[0].name, "Intro");
    EXPECT_DOUBLE_EQ(regions[0].startTime, 0.0);
    EXPECT_DOUBLE_EQ(regions[0].duration, 8.0);
    EXPECT_EQ(regions[0].color, 0xFF0000FF);
}

TEST_F(ArrangerTest, RemoveRegion) {
    auto rid = commands->addArrangerRegion("Intro", 0.0, 8.0);
    commands->removeArrangerRegion(rid);
    EXPECT_TRUE(readModel->getArrangerRegions().empty());
}

TEST_F(ArrangerTest, RenameRegion) {
    auto rid = commands->addArrangerRegion("Intro", 0.0, 8.0);
    commands->setArrangerRegionName(rid, "Verse");
    auto regions = readModel->getArrangerRegions();
    EXPECT_EQ(regions[0].name, "Verse");
}

TEST_F(ArrangerTest, MoveRegion) {
    auto rid = commands->addArrangerRegion("Intro", 0.0, 8.0);
    commands->setArrangerRegionBounds(rid, 4.0, 12.0);
    auto regions = readModel->getArrangerRegions();
    EXPECT_DOUBLE_EQ(regions[0].startTime, 4.0);
    EXPECT_DOUBLE_EQ(regions[0].duration, 12.0);
}

TEST_F(ArrangerTest, RecolorRegion) {
    auto rid = commands->addArrangerRegion("Intro", 0.0, 8.0);
    commands->setArrangerRegionColor(rid, 0xFFFF0000);
    auto regions = readModel->getArrangerRegions();
    EXPECT_EQ(regions[0].color, 0xFFFF0000);
}

// --- Chain CRUD ---

TEST_F(ArrangerTest, AddChain) {
    auto cid = commands->addArrangerChain("Arrangement A");
    EXPECT_FALSE(cid.empty());

    auto chains = readModel->getArrangerChains();
    ASSERT_EQ(chains.size(), 1u);
    EXPECT_EQ(chains[0].name, "Arrangement A");
    EXPECT_TRUE(chains[0].isActive); // first chain auto-activates
}

TEST_F(ArrangerTest, RemoveChain) {
    auto cid = commands->addArrangerChain("A");
    commands->removeArrangerChain(cid);
    EXPECT_TRUE(readModel->getArrangerChains().empty());
}

TEST_F(ArrangerTest, SingleActiveChain) {
    auto cid1 = commands->addArrangerChain("A");
    auto cid2 = commands->addArrangerChain("B");
    commands->setArrangerChainActive(cid2);

    auto chains = readModel->getArrangerChains();
    EXPECT_FALSE(chains[0].isActive);
    EXPECT_TRUE(chains[1].isActive);
}

TEST_F(ArrangerTest, ActivateDeactivatesOthers) {
    auto cid1 = commands->addArrangerChain("A");
    auto cid2 = commands->addArrangerChain("B");
    commands->setArrangerChainActive(cid1);

    auto chains = readModel->getArrangerChains();
    EXPECT_TRUE(chains[0].isActive);
    EXPECT_FALSE(chains[1].isActive);
}

// --- Chain Entries ---

TEST_F(ArrangerTest, AddChainEntry) {
    auto rid = commands->addArrangerRegion("Verse", 8.0, 16.0);
    auto cid = commands->addArrangerChain("A");
    int idx = commands->addChainEntry(cid, rid, 2);
    EXPECT_EQ(idx, 0);

    auto chains = readModel->getArrangerChains();
    ASSERT_EQ(chains[0].entries.size(), 1u);
    EXPECT_EQ(chains[0].entries[0].regionID, rid);
    EXPECT_EQ(chains[0].entries[0].repeatCount, 2);
}

TEST_F(ArrangerTest, RemoveChainEntry) {
    auto rid = commands->addArrangerRegion("Verse", 8.0, 16.0);
    auto cid = commands->addArrangerChain("A");
    commands->addChainEntry(cid, rid);
    commands->removeChainEntry(cid, 0);

    auto chains = readModel->getArrangerChains();
    EXPECT_TRUE(chains[0].entries.empty());
}

TEST_F(ArrangerTest, ReorderChainEntry) {
    auto rid1 = commands->addArrangerRegion("A", 0.0, 8.0);
    auto rid2 = commands->addArrangerRegion("B", 8.0, 8.0);
    auto cid = commands->addArrangerChain("X");
    commands->addChainEntry(cid, rid1);
    commands->addChainEntry(cid, rid2);
    commands->reorderChainEntry(cid, 0, 1);

    auto chains = readModel->getArrangerChains();
    EXPECT_EQ(chains[0].entries[0].regionID, rid2);
    EXPECT_EQ(chains[0].entries[1].regionID, rid1);
}

TEST_F(ArrangerTest, SetRepeatCount) {
    auto rid = commands->addArrangerRegion("Chorus", 24.0, 16.0);
    auto cid = commands->addArrangerChain("A");
    commands->addChainEntry(cid, rid, 1);
    commands->setChainEntryRepeat(cid, 0, 3);

    auto chains = readModel->getArrangerChains();
    EXPECT_EQ(chains[0].entries[0].repeatCount, 3);
}

TEST_F(ArrangerTest, RepeatMinOne) {
    auto rid = commands->addArrangerRegion("Chorus", 24.0, 16.0);
    auto cid = commands->addArrangerChain("A");
    commands->addChainEntry(cid, rid, 5);
    commands->setChainEntryRepeat(cid, 0, 0); // should clamp to 1

    auto chains = readModel->getArrangerChains();
    EXPECT_EQ(chains[0].entries[0].repeatCount, 1);
}

// --- Cascade ---

TEST_F(ArrangerTest, RemoveRegionCascadesToChains) {
    auto rid = commands->addArrangerRegion("Verse", 8.0, 16.0);
    auto cid = commands->addArrangerChain("A");
    commands->addChainEntry(cid, rid);
    commands->removeArrangerRegion(rid);

    auto chains = readModel->getArrangerChains();
    EXPECT_TRUE(chains[0].entries.empty());
}

// --- Undo/Redo ---

TEST_F(ArrangerTest, UndoRegionAdd) {
    auto rid = commands->addArrangerRegion("Intro", 0.0, 8.0);
    model->getUndoManager().undo();
    EXPECT_TRUE(readModel->getArrangerRegions().empty());
}

TEST_F(ArrangerTest, RedoRegionAdd) {
    auto rid = commands->addArrangerRegion("Intro", 0.0, 8.0);
    model->getUndoManager().undo();
    model->getUndoManager().redo();
    EXPECT_EQ(readModel->getArrangerRegions().size(), 1u);
}
```

- [ ] **Step 2: Add test file to CMakeLists.txt**

Add `tests/arranger_test.cpp` to the test executable sources.

- [ ] **Step 3: Build and run tests**

Run: `cmake --build build --config Debug; if ($?) { build\Debug\hdaw_tests.exe --gtest_filter=ArrangerTest.* }`
Expected: All tests pass.

- [ ] **Step 4: Commit**

```bash
git add tests/arranger_test.cpp CMakeLists.txt
git commit -m "test(arranger): add CRUD, cascade, and undo/redo tests"
```

---

## Task 6: Transport State & Atomics

**Files:**
- Modify: `src/engine/TransportManager.h`
- Modify: `src/engine/AudioEngine.cpp` (valueTreePropertyChanged)

- [ ] **Step 1: Add arranger atomics to TransportManager.h**

After the existing atomics block (line ~162):

```cpp
// Arranger mode
std::atomic<bool> arrangerEnabled { false };
std::atomic<int> arrangerChainPosition { 0 };
std::atomic<int> arrangerRepeatIndex { 0 };
```

Add setters:

```cpp
void setArrangerEnabled(bool v) { arrangerEnabled.store(v); }
void setArrangerChainPosition(int v) { arrangerChainPosition.store(v); }
void setArrangerRepeatIndex(int v) { arrangerRepeatIndex.store(v); }
```

- [ ] **Step 2: Add ValueTree → atomic sync in AudioEngine.cpp**

In `valueTreePropertyChanged`, where it handles TRANSPORT properties (around line 632-660), add cases for the three new properties:

```cpp
if (property == IDs::arrangerEnabled)
    transportManager.setArrangerEnabled(static_cast<bool>(value));
else if (property == IDs::arrangerChainPosition)
    transportManager.setArrangerChainPosition(static_cast<int>(value));
else if (property == IDs::arrangerRepeatIndex)
    transportManager.setArrangerRepeatIndex(static_cast<int>(value));
```

Also add the ID declarations to `ProjectModel.h` if not already present:

```cpp
DECLARE_ID(arrangerEnabled)
DECLARE_ID(arrangerChainPosition)
DECLARE_ID(arrangerRepeatIndex)
```

And set defaults in `createDefaultProject()` for the TRANSPORT node:

```cpp
transportTree.setProperty(IDs::arrangerEnabled, false, nullptr);
transportTree.setProperty(IDs::arrangerChainPosition, 0, nullptr);
transportTree.setProperty(IDs::arrangerRepeatIndex, 0, nullptr);
```

- [ ] **Step 3: Verify build compiles**

Run: `cmake --build build --config Debug`
Expected: Compiles.

- [ ] **Step 4: Commit**

```bash
git add src/engine/TransportManager.h src/engine/AudioEngine.cpp src/model/ProjectModel.h src/model/ProjectModel.cpp
git commit -m "feat(arranger): add transport state atomics for arranger mode"
```

---

## Task 7: Frontend Store & Sync

**Files:**
- Create: `frontend/src/store/arrangerStore.ts`
- Modify: `frontend/src/store/projectStore.ts`

- [ ] **Step 1: Create arrangerStore.ts**

```typescript
import { create } from "zustand";
import { RpcClient } from "../rpc/client";

export interface ArrangerRegionSnapshot {
  regionID: string;
  name: string;
  startTime: number;
  duration: number;
  color: number;
}

export interface ChainEntrySnapshot {
  regionID: string;
  repeatCount: number;
}

export interface ArrangerChainSnapshot {
  chainID: string;
  name: string;
  isActive: boolean;
  entries: ChainEntrySnapshot[];
}

interface ArrangerState {
  regions: ArrangerRegionSnapshot[];
  chains: ArrangerChainSnapshot[];
  syncArranger: (rpc: RpcClient) => Promise<void>;
}

export const useArrangerStore = create<ArrangerState>((set) => ({
  regions: [],
  chains: [],
  syncArranger: async (rpc: RpcClient) => {
    try {
      const [regionsResult, chainsResult] = await Promise.all([
        rpc.call("read.getArrangerRegions"),
        rpc.call("read.getArrangerChains"),
      ]);
      set({
        regions: Array.isArray(regionsResult) ? (regionsResult as ArrangerRegionSnapshot[]) : [],
        chains: Array.isArray(chainsResult) ? (chainsResult as ArrangerChainSnapshot[]) : [],
      });
    } catch {
      // ignore — arranger data may not exist yet
    }
  },
}));
```

- [ ] **Step 2: Wire into projectStore fullSync**

In `frontend/src/store/projectStore.ts`, find the `syncSnapshot` function where markers are synced (around line 53-55). After `useMarkerStore.getState().syncMarkers(rpc)`, add:

```typescript
useArrangerStore.getState().syncArranger(rpc);
```

Add the import at the top:

```typescript
import { useArrangerStore } from "./arrangerStore";
```

- [ ] **Step 3: Run frontend type check**

Run: `cd frontend; npm run typecheck`
Expected: No errors.

- [ ] **Step 4: Commit**

```bash
git add frontend/src/store/arrangerStore.ts frontend/src/store/projectStore.ts
git commit -m "feat(arranger): add frontend store and fullSync wiring"
```

---

## Task 8: Arranger Lane on Timeline

**Files:**
- Create: `frontend/src/components/ArrangerLane.tsx`
- Create: `frontend/src/components/ArrangerLane.css`
- Modify: `frontend/src/components/TimelineMinimal/TimelineMinimal.tsx`

- [ ] **Step 1: Create ArrangerLane.css**

```css
.tl-arranger-lane {
  position: relative;
  height: 32px;
  background: var(--bg-panel);
  border-bottom: 1px solid var(--border);
  overflow: hidden;
  flex-shrink: 0;
}

.tl-arranger-lane-empty {
  height: 0;
  overflow: hidden;
}

.tl-arranger-region {
  position: absolute;
  top: 2px;
  bottom: 2px;
  border-radius: 3px;
  display: flex;
  align-items: center;
  padding: 0 6px;
  cursor: pointer;
  user-select: none;
  overflow: hidden;
  border: 1px solid transparent;
  transition: border-color 0.1s;
}

.tl-arranger-region:hover {
  border-color: rgba(255, 255, 255, 0.3);
}

.tl-arranger-region.selected {
  border-color: var(--accent);
  box-shadow: 0 0 0 1px var(--accent);
}

.tl-arranger-region-label {
  font-size: 11px;
  font-weight: 500;
  color: #fff;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
  pointer-events: none;
  text-shadow: 0 1px 2px rgba(0, 0, 0, 0.5);
}

.tl-arranger-region-handle {
  position: absolute;
  top: 0;
  bottom: 0;
  width: 6px;
  cursor: ew-resize;
}

.tl-arranger-region-handle-left {
  left: 0;
}

.tl-arranger-region-handle-right {
  right: 0;
}
```

- [ ] **Step 2: Create ArrangerLane.tsx**

```tsx
import React, { useCallback, useRef, useState } from "react";
import { useArrangerStore, ArrangerRegionSnapshot } from "../store/arrangerStore";
import { rpc } from "../rpc/client";
import "./ArrangerLane.css";

interface Props {
  pps: number;
  scrollLeft: number;
}

export const ArrangerLane: React.FC<Props> = ({ pps, scrollLeft }) => {
  const regions = useArrangerStore((s) => s.regions);
  const [selectedId, setSelectedId] = useState<string | null>(null);
  const [editingId, setEditingId] = useState<string | null>(null);
  const [dragState, setDragState] = useState<{
    type: "move" | "resize-left" | "resize-right";
    regionID: string;
    startX: number;
    origStart: number;
    origDuration: number;
  } | null>(null);
  const laneRef = useRef<HTMLDivElement>(null);

  const handleMouseDown = useCallback(
    (e: React.MouseEvent, region: ArrangerRegionSnapshot, handle?: "left" | "right") => {
      e.stopPropagation();
      setSelectedId(region.regionID);
      setDragState({
        type: handle === "left" ? "resize-left" : handle === "right" ? "resize-right" : "move",
        regionID: region.regionID,
        startX: e.clientX,
        origStart: region.startTime,
        origDuration: region.duration,
      });
    },
    []
  );

  const handleMouseMove = useCallback(
    (e: React.MouseEvent) => {
      if (!dragState) return;
      const dx = e.clientX - dragState.startX;
      const dBeats = dx / pps;

      let newStart = dragState.origStart;
      let newDuration = dragState.origDuration;

      if (dragState.type === "move") {
        newStart = Math.max(0, dragState.origStart + dBeats);
      } else if (dragState.type === "resize-left") {
        newStart = Math.max(0, dragState.origStart + dBeats);
        newDuration = Math.max(1, dragState.origDuration - dBeats);
      } else {
        newDuration = Math.max(1, dragState.origDuration + dBeats);
      }

      // Snap to beat grid
      newStart = Math.round(newStart * 4) / 4;
      newDuration = Math.round(newDuration * 4) / 4;
      if (newDuration < 0.25) newDuration = 0.25;

      rpc.call("project.setArrangerRegionBounds", {
        regionID: dragState.regionID,
        startTime: newStart,
        duration: newDuration,
      });
    },
    [dragState, pps]
  );

  const handleMouseUp = useCallback(() => {
    setDragState(null);
  }, []);

  const handleDoubleClick = useCallback(
    (e: React.MouseEvent, region: ArrangerRegionSnapshot) => {
      e.stopPropagation();
      setEditingId(region.regionID);
    },
    []
  );

  const handleRename = useCallback(
    (regionID: string, newName: string) => {
      if (newName.trim()) {
        rpc.call("project.setArrangerRegionName", { regionID, name: newName.trim() });
      }
      setEditingId(null);
    },
    []
  );

  const handleDelete = useCallback(() => {
    if (selectedId) {
      rpc.call("project.removeArrangerRegion", { regionID: selectedId });
      setSelectedId(null);
    }
  }, [selectedId]);

  const handleContextMenu = useCallback(
    (e: React.MouseEvent, region: ArrangerRegionSnapshot) => {
      e.preventDefault();
      e.stopPropagation();
      setSelectedId(region.regionID);
      // Context menu handled by parent TimelineContextMenu or dedicated menu
    },
    []
  );

  const handleLaneClick = useCallback(
    (e: React.MouseEvent) => {
      if (e.target === laneRef.current || (e.target as HTMLElement).classList.contains("tl-arranger-lane")) {
        setSelectedId(null);
      }
    },
    []
  );

  const handleLaneDoubleClick = useCallback(
    (e: React.MouseEvent) => {
      if (!laneRef.current) return;
      const rect = laneRef.current.getBoundingClientRect();
      const x = e.clientX - rect.left + scrollLeft;
      const startBeat = Math.round((x / pps) * 4) / 4;
      const dur = 8; // default 8 beats
      rpc.call("project.addArrangerRegion", {
        name: `Section ${regions.length + 1}`,
        startTime: startBeat,
        duration: dur,
      });
    },
    [pps, scrollLeft, regions.length]
  );

  const handleKeyDown = useCallback(
    (e: React.KeyboardEvent) => {
      if (e.key === "Delete" || e.key === "Backspace") {
        handleDelete();
      }
    },
    [handleDelete]
  );

  if (regions.length === 0 && !dragState) {
    return (
      <div
        className="tl-arranger-lane tl-arranger-lane-empty"
        onDoubleClick={handleLaneDoubleClick}
        ref={laneRef}
      />
    );
  }

  return (
    <div
      className="tl-arranger-lane"
      ref={laneRef}
      onMouseMove={handleMouseMove}
      onMouseUp={handleMouseUp}
      onClick={handleLaneClick}
      onDoubleClick={handleLaneDoubleClick}
      onKeyDown={handleKeyDown}
      tabIndex={0}
    >
      {regions.map((region) => {
        const left = region.startTime * pps - scrollLeft;
        const width = region.duration * pps;
        const isSelected = region.regionID === selectedId;
        const isEditing = region.regionID === editingId;
        const bgColor = region.color
          ? `#${region.color.toString(16).padStart(8, "0").slice(2)}`
          : "var(--accent)";

        return (
          <div
            key={region.regionID}
            className={`tl-arranger-region${isSelected ? " selected" : ""}`}
            style={{ left, width, backgroundColor: bgColor }}
            onMouseDown={(e) => handleMouseDown(e, region)}
            onDoubleClick={(e) => handleDoubleClick(e, region)}
            onContextMenu={(e) => handleContextMenu(e, region)}
          >
            <div
              className="tl-arranger-region-handle tl-arranger-region-handle-left"
              onMouseDown={(e) => handleMouseDown(e, region, "left")}
            />
            {isEditing ? (
              <input
                autoFocus
                defaultValue={region.name}
                onBlur={(e) => handleRename(region.regionID, e.currentTarget.value)}
                onKeyDown={(e) => {
                  if (e.key === "Enter") handleRename(region.regionID, e.currentTarget.value);
                  if (e.key === "Escape") setEditingId(null);
                }}
                onClick={(e) => e.stopPropagation()}
                style={{
                  background: "transparent",
                  border: "none",
                  color: "#fff",
                  fontSize: "11px",
                  fontWeight: 500,
                  width: "100%",
                  outline: "none",
                }}
              />
            ) : (
              <span className="tl-arranger-region-label">{region.name}</span>
            )}
            <div
              className="tl-arranger-region-handle tl-arranger-region-handle-right"
              onMouseDown={(e) => handleMouseDown(e, region, "right")}
            />
          </div>
        );
      })}
    </div>
  );
};
```

- [ ] **Step 3: Add ArrangerLane to TimelineMinimal.tsx**

In the timeline component, import and render `ArrangerLane` between the ruler and tracks:

```tsx
import { ArrangerLane } from "../ArrangerLane";
```

In the JSX, after `.tl-ruler` and before `.tl-tracks`:

```tsx
<ArrangerLane pps={pps} scrollLeft={scrollLeft} />
```

The `scrollLeft` state should already exist in the timeline component (used for horizontal scrolling). If it's a ref, convert to state or pass the ref value.

- [ ] **Step 4: Run frontend type check and tests**

Run: `cd frontend; npm run typecheck; npm test`
Expected: No errors.

- [ ] **Step 5: Commit**

```bash
git add frontend/src/components/ArrangerLane.tsx frontend/src/components/ArrangerLane.css frontend/src/components/TimelineMinimal/TimelineMinimal.tsx
git commit -m "feat(arranger): add arranger lane to timeline"
```

---

## Task 9: Chain Editor Tab

**Files:**
- Create: `frontend/src/components/ArrangerChainEditor.tsx`
- Create: `frontend/src/components/ArrangerChainEditor.css`
- Modify: `frontend/src/components/BottomTabs.tsx` (or equivalent tab registry)

- [ ] **Step 1: Create ArrangerChainEditor.css**

```css
.arranger-chain-editor {
  display: flex;
  flex-direction: column;
  height: 100%;
  padding: 8px;
  gap: 8px;
}

.arranger-chain-toolbar {
  display: flex;
  align-items: center;
  gap: 8px;
  flex-shrink: 0;
}

.arranger-chain-toolbar select {
  background: var(--bg-control);
  color: var(--text);
  border: 1px solid var(--border);
  border-radius: 3px;
  padding: 2px 6px;
  font-size: 12px;
}

.arranger-chain-toolbar button {
  background: var(--bg-control);
  color: var(--text);
  border: 1px solid var(--border);
  border-radius: 3px;
  padding: 2px 8px;
  font-size: 12px;
  cursor: pointer;
}

.arranger-chain-toolbar button:hover {
  background: var(--bg-control-hover);
}

.arranger-chain-toolbar button.active {
  background: var(--accent);
  color: #fff;
  border-color: var(--accent);
}

.arranger-chain-columns {
  display: flex;
  flex: 1;
  gap: 8px;
  overflow: hidden;
}

.arranger-chain-column {
  flex: 1;
  display: flex;
  flex-direction: column;
  border: 1px solid var(--border);
  border-radius: 4px;
  overflow: hidden;
}

.arranger-chain-column-header {
  font-size: 11px;
  font-weight: 600;
  text-transform: uppercase;
  color: var(--text-secondary);
  padding: 6px 8px;
  background: var(--bg-panel);
  border-bottom: 1px solid var(--border);
  flex-shrink: 0;
}

.arranger-chain-list {
  flex: 1;
  overflow-y: auto;
  padding: 4px;
}

.arranger-chain-entry {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 4px 8px;
  border-radius: 3px;
  font-size: 12px;
  cursor: grab;
  user-select: none;
}

.arranger-chain-entry:hover {
  background: var(--bg-control);
}

.arranger-chain-entry.dragging {
  opacity: 0.5;
}

.arranger-chain-entry-name {
  flex: 1;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.arranger-chain-entry-repeat {
  background: var(--bg-control);
  border: 1px solid var(--border);
  border-radius: 3px;
  padding: 1px 5px;
  font-size: 11px;
  cursor: pointer;
  min-width: 28px;
  text-align: center;
}

.arranger-chain-entry-remove {
  background: none;
  border: none;
  color: var(--text-secondary);
  cursor: pointer;
  font-size: 14px;
  padding: 0 2px;
  line-height: 1;
}

.arranger-chain-entry-remove:hover {
  color: var(--danger);
}

.arranger-region-item {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 4px 8px;
  border-radius: 3px;
  font-size: 12px;
  cursor: pointer;
  user-select: none;
}

.arranger-region-item:hover {
  background: var(--bg-control);
}

.arranger-region-item-beats {
  color: var(--text-secondary);
  font-size: 11px;
}
```

- [ ] **Step 2: Create ArrangerChainEditor.tsx**

```tsx
import React, { useCallback, useState } from "react";
import {
  useArrangerStore,
  ArrangerRegionSnapshot,
  ArrangerChainSnapshot,
} from "../store/arrangerStore";
import { rpc } from "../rpc/client";
import "./ArrangerChainEditor.css";

export const ArrangerChainEditor: React.FC = () => {
  const regions = useArrangerStore((s) => s.regions);
  const chains = useArrangerStore((s) => s.chains);
  const [dragEntryIdx, setDragEntryIdx] = useState<number | null>(null);

  const activeChain = chains.find((c) => c.isActive) || chains[0];
  const regionMap = new Map(regions.map((r) => [r.regionID, r]));

  const handleNewChain = useCallback(async () => {
    const name = `Arrangement ${String.fromCharCode(65 + chains.length)}`;
    await rpc.call("project.addArrangerChain", { name });
  }, [chains.length]);

  const handleDeleteChain = useCallback(async () => {
    if (activeChain) {
      await rpc.call("project.removeArrangerChain", { chainID: activeChain.chainID });
    }
  }, [activeChain]);

  const handleSwitchChain = useCallback(async (chainID: string) => {
    await rpc.call("project.setArrangerChainActive", { chainID });
  }, []);

  const handleAddEntry = useCallback(
    async (regionID: string) => {
      if (!activeChain) return;
      await rpc.call("project.addChainEntry", {
        chainID: activeChain.chainID,
        regionID,
        repeatCount: 1,
      });
    },
    [activeChain]
  );

  const handleRemoveEntry = useCallback(
    async (entryIndex: number) => {
      if (!activeChain) return;
      await rpc.call("project.removeChainEntry", {
        chainID: activeChain.chainID,
        entryIndex,
      });
    },
    [activeChain]
  );

  const handleSetRepeat = useCallback(
    async (entryIndex: number, currentRepeat: number) => {
      if (!activeChain) return;
      const newRepeat = currentRepeat >= 8 ? 1 : currentRepeat + 1;
      await rpc.call("project.setChainEntryRepeat", {
        chainID: activeChain.chainID,
        entryIndex,
        repeatCount: newRepeat,
      });
    },
    [activeChain]
  );

  const handleDragStart = useCallback((idx: number) => {
    setDragEntryIdx(idx);
  }, []);

  const handleDragOver = useCallback(
    async (e: React.DragEvent, idx: number) => {
      e.preventDefault();
      if (dragEntryIdx === null || !activeChain || dragEntryIdx === idx) return;
      await rpc.call("project.reorderChainEntry", {
        chainID: activeChain.chainID,
        fromIndex: dragEntryIdx,
        toIndex: idx,
      });
      setDragEntryIdx(idx);
    },
    [dragEntryIdx, activeChain]
  );

  const handleDragEnd = useCallback(() => {
    setDragEntryIdx(null);
  }, []);

  return (
    <div className="arranger-chain-editor">
      <div className="arranger-chain-toolbar">
        <span style={{ fontSize: "12px", fontWeight: 600 }}>Chain:</span>
        <select
          value={activeChain?.chainID || ""}
          onChange={(e) => handleSwitchChain(e.target.value)}
        >
          {chains.map((c) => (
            <option key={c.chainID} value={c.chainID}>
              {c.name}{c.isActive ? " *" : ""}
            </option>
          ))}
        </select>
        <button onClick={handleNewChain}>+ New</button>
        <button onClick={handleDeleteChain} disabled={!activeChain || chains.length <= 1}>
          Delete
        </button>
        <div style={{ flex: 1 }} />
      </div>

      <div className="arranger-chain-columns">
        <div className="arranger-chain-column">
          <div className="arranger-chain-column-header">
            Active Chain ({activeChain?.entries.length || 0} entries)
          </div>
          <div className="arranger-chain-list">
            {activeChain?.entries.map((entry, idx) => {
              const region = regionMap.get(entry.regionID);
              if (!region) return null;
              return (
                <div
                  key={`${entry.regionID}-${idx}`}
                  className={`arranger-chain-entry${dragEntryIdx === idx ? " dragging" : ""}`}
                  draggable
                  onDragStart={() => handleDragStart(idx)}
                  onDragOver={(e) => handleDragOver(e, idx)}
                  onDragEnd={handleDragEnd}
                >
                  <span style={{ color: "var(--text-secondary)", fontSize: "11px", minWidth: 20 }}>
                    {idx + 1}.
                  </span>
                  <span className="arranger-chain-entry-name">{region.name}</span>
                  <span
                    className="arranger-chain-entry-repeat"
                    onClick={() => handleSetRepeat(idx, entry.repeatCount)}
                    title="Click to cycle repeat count"
                  >
                    x{entry.repeatCount}
                  </span>
                  <button
                    className="arranger-chain-entry-remove"
                    onClick={() => handleRemoveEntry(idx)}
                    title="Remove from chain"
                  >
                    x
                  </button>
                </div>
              );
            })}
            {(!activeChain || activeChain.entries.length === 0) && (
              <div style={{ padding: "12px", color: "var(--text-secondary)", fontSize: "12px", textAlign: "center" }}>
                Double-click a region on the right to add it
              </div>
            )}
          </div>
        </div>

        <div className="arranger-chain-column">
          <div className="arranger-chain-column-header">
            Available Regions ({regions.length})
          </div>
          <div className="arranger-chain-list">
            {regions.map((region) => (
              <div
                key={region.regionID}
                className="arranger-region-item"
                onDoubleClick={() => handleAddEntry(region.regionID)}
                title="Double-click to add to chain"
              >
                <span>{region.name}</span>
                <span className="arranger-region-item-beats">
                  {region.startTime.toFixed(1)}-{(region.startTime + region.duration).toFixed(1)}
                </span>
              </div>
            ))}
            {regions.length === 0 && (
              <div style={{ padding: "12px", color: "var(--text-secondary)", fontSize: "12px", textAlign: "center" }}>
                Draw regions on the timeline arranger lane
              </div>
            )}
          </div>
        </div>
      </div>
    </div>
  );
};
```

- [ ] **Step 3: Register as bottom panel tab**

In `frontend/src/components/BottomTabs.tsx` (or equivalent tab registry), add the "Arranger" tab. Import `ArrangerChainEditor` and add it to the tab list alongside Mixer, Piano Roll, etc.

The exact integration depends on how tabs are registered — look at how existing tabs (Mixer, Piano Roll, Automation) are added and follow the same pattern. Add an icon (e.g., a puzzle-piece or chain-link SVG).

- [ ] **Step 4: Run frontend type check and tests**

Run: `cd frontend; npm run typecheck; npm test`
Expected: No errors.

- [ ] **Step 5: Commit**

```bash
git add frontend/src/components/ArrangerChainEditor.tsx frontend/src/components/ArrangerChainEditor.css frontend/src/components/BottomTabs.tsx
git commit -m "feat(arranger): add chain editor bottom panel tab"
```

---

## Task 10: Playback Engine — Arranger Mode

**Files:**
- Modify: `src/engine/TransportManager.h` (add resolveChainPosition)
- Modify: `src/engine/MainAudioProcessor.cpp` (processBlock arranger logic)

- [ ] **Step 1: Add chain data structures to TransportManager.h**

Add cached chain data that the audio thread can read lock-free:

```cpp
// Cached arranger chain data (rebuilt on message thread when chain changes)
struct ArrangerRegionData {
    juce::String regionID;
    double startTime = 0.0;  // beats
    double duration = 0.0;   // beats
};

struct ArrangerChainEntryData {
    int regionIndex = -1;    // index into regions vector
    int repeatCount = 1;
};

struct ArrangerChainData {
    std::vector<ArrangerRegionData> regions;
    std::vector<ArrangerChainEntryData> entries;
    double totalDurationBeats = 0.0; // precomputed
};

std::shared_ptr<const ArrangerChainData> arrangerChainData;
std::shared_mutex arrangerChainMutex;

void rebuildArrangerChainData(const juce::ValueTree& root);
```

- [ ] **Step 2: Implement rebuildArrangerChainData**

This runs on the message thread when arranger data changes. It reads the ValueTree and builds the lock-free `ArrangerChainData` snapshot:

```cpp
void TransportManager::rebuildArrangerChainData(const juce::ValueTree& root)
{
    auto data = std::make_shared<ArrangerChainData>();

    auto arrangerList = root.getChildWithName(IDs::ARRANGER_LIST);
    auto chainList = root.getChildWithName(IDs::ARRANGER_CHAIN_LIST);
    if (!arrangerList.isValid() || !chainList.isValid())
    {
        std::unique_lock lock(arrangerChainMutex);
        arrangerChainData = data;
        return;
    }

    // Find active chain
    juce::ValueTree activeChain;
    for (int i = 0; i < chainList.getNumChildren(); ++i)
    {
        auto chain = chainList.getChild(i);
        if (static_cast<bool>(chain.getProperty(IDs::isActive, false)))
        {
            activeChain = chain;
            break;
        }
    }
    if (!activeChain.isValid())
    {
        // No active chain — use first if exists
        if (chainList.getNumChildren() > 0)
            activeChain = chainList.getChild(0);
    }
    if (!activeChain.isValid())
    {
        std::unique_lock lock(arrangerChainMutex);
        arrangerChainData = data;
        return;
    }

    // Build region index
    std::map<juce::String, int> regionIndexMap;
    for (int i = 0; i < arrangerList.getNumChildren(); ++i)
    {
        auto region = arrangerList.getChild(i);
        ArrangerRegionData rd;
        rd.regionID = region.getProperty(IDs::regionID, "").toString();
        rd.startTime = static_cast<double>(region.getProperty(IDs::startTime, 0.0));
        rd.duration = static_cast<double>(region.getProperty(IDs::duration, 0.0));
        regionIndexMap[rd.regionID] = static_cast<int>(data->regions.size());
        data->regions.push_back(rd);
    }

    // Build entries
    double totalBeats = 0.0;
    for (int i = 0; i < activeChain.getNumChildren(); ++i)
    {
        auto entry = activeChain.getChild(i);
        juce::String rid = entry.getProperty(IDs::regionID, "").toString();
        auto it = regionIndexMap.find(rid);
        if (it == regionIndexMap.end()) continue;

        ArrangerChainEntryData ed;
        ed.regionIndex = it->second;
        ed.repeatCount = juce::jmax(1, static_cast<int>(entry.getProperty(IDs::repeatCount, 1)));
        data->entries.push_back(ed);
        totalBeats += data->regions[ed.regionIndex].duration * ed.repeatCount;
    }
    data->totalDurationBeats = totalBeats;

    std::unique_lock lock(arrangerChainMutex);
    arrangerChainData = data;
}
```

- [ ] **Step 3: Add resolveChainPosition to TransportManager.h**

```cpp
struct ChainPosition {
    int entryIndex = 0;
    int repeatIndex = 0;
    double timelineBeat = 0.0; // actual beat on the timeline
};

ChainPosition resolveChainPosition(double beat) const
{
    std::shared_lock lock(arrangerChainMutex);
    ChainPosition result;
    if (!arrangerChainData || arrangerChainData->entries.empty())
    {
        result.timelineBeat = beat;
        return result;
    }

    double accumulated = 0.0;
    for (int i = 0; i < static_cast<int>(arrangerChainData->entries.size()); ++i)
    {
        const auto& entry = arrangerChainData->entries[i];
        const auto& region = arrangerChainData->regions[entry.regionIndex];
        double entryDuration = region.duration * entry.repeatCount;
        if (beat >= accumulated && beat < accumulated + entryDuration)
        {
            double offsetInEntry = beat - accumulated;
            int repeatIdx = static_cast<int>(offsetInEntry / region.duration);
            double offsetInRepeat = offsetInEntry - (repeatIdx * region.duration);
            result.entryIndex = i;
            result.repeatIndex = repeatIdx;
            result.timelineBeat = region.startTime + offsetInRepeat;
            return result;
        }
        accumulated += entryDuration;
    }

    // Past chain end
    if (!arrangerChainData->entries.empty())
    {
        int lastIdx = static_cast<int>(arrangerChainData->entries.size()) - 1;
        const auto& lastEntry = arrangerChainData->entries[lastIdx];
        const auto& lastRegion = arrangerChainData->regions[lastEntry.regionIndex];
        result.entryIndex = lastIdx;
        result.repeatIndex = lastEntry.repeatCount - 1;
        result.timelineBeat = lastRegion.startTime + lastRegion.duration;
    }
    return result;
}
```

- [ ] **Step 4: Modify processBlock for arranger mode**

In `MainAudioProcessor.cpp`, in `processBlock`, after the existing transport advance logic, add arranger-mode handling:

```cpp
// Arranger mode: remap position
if (transportManager->arrangerEnabled.load())
{
    auto chainData = transportManager->arrangerChainData;
    if (chainData && !chainData->entries.empty())
    {
        double currentBeat = transportManager->getCurrentBeat(); // position in beats
        auto chainPos = transportManager->resolveChainPosition(currentBeat);

        // Update transport state for frontend
        // (these are written on the audio thread, read by the message thread via atomics)
        transportManager->arrangerChainPosition.store(chainPos.entryIndex);
        transportManager->arrangerRepeatIndex.store(chainPos.repeatIndex);

        // The graph should render based on the timeline beat
        // This requires setting the graph's playback position to chainPos.timelineBeat
        // Implementation depends on how the graph reads position — may need an SPSC message
    }
}
```

**Note:** The exact processBlock integration depends on how `MainAudioProcessor` currently handles position-based clip rendering. The key insight is that in arranger mode, the "virtual" beat position (what the user sees) maps to a different "timeline" beat position (where the clips actually are). The `resolveChainPosition` function handles this mapping. The actual clip graph position remapping may require an SPSC message to update the graph's read position, following the same pattern as loop handling.

- [ ] **Step 5: Trigger chain data rebuild on arranger changes**

In `AudioEngine.cpp`, in `valueTreePropertyChanged` / `valueTreeChildAdded` / `valueTreeChildRemoved`, when the changed node is under `ARRANGER_LIST` or `ARRANGER_CHAIN_LIST`, call `transportManager->rebuildArrangerChainData(root)`.

- [ ] **Step 6: Build and verify**

Run: `cmake --build build --config Debug`
Expected: Compiles.

- [ ] **Step 7: Commit**

```bash
git add src/engine/TransportManager.h src/engine/MainAudioProcessor.cpp src/engine/AudioEngine.cpp
git commit -m "feat(arranger): implement arranger mode playback engine"
```

---

## Task 11: Flatten Implementation

**Files:**
- Modify: `src/engine/AudioEngineCommands_Arranger.cpp` (fill in flattenArranger)

- [ ] **Step 1: Implement flattenArranger**

Replace the `jassertfalse` stub with the full implementation:

```cpp
void AudioEngineCommands::flattenArranger()
{
    auto& um = model_.getUndoManager();
    auto root = model_.getTree();

    // Find active chain
    auto chainList = root.getChildWithName(IDs::ARRANGER_CHAIN_LIST);
    if (!chainList.isValid()) return;

    juce::ValueTree activeChain;
    for (int i = 0; i < chainList.getNumChildren(); ++i)
    {
        auto chain = chainList.getChild(i);
        if (static_cast<bool>(chain.getProperty(IDs::isActive, false)))
        {
            activeChain = chain;
            break;
        }
    }
    if (!activeChain.isValid() || activeChain.getNumChildren() == 0) return;

    // Build region map
    auto arrangerList = root.getChildWithName(IDs::ARRANGER_LIST);
    if (!arrangerList.isValid()) return;

    std::map<juce::String, juce::ValueTree> regionMap;
    for (int i = 0; i < arrangerList.getNumChildren(); ++i)
    {
        auto region = arrangerList.getChild(i);
        regionMap[region.getProperty(IDs::regionID, "").toString()] = region;
    }

    // Collect all clips per track that fall within any chain region
    auto trackList = root.getChildWithName(IDs::TRACK_LIST);
    if (!trackList.isValid()) return;

    // Build output clip list
    struct OutputClip {
        int trackIndex;
        juce::ValueTree clipTree;
        double newStartBeat;
    };
    std::vector<OutputClip> outputClips;
    double outputOffset = 0.0;

    for (int e = 0; e < activeChain.getNumChildren(); ++e)
    {
        auto entry = activeChain.getChild(e);
        juce::String rid = entry.getProperty(IDs::regionID, "").toString();
        auto it = regionMap.find(rid);
        if (it == regionMap.end()) continue;

        double regionStart = static_cast<double>(it->second.getProperty(IDs::startTime, 0.0));
        double regionDuration = static_cast<double>(it->second.getProperty(IDs::duration, 0.0));
        int repeatCount = juce::jmax(1, static_cast<int>(entry.getProperty(IDs::repeatCount, 1)));

        for (int rep = 0; rep < repeatCount; ++rep)
        {
            double repOffset = outputOffset + (rep * regionDuration);

            for (int t = 0; t < trackList.getNumChildren(); ++t)
            {
                auto track = trackList.getChild(t);
                auto clipList = track.getChildWithName(IDs::CLIP_LIST);
                if (!clipList.isValid()) continue;

                for (int c = 0; c < clipList.getNumChildren(); ++c)
                {
                    auto clip = clipList.getChild(c);
                    double clipStart = static_cast<double>(clip.getProperty(IDs::startTime, 0.0));
                    double clipDur = static_cast<double>(clip.getProperty(IDs::duration, 0.0));
                    double clipEnd = clipStart + clipDur;

                    // Check overlap with region
                    double regionEnd = regionStart + regionDuration;
                    if (clipEnd <= regionStart || clipStart >= regionEnd) continue;

                    // Compute clipped bounds within region
                    double clippedStart = juce::jmax(clipStart, regionStart);
                    double clippedEnd = juce::jmin(clipEnd, regionEnd);
                    double clippedDur = clippedEnd - clippedStart;

                    // Output position
                    double newStart = repOffset + (clippedStart - regionStart);

                    // Create output clip (deep copy)
                    auto newClip = clip.createCopy();
                    newClip.setProperty(IDs::startTime, newStart, nullptr);
                    newClip.setProperty(IDs::duration, clippedDur, nullptr);
                    // Adjust offset for audio clips
                    if (clip.hasProperty(IDs::offset))
                    {
                        double origOffset = static_cast<double>(clip.getProperty(IDs::offset, 0.0));
                        double clipRelativeStart = clippedStart - clipStart;
                        newClip.setProperty(IDs::offset, origOffset + clipRelativeStart, nullptr);
                    }
                    // Generate new clip ID
                    newClip.setProperty(IDs::clipID, juce::Uuid().toString(), nullptr);

                    outputClips.push_back({ t, newClip, newStart });
                }
            }
        }
        outputOffset += regionDuration * repeatCount;
    }

    // Delete all existing clips from all tracks
    for (int t = 0; t < trackList.getNumChildren(); ++t)
    {
        auto track = trackList.getChild(t);
        auto clipList = track.getChildWithName(IDs::CLIP_LIST);
        if (!clipList.isValid()) continue;
        while (clipList.getNumChildren() > 0)
            clipList.removeChild(0, &um);
    }

    // Insert output clips
    for (const auto& oc : outputClips)
    {
        auto track = trackList.getChild(oc.trackIndex);
        auto clipList = track.getChildWithName(IDs::CLIP_LIST);
        if (clipList.isValid())
            clipList.addChild(oc.clipTree, -1, &um);
    }

    // Remove arranger data
    root.removeChild(arrangerList, &um);
    root.removeChild(chainList, &um);
}
```

- [ ] **Step 2: Add flatten tests to arranger_test.cpp**

```cpp
TEST_F(ArrangerTest, FlattenBasic) {
    // Create a region and a chain, then flatten
    auto rid = commands->addArrangerRegion("Section", 0.0, 8.0);
    auto cid = readModel->getArrangerChains()[0].chainID;
    commands->addChainEntry(cid, rid, 1);
    commands->flattenArranger();

    // Arranger data should be gone
    EXPECT_TRUE(readModel->getArrangerRegions().empty());
    EXPECT_TRUE(readModel->getArrangerChains().empty());
}

TEST_F(ArrangerTest, FlattenNoopWhenNoChain) {
    commands->flattenArranger(); // should not crash
    EXPECT_TRUE(readModel->getArrangerRegions().empty());
}
```

- [ ] **Step 3: Build and run tests**

Run: `cmake --build build --config Debug; if ($?) { build\Debug\hdaw_tests.exe --gtest_filter=ArrangerTest.* }`
Expected: All tests pass.

- [ ] **Step 4: Commit**

```bash
git add src/engine/AudioEngineCommands_Arranger.cpp tests/arranger_test.cpp
git commit -m "feat(arranger): implement flatten algorithm"
```

---

## Task 12: MCP Tools & Integration

**Files:**
- Modify: MCP server tool definitions (check `src/mcp/` for existing MCP tool registration pattern)

- [ ] **Step 1: Add MCP tool definitions**

The MCP server exposes tools that map to the same `ProjectCommands` and `ReadModel` interfaces as the RPC routes. Find where existing MCP tools (e.g., `project.addMarker`, `read.getMarkers`) are registered and add the arranger tools following the same pattern:

| MCP Tool | Maps to | Parameters |
|----------|---------|------------|
| `project.addArrangerRegion` | `addArrangerRegion` | `name: string, startTime: number, duration: number, color?: number` |
| `project.removeArrangerRegion` | `removeArrangerRegion` | `regionID: string` |
| `project.setArrangerRegionName` | `setArrangerRegionName` | `regionID: string, name: string` |
| `project.setArrangerRegionBounds` | `setArrangerRegionBounds` | `regionID: string, startTime: number, duration: number` |
| `project.setArrangerRegionColor` | `setArrangerRegionColor` | `regionID: string, color: number` |
| `project.addArrangerChain` | `addArrangerChain` | `name: string` |
| `project.removeArrangerChain` | `removeArrangerChain` | `chainID: string` |
| `project.setArrangerChainName` | `setArrangerChainName` | `chainID: string, name: string` |
| `project.setArrangerChainActive` | `setArrangerChainActive` | `chainID: string` |
| `project.addChainEntry` | `addChainEntry` | `chainID: string, regionID: string, repeatCount?: number` |
| `project.removeChainEntry` | `removeChainEntry` | `chainID: string, entryIndex: number` |
| `project.reorderChainEntry` | `reorderChainEntry` | `chainID: string, fromIndex: number, toIndex: number` |
| `project.setChainEntryRepeat` | `setChainEntryRepeat` | `chainID: string, entryIndex: number, repeatCount: number` |
| `project.flattenArranger` | `flattenArranger` | (none) |
| `read.getArrangerRegions` | `getArrangerRegions` | (none) |
| `read.getArrangerChains` | `getArrangerChains` | (none) |

Grep for `addMarker` in the MCP server code to find the registration pattern, then replicate for all arranger tools.

- [ ] **Step 2: Verify end-to-end flow**

Run the app (`build/Debug/HDAW.exe`), then:
1. Draw regions on the arranger lane
2. Rename them
3. Open the Arranger tab in the bottom panel
4. Build a chain
5. Toggle arranger mode
6. Verify playback follows chain order
7. Flatten and verify clips rearranged

- [ ] **Step 2: Run all tests**

Run: `build\Debug\hdaw_tests.exe`
Run: `cd frontend; npm test`
Run: `cd frontend; npm run test:e2e`
Expected: All pass.

- [ ] **Step 3: Run linters**

Run: `cmake --build build --config Debug`
Run: `cd frontend; npm run typecheck`
Expected: No warnings or errors.

- [ ] **Step 4: Final commit**

```bash
git add -A
git commit -m "feat(arranger): complete arranger track feature"
```
