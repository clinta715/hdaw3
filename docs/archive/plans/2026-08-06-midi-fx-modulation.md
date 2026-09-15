# MIDI FX Modulation & Automation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Enable modulation and automation of MIDI FX parameters, closing the gap between audio FX (which have a full param interface) and MIDI FX (which have none).

**Architecture:** Replicate the `TrackFXSlot` atomic param cache pattern on `MidiFxSlot`. Define a paramID range (200+) for MIDI FX params. Wire the automation loop and modulation application in `Track::processBlock()` to reach MIDI FX params. Expose MIDI FX param metadata via ReadModel for the frontend.

**Tech Stack:** C++17, JUCE 8, React 19, TypeScript, Zustand

---

## ParamID Encoding

| Range | Target |
|-------|--------|
| 1 | Volume |
| 2 | Pan |
| 3 | Mute |
| 100 + slotIndex*100 + paramIndex | Audio FX plugin parameter |
| **200 + slotIndex*100 + paramIndex** | **MIDI FX parameter (new)** |

Example: MIDI FX slot 0, param 2 (Arpeggiator::pattern) = `200 + 0*100 + 2 = 202`

---

## MIDI FX Parameter Definitions

Each MIDI FX type gets a static param defs table. These define the mapping between param index, name, and value range:

| Type | Index | Name | Default | Min | Max |
|------|-------|------|---------|-----|-----|
| **Arpeggiator** | 0 | rate | 0.25 | 0.01 | 2.0 |
| | 1 | pattern | 0 | 0 | 5 |
| | 2 | octaves | 1 | 1 | 4 |
| | 3 | gate | 0.5 | 0.1 | 1.0 |
| | 4 | velocity | 100 | 1 | 127 |
| **VelocityScaler** | 0 | factor | 1.0 | 0.0 | 2.0 |
| **Chorder** | 0 | chordType | 0 | 0 | 17 |
| **ScaleQuantize** | 0 | root | 0 | 0 | 11 |
| | 1 | scaleType | 0 | 0 | 12 |
| **NoteLengthScaler** | 0 | factor | 1.0 | 0.1 | 4.0 |
| **Transpose** | 0 | semitones | 0 | -24 | 24 |
| **KeyFilter** | 0 | root | 0 | 0 | 11 |
| | 1 | scaleType | 0 | 0 | 12 |
| **VelocityCurve** | 0 | curveType | 0 | 0 | 3 |
| | 1 | curveAmount | 0.5 | 0.0 | 1.0 |
| **NoteChance** | 0 | noteChance | 1.0 | 0.0 | 1.0 |
| **MidiDelay** | 0 | delayBeats | 0.25 | 0.0 | 4.0 |
| | 1 | feedback | 0.0 | 0.0 | 0.95 |
| | 2 | mix | 0.5 | 0.0 | 1.0 |
| **Humanize** | 0 | humanizeTiming | 0.0 | 0.0 | 0.1 |
| | 1 | humanizeVelocity | 0.0 | 0.0 | 1.0 |
| | 2 | humanizePitch | 0.0 | 0.0 | 1.0 |
| **Strum** | 0 | strumTime | 0.02 | 0.0 | 0.2 |
| | 1 | strumDirection | 0 | 0 | 2 |

---

## Files to Modify

| File | Change |
|------|--------|
| `src/engine/MidiFx.h` | Add `MidiFxParamDef`, param defs tables, `MidiFxSlot` param cache + interface |
| `src/engine/MidiFx.cpp` | **Create** — `MidiFxSlot` method implementations, param defs tables |
| `src/engine/Track.h` | Add `getMidiFxChain()` accessor |
| `src/engine/Track.cpp` | Wire `applyAutomation()` in MIDI FX loop; extend automation/modulation for pid ≥ 200 |
| `src/engine/AudioEngineCommands_Fx.cpp` | Real-time param write to live slot instead of full rebuild |
| `src/engine/ReadModelImpl.cpp` | Walk MIDI FX chain in `getAutomatableParams()` |
| `src/common/ReadModel.h` | (optional) Extend `MidiFxSlotSnapshot` with param metadata |
| `src/mcp/McpTools_Audio.cpp` | Add MCP tool `set_midi_fx_param_normalized` or extend existing |
| `frontend/src/components/ModulationPanel.tsx` | Add target selector dropdown |
| `tests/unit/engine/midi_fx_automation_test.cpp` | **Create** — param cache, applyAutomation, modulation wiring |

## Pitfall Gates Triggered

| Gate | Why | Mitigation |
|------|-----|------------|
| **Gate 1: State Not Restored on Rebuild** | `MidiFxSlot` param cache must be populated in `rebuildMidiFXChain()` from the ValueTree, same as audio FX | Add `loadParamsFromTree()` call in `rebuildMidiFXChain()` after creating each slot |
| **Gate 2: Unimplemented Code Path** | Every layer (ValueTree → command → atomic cache → applyAutomation → effect param → ReadModel → frontend) must be wired end-to-end | Test the full path: set param via MCP → verify live effect param changes |
| **Gate 3: Audio-Thread Safety** | `applyAutomation()` runs on the audio thread; must use atomics only | Use `std::atomic<float>[]` + `std::atomic<bool>[]` dirty flags (same pattern as TrackFXSlot) |

## Anti-Pattern Scan

- No `rebuildRoutingGraph()` per-param — the whole point is to avoid full rebuilds
- No heap allocation in `applyAutomation()` — just atomic loads and member writes
- No mutex in the audio path — dirty flags + atomics only
- Batch RPC: `setMidiFxParam` writes ValueTree + atomic in one call

---

## Task 1: Add Param Infrastructure to MidiFxSlot

**Files:**
- Modify: `src/engine/MidiFx.h`
- Create: `src/engine/MidiFx.cpp`

### Step 1: Define `MidiFxParamDef` and param defs tables

In `src/engine/MidiFx.h`, add after the `MidiEffect` base class (after line 26):

```cpp
struct MidiFxParamDef {
    int index;
    const char* name;
    float defaultValue;
    float minValue;
    float maxValue;
};

// Returns the param defs for a given MIDI FX type string.
// Returns empty span for unknown types.
juce::Span<const MidiFxParamDef> getMidiFxParamDefs(const juce::String& type);
int getMidiFxParamCount(const juce::String& type);
```

### Step 2: Add param cache and interface to MidiFxSlot

In `src/engine/MidiFx.h`, expand the `MidiFxSlot` class (currently lines 798-822):

```cpp
class MidiFxSlot {
public:
    MidiFxSlot(std::unique_ptr<MidiEffect> effect, juce::String type);
    void process(juce::MidiBuffer& buffer,
                 const juce::AudioPlayHead::PositionInfo* position,
                 double sampleRate, int numSamples);
    void setBypassed(bool b);
    bool isBypassed() const;
    const juce::String& getType() const;
    MidiEffect* getEffect() const;
    void reset();

    // Automation param interface (mirrors TrackFXSlot pattern)
    void setAutomationParam(int paramIndex, float normalizedValue);
    float getAutomationParam(int paramIndex) const;
    void applyAutomation();
    const std::vector<ParamInfo>& getAutomatableParams() const;
    void loadParamsFromTree(const juce::ValueTree& slotTree);

    struct ParamInfo {
        juce::String name;
        int index;
        float defaultValue;
        float minValue;
        float maxValue;
    };

private:
    std::unique_ptr<MidiEffect> effect_;
    juce::String slotType_;
    std::atomic<bool> bypassed_{ false };

    // Param cache — allocated once during construction
    int numParams_ = 0;
    std::unique_ptr<std::atomic<float>[]> paramValues_;
    std::unique_ptr<std::atomic<bool>[]> paramDirty_;
    std::vector<ParamInfo> cachedParamInfo_;
};
```

### Step 3: Implement param defs tables

Create `src/engine/MidiFx.cpp` with:

```cpp
#include "MidiFx.h"

namespace HDAW {

// Param defs for each MIDI FX type
static const MidiFxParamDef arpeggiatorParams[] = {
    {0, "rate",     0.25f, 0.01f, 2.0f},
    {1, "pattern",  0.0f,  0.0f,  5.0f},
    {2, "octaves",  1.0f,  1.0f,  4.0f},
    {3, "gate",     0.5f,  0.1f,  1.0f},
    {4, "velocity", 100.f, 1.0f, 127.f},
};

static const MidiFxParamDef velocityScalerParams[] = {
    {0, "factor", 1.0f, 0.0f, 2.0f},
};

// ... (all 13 types) ...

juce::Span<const MidiFxParamDef> getMidiFxParamDefs(const juce::String& type)
{
    if (type == "arpeggiator")  return {arpeggiatorParams};
    if (type == "velocity")     return {velocityScalerParams};
    // ... etc for all types ...
    return {};
}

int getMidiFxParamCount(const juce::String& type)
{
    return static_cast<int>(getMidiFxParamDefs(type).size());
}

// MidiFxSlot implementation

MidiFxSlot::MidiFxSlot(std::unique_ptr<MidiEffect> effect, juce::String type)
    : effect_(std::move(effect)), slotType_(std::move(type))
{
    auto defs = getMidiFxParamDefs(slotType_);
    numParams_ = static_cast<int>(defs.size());
    if (numParams_ > 0)
    {
        paramValues_ = std::make_unique<std::atomic<float>[]>(numParams_);
        paramDirty_ = std::make_unique<std::atomic<bool>[]>(numParams_);
        for (int i = 0; i < numParams_; ++i)
        {
            paramValues_[i].store(defs[i].defaultValue, std::memory_order_relaxed);
            paramDirty_[i].store(false, std::memory_order_relaxed);
            cachedParamInfo_.push_back({defs[i].name, defs[i].index,
                                        defs[i].defaultValue,
                                        defs[i].minValue, defs[i].maxValue});
        }
    }
}

void MidiFxSlot::setAutomationParam(int paramIndex, float normalizedValue)
{
    if (paramIndex >= 0 && paramIndex < numParams_)
    {
        paramValues_[paramIndex].store(normalizedValue, std::memory_order_relaxed);
        paramDirty_[paramIndex].store(true, std::memory_order_relaxed);
    }
}

float MidiFxSlot::getAutomationParam(int paramIndex) const
{
    if (paramIndex >= 0 && paramIndex < numParams_)
        return paramValues_[paramIndex].load(std::memory_order_relaxed);
    return 0.0f;
}

void MidiFxSlot::applyAutomation()
{
    if (!effect_ || numParams_ == 0) return;
    for (int i = 0; i < numParams_; ++i)
    {
        if (!paramDirty_[i].load(std::memory_order_relaxed))
            continue;
        paramDirty_[i].store(false, std::memory_order_relaxed);
        float normalized = paramValues_[i].load(std::memory_order_relaxed);
        const auto& def = cachedParamInfo_[i];
        float denormalized = def.minValue + normalized * (def.maxValue - def.minValue);
        applyToEffect(i, denormalized);
    }
}

void MidiFxSlot::applyToEffect(int paramIndex, float value)
{
    // Dispatch to the correct effect member variable
    if (auto* arp = dynamic_cast<Arpeggiator*>(effect_.get()))
    {
        switch (paramIndex) {
            case 0: arp->rate = static_cast<double>(value); break;
            case 1: arp->pattern = static_cast<int>(value); break;
            case 2: arp->octaves = static_cast<int>(value); break;
            case 3: arp->gate = static_cast<double>(value); break;
            case 4: arp->velocity = static_cast<int>(value); break;
        }
    }
    else if (auto* v = dynamic_cast<VelocityScaler*>(effect_.get()))
    {
        if (paramIndex == 0) v->factor = static_cast<double>(value);
    }
    // ... etc for all types ...
}

const std::vector<MidiFxSlot::ParamInfo>& MidiFxSlot::getAutomatableParams() const
{
    return cachedParamInfo_;
}

void MidiFxSlot::loadParamsFromTree(const juce::ValueTree& slotTree)
{
    // Read param values from ValueTree and store in atomic cache
    // Called during rebuildMidiFXChain() to initialize the cache
    auto defs = getMidiFxParamDefs(slotType_);
    for (int i = 0; i < numParams_ && i < static_cast<int>(defs.size()); ++i)
    {
        float val = static_cast<float>(slotTree.getProperty(
            juce::Identifier(defs[i].name), defs[i].defaultValue));
        float normalized = (val - defs[i].minValue) / (defs[i].maxValue - defs[i].minValue);
        paramValues_[i].store(normalized, std::memory_order_relaxed);
    }
}

} // namespace HDAW
```

### Step 4: Add MidiFx.cpp to CMakeLists.txt

Add `src/engine/MidiFx.cpp` to the `HDAW_lib` target's source list.

### Step 5: Build

```
cmake --build build --config Debug
```

---

## Task 2: Wire MidiFxSlot into Track

**Files:**
- Modify: `src/engine/Track.h`
- Modify: `src/engine/Track.cpp`

### Step 1: Add getMidiFxChain() accessor to Track.h

```cpp
const std::vector<std::unique_ptr<MidiFxSlot>>& getMidiFxChain() const { return midiFxChain; }
```

### Step 2: Wire applyAutomation() in the MIDI FX process loop

In `Track::processBlock()`, around line 431-434, change:

```cpp
for (const auto& slot : midiFxChain)
    if (slot)
        slot->process(midiMessages, hasPos ? &midiPos : nullptr,
                      fxSpec.sampleRate, buffer.getNumSamples());
```

to:

```cpp
for (const auto& slot : midiFxChain)
{
    if (slot)
    {
        slot->applyAutomation();
        slot->process(midiMessages, hasPos ? &midiPos : nullptr,
                      fxSpec.sampleRate, buffer.getNumSamples());
    }
}
```

### Step 3: Extend the automation loop for pid ≥ 200

In the automation loop (lines 348-405), add a new branch after the `pid >= 100` block:

```cpp
else if (pid >= 200)
{
    int si = (pid - 200) / 100;
    int pi = (pid - 200) % 100;
    if (si < static_cast<int>(midiFxChain.size()) && midiFxChain[si])
    {
        midiFxChain[si]->setAutomationParam(pi, static_cast<float>(value));
    }
}
```

And for the write-back path (automation recording):

```cpp
else if (pid >= 200)
{
    int si = (pid - 200) / 100;
    int pi = (pid - 200) % 100;
    if (si < static_cast<int>(midiFxChain.size()) && midiFxChain[si])
        currentVal = midiFxChain[si]->getAutomationParam(pi);
}
```

### Step 4: Wire modulation application for pid ≥ 200

In the modulation section (lines 498-513), replace the TODO with actual application:

```cpp
if (modulationLocked)
{
    for (int pidIdx = 0; pidIdx < numUniqueParamIDs; ++pidIdx)
    {
        int pid = uniqueParamIDs[pidIdx];
        float modVal = modulationManager->getModulation(pid, bpm, getSampleRate());
        if (pid == 1)
            modGain = modVal;
        else if (pid == 2)
            modPan = modVal;
        else if (pid >= 200)
        {
            // MIDI FX modulation — write to the slot's param cache
            int si = (pid - 200) / 100;
            int pi = (pid - 200) % 100;
            if (si < static_cast<int>(midiFxChain.size()) && midiFxChain[si])
            {
                // Modulation is additive on top of the base value
                float base = midiFxChain[si]->getAutomationParam(pi);
                midiFxChain[si]->setAutomationParam(pi,
                    juce::jlimit(0.0f, 1.0f, base + modVal));
            }
        }
        else if (pid >= 100)
        {
            // Audio FX modulation — same pattern
            int si = (pid - 100) / 100;
            int pi = (pid - 100) % 100;
            if (si < static_cast<int>(fxChain.size()) && fxChain[si])
            {
                float base = fxChain[si]->getAutomationParam(pi);
                fxChain[si]->setAutomationParam(pi,
                    juce::jlimit(0.0f, 1.0f, base + modVal));
            }
        }
    }
}
```

### Step 5: Populate param cache in rebuildMidiFXChain()

In `Track::rebuildMidiFXChain()`, after creating each `MidiFxSlot`, call `loadParamsFromTree()`:

```cpp
auto slot = std::make_unique<MidiFxSlot>(std::move(effect), type);
slot->setBypassed(static_cast<bool>(slotTree.getProperty(IDs::bypassed, false)));
slot->loadParamsFromTree(slotTree);  // <-- NEW: populate param cache from ValueTree
midiFxChain.push_back(std::move(slot));
```

### Step 6: Build and run existing tests

```
cmake --build build --config Debug
build\Debug\hdaw_tests.exe
```

---

## Task 3: Real-Time MIDI FX Param Updates

**Files:**
- Modify: `src/engine/AudioEngineCommands_Fx.cpp`

### Step 1: Change setMidiFxSlotParam() to use atomic param cache

Replace the current implementation (lines 202-211) which does a full rebuild:

```cpp
void AudioEngineCommands::setMidiFxSlotParam(int trackIndex, int slotIndex,
                                              const std::string& paramName, double value)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto slot = findMidiFxSlot(trackIndex, slotIndex);
    if (!slot.isValid()) return;
    slot.setProperty(juce::Identifier(paramName), value, &um);       // ValueTree write
    if (auto* proc = engine_.getMainProcessor())
        proc->rebuildMidiTrackFX(trackIndex);                         // FULL REBUILD
}
```

With:

```cpp
void AudioEngineCommands::setMidiFxSlotParam(int trackIndex, int slotIndex,
                                              const std::string& paramName, double value)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto slotTree = findMidiFxSlot(trackIndex, slotIndex);
    if (!slotTree.isValid()) return;
    slotTree.setProperty(juce::Identifier(paramName), value, &um);  // ValueTree write (persistence/undo)

    // Real-time update: write to the live slot's atomic param cache
    // instead of doing a full chain rebuild.
    if (auto* proc = engine_.getMainProcessor())
    {
        auto* track = proc->getTrack(trackIndex);
        if (track)
        {
            auto& chain = track->getMidiFxChain();
            if (slotIndex >= 0 && slotIndex < static_cast<int>(chain.size()) && chain[slotIndex])
            {
                auto defs = getMidiFxParamDefs(chain[slotIndex]->getType());
                for (const auto& def : defs)
                {
                    if (paramName == def.name)
                    {
                        float normalized = static_cast<float>(
                            (value - def.minValue) / (def.maxValue - def.minValue));
                        chain[slotIndex]->setAutomationParam(def.index, normalized);
                        break;
                    }
                }
            }
        }
    }
}
```

### Step 2: Build

```
cmake --build build --config Debug
```

---

## Task 4: Expose MIDI FX Params in ReadModel

**Files:**
- Modify: `src/engine/ReadModelImpl.cpp`
- Modify: `src/common/ReadModel.h` (if needed)

### Step 1: Extend getAutomatableParams() to walk MIDI FX chain

In `ReadModelImpl::getAutomatableParams()` (lines 599-632), add after the audio FX loop:

```cpp
// Walk the live MIDI FX chain
auto& midiFxChain = track->getMidiFxChain();
for (int si = 0; si < static_cast<int>(midiFxChain.size()); ++si)
{
    auto& slot = midiFxChain[si];
    if (!slot || slot->isBypassed()) continue;

    const auto& params = slot->getAutomatableParams();
    for (const auto& p : params)
    {
        AutomatableParamSnapshot aps;
        aps.slotIndex = si;
        aps.paramIndex = 200 + si * 100 + p.index;  // MIDI FX paramID range
        aps.name = slot->getType().toStdString() + "." + p.name.toStdString();
        aps.automatable = true;
        result.push_back(aps);
    }
}
```

### Step 2: Build

```
cmake --build build --config Debug
```

---

## Task 5: Add MCP Tool for MIDI FX Param Modulation

**Files:**
- Modify: `src/mcp/McpTools_Audio.cpp`

### Step 1: Add `set_midi_fx_param_normalized` MCP tool

Register a new tool that writes a normalized (0..1) value directly to the MIDI FX param cache, bypassing the ValueTree (for real-time modulation from MCP):

```cpp
s.registerTool({"set_midi_fx_param_normalized",
    "Set a MIDI FX parameter by normalized value (0..1) for real-time modulation.",
    objSchema({{"trackId",     QJsonObject{{"type","integer"}}},
               {"slotIndex",   QJsonObject{{"type","integer"}}},
               {"paramIndex",  QJsonObject{{"type","integer"}}},
               {"value",       QJsonObject{{"type","number"},{"minimum",0.0},{"maximum",1.0}}}},
              {"trackId","slotIndex","paramIndex","value"}),
    [e](const QJsonObject& a) -> McpToolResult {
        int trackId = a["trackId"].toInt();
        int slotIndex = a["slotIndex"].toInt();
        int paramIndex = a["paramIndex"].toInt();
        float value = static_cast<float>(a["value"].toDouble());

        auto* proc = e->getMainProcessor();
        if (!proc) return McpToolResult::error("No audio processor");
        auto* track = proc->getTrack(trackId);
        if (!track) return McpToolResult::error("Track not found");

        auto& chain = track->getMidiFxChain();
        if (slotIndex < 0 || slotIndex >= static_cast<int>(chain.size()) || !chain[slotIndex])
            return McpToolResult::error("MIDI FX slot not found");

        chain[slotIndex]->setAutomationParam(paramIndex, value);
        return McpToolResult::ok("param set");
    }});
```

### Step 2: Build

```
cmake --build build --config Debug
```

---

## Task 6: Add Frontend Target Selector

**Files:**
- Modify: `frontend/src/components/ModulationPanel.tsx`

### Step 1: Add target selector dropdown to each LFO

The `ModulationPanel` currently shows LFO controls but no target selector. Add a `<select>` dropdown that lists available targets (Volume, Pan, Audio FX params, MIDI FX params). The target list comes from `read.getAutomatableParams` which we extended in Task 4.

### Step 2: Wire the target change to `project.setLfoParam`

When the user selects a new target, call `project.setLfoParam` with `targetParamID` set to the selected param's ID (1 for volume, 2 for pan, 100+ for audio FX, 200+ for MIDI FX).

### Step 3: Build frontend

```
cd frontend && npm run build
```

---

## Task 7: Add Tests

**Files:**
- Create: `tests/unit/engine/midi_fx_automation_test.cpp`

### Step 1: Test param defs lookup

```cpp
TEST(MidiFxAutomation, ParamDefsLookup) {
    auto defs = getMidiFxParamDefs("arpeggiator");
    ASSERT_EQ(defs.size(), 5);
    EXPECT_STREQ(defs[0].name, "rate");
    EXPECT_FLOAT_EQ(defs[0].defaultValue, 0.25f);

    auto empty = getMidiFxParamDefs("unknown_type");
    EXPECT_EQ(empty.size(), 0);
}
```

### Step 2: Test MidiFxSlot param cache

```cpp
TEST(MidiFxAutomation, SlotParamCache) {
    auto arp = std::make_unique<Arpeggiator>();
    MidiFxSlot slot(std::move(arp), "arpeggiator");

    EXPECT_EQ(slot.getAutomatableParams().size(), 5);

    slot.setAutomationParam(0, 0.75f);
    EXPECT_FLOAT_EQ(slot.getAutomationParam(0), 0.75f);

    slot.applyAutomation();
    auto* effect = dynamic_cast<Arpeggiator*>(slot.getEffect());
    ASSERT_NE(effect, nullptr);
    // rate = 0.01 + 0.75 * (2.0 - 0.01) = 0.01 + 1.4925 = 1.5025
    EXPECT_NEAR(effect->rate, 1.5025, 0.001);
}
```

### Step 3: Test automation paramID routing

```cpp
TEST(MidiFxAutomation, ParamIDRouting) {
    // Verify that paramID 200+ routes to MIDI FX params
    // This is an integration test that sets a param via the automation
    // path and verifies the live effect changes.
}
```

### Step 4: Add to CMakeLists.txt and run

```
cmake --build build --config Debug
build\Debug\hdaw_tests.exe --gtest_filter=MidiFxAutomation*
```

---

## Verification Checklist

- [ ] `cmake --build build --config Debug` succeeds
- [ ] `build\Debug\hdaw_tests.exe` — all existing tests pass (no regressions)
- [ ] `build\Debug\hdaw_tests.exe --gtest_filter=MidiFxAutomation*` — new tests pass
- [ ] `cd frontend && npm run build` succeeds
- [ ] MCP tool `set_midi_fx_param_normalized` works
- [ ] Modulation target selector shows MIDI FX params in the UI
- [ ] Setting a modulation target to a MIDI FX param produces audible modulation
