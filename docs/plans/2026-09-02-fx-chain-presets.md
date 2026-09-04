# FX Chain Presets Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Save a track's entire FX chain (slot types, order, params, bypass states, plugin states, sampler + psy-fm state) under a name, and recall it onto any track via RPC, MCP, and UI.

**Architecture:** Mirror the pattern-preset file library (`PatternLibrary` → new `ChainLibrary` rooted at `HDAW/chains` with `user/_factory/index.json`); add engine commands + `project.*` RPC routes + `save_fx_chain/...` MCP tools; apply-as-one-undo-transaction with a single rebuild; add a preset bar to `FXChain.tsx`.

**Tech Stack:** C++20, JUCE ValueTree/UndoManager, Qt JSON, existing `project.*` router, MCP tool registry, React 19 + TypeScript.

**Effort:** Medium (~2-3 sessions). **Risk:** Low-Medium — no DSP/audio-thread changes; touches ValueTree write paths, plugin state capture (reuse existing pre-pass, do not reimplement chunking), and sampler path portability. Wide-blast-radius rule does not trigger (no `processBlock`/graph-topology change).

---

## Success Gates (all must pass to declare done)

- [ ] Gate 1: `ChainLibrary` round-trips a 4-slot chain (internal + plugin + sampler + psy_fm) through save → list → load → delete on a temp dir.
- [ ] Gate 2: gtest applies a saved chain to a fresh track and asserts **live processor** slot types + param values + bypass, then `rebuildTrackFX()` and asserts again (lesson-10 seam).
- [ ] Gate 3: `pluginState` base64 survives save → load byte-identical (plugin_state_save_load_test pattern).
- [ ] Gate 4: Chain apply is one undo unit (single `descriptions` entry, undo removes all applied slots).
- [ ] Gate 5: MCP `save_fx_chain → list_fx_chains → load_fx_chain` round-trip passes in `mcp_coverage_test`.
- [ ] Gate 6: `build-fast.bat test` green; no new anti-patterns (new `.cpp` added to `CMakeLists.txt`).

## Dependency Map

- Blast radius: `AudioEngineCommands_Fx.cpp`, `ProjectCommands.h`, `Router_Project.cpp`, `src/mcp/` registry, `FXChain.tsx`. No audio-thread code.
- Upstream: `read.getFxSlots` / `getInternalFxParams` / `getSamplerState` (readers reused for export).
- Downstream: `rebuildTrackFX` (single call per apply), `ReadModel` (unchanged shapes), undo stack.
- God nodes: `AudioEngineCommands` (hub — additive methods only, no signature changes).
- Projections: ValueTree (source of truth) → ReadModel (auto) → audio graph (one rebuild). No delta-sync derivation involved.
- SPSC: none (message-thread only, like all `addFxSlot` paths).

## Pitfall Gates Triggered

- Gate 2 (unimplemented path): trace export→import→apply→audible for every slot kind; a sampler chain that loads without its sample file must warn, not silently pass.
- Gate 4 (stale binaries): verify `build\hdaw_tests.exe` timestamp after `build-fast.bat test`.
- Gate 9 (ids/validation): validate every `param_N` index against `getParamDefsForType` on import (never write stray props); validate `trackId`.
- Lesson 14 (plugin state): reuse `ProjectSerializer` pre-pass capture; never reimplement STATE_CHUNK.
- Lesson 23 (clamping): route all param writes through `setFxSlotParam` so the write-side clamp applies.
- Anti-pattern: batch N slot writes + ONE `rebuildTrackFX` (never rebuild per slot); wrap in one transaction.

---

## Task 1: ChainPreset JSON schema + ChainLibrary storage

**Files:**
- Create: `src/engine/ChainLibrary.h`
- Create: `src/engine/ChainLibrary.cpp`
- Modify: `CMakeLists.txt` (add `src/engine/ChainLibrary.cpp` to the engine source list)
- Test: `tests/unit/engine/chain_library_test.cpp`

- [ ] **Step 1: Write the failing test** — save/list/load/delete round-trip on a temp root:

```cpp
#include <gtest/gtest.h>
#include "engine/ChainLibrary.h"

TEST(ChainLibrary, SaveListLoadDeleteRoundTrip) {
    auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                   .getChildFile("hdaw_chain_test");
    tmp.deleteRecursively();
    ChainLibrary lib(tmp);
    ChainPreset p;
    p.name = "Driven Bass";
    ChainPreset::Slot s;
    s.fxType = "compressor";
    s.bypassed = false;
    s.params = { {"param_0", -12.0}, {"param_1", 4.0} };
    p.slots.push_back(s);
    auto id = lib.savePreset(p);
    EXPECT_FALSE(id.isEmpty());
    auto list = lib.listPresets();
    ASSERT_EQ(list.size(), 1u);
    EXPECT_EQ(list[0].name, "Driven Bass");
    auto loaded = lib.loadPreset(id);
    EXPECT_EQ(loaded.slots.size(), 1u);
    EXPECT_EQ(loaded.slots[0].fxType, "compressor");
    EXPECT_DOUBLE_EQ(loaded.slots[0].params.at("param_0"), -12.0);
    EXPECT_TRUE(lib.deletePreset(id));
    EXPECT_TRUE(lib.listPresets().empty());
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmd /c build-fast.bat test`
Then run: `.\build\hdaw_tests.exe --gtest_filter=ChainLibrary.*`
Expected: FAIL — `engine/ChainLibrary.h` not found (also confirms the new test file is wired into the test build; if CMake doesn't pick up the new test file, add it next to `pattern_library_test.cpp` in the tests source list).

- [ ] **Step 3: Write minimal implementation** — mirror `src/engine/PatternLibrary.h/.cpp` structure (`PatternLibrary.cpp:18-19` root/_factory creation, `:42-45,:280-290` uniquified save, `:106,:186` load by relative path, `:392,:428-429` scan):

```cpp
// src/engine/ChainLibrary.h
#pragma once
#include <juce_core/juce_core.h>
#include <map>
#include <vector>

struct ChainPreset {
    struct PluginRef { juce::String id, format, path, stateBase64; };
    struct Slot {
        juce::String fxType;
        bool bypassed = false;
        juce::String name;
        std::map<juce::String, double> params;          // "param_N" -> real-unit value
        PluginRef plugin;                                // only when fxType == "plugin"
        std::map<juce::String, juce::String> sampler;   // sampleFile, mode, rootNote, ... (strings)
        juce::String slicePoints;                        // space-separated normalized floats
        juce::String psyFmMatrix; double psyFmSweepRate = 0.0;
    };
    juce::String id, name;
    std::vector<Slot> slots;
};

class ChainLibrary {
public:
    explicit ChainLibrary(const juce::File& root);
    static ChainLibrary userLibrary();  // userApplicationDataDirectory/HDAW/chains (mirror McpTools_CompositionPattern.cpp:86-88)
    juce::String savePreset(const ChainPreset& p);   // root/user/<sanitized>.json, uniquified -N
    std::vector<ChainPreset> listPresets();          // scan *.json like PatternLibrary.cpp:392
    ChainPreset loadPreset(const juce::String& id);
    bool deletePreset(const juce::String& id);
private:
    juce::File root_, userDir_;
};
```

Schema version field `"version": 1` in every JSON; unknown future fields ignored on load (forward tolerance). Sampler `sampleFile` stored absolute; resolution fallback on apply lives in Task 2.

- [ ] **Step 4: Run test to verify it passes**

Run: `.\build\hdaw_tests.exe --gtest_filter=ChainLibrary.*`
Expected: PASS.

- [ ] **Step 5: Commit**

Run: `git add src/engine/ChainLibrary.h src/engine/ChainLibrary.cpp tests/unit/engine/chain_library_test.cpp CMakeLists.txt`
Run: `git commit -m "feat: ChainLibrary file storage for FX chain presets"`

---

## Task 2: Engine export/import commands

**Files:**
- Modify: `src/common/ProjectCommands.h` (new virtuals near `:145-160`)
- Modify: `src/engine/AudioEngineCommands.h` (decls near `:168-195`)
- Modify: `src/engine/AudioEngineCommands_Fx.cpp` (impl, follow `:16-104` pattern)
- Test: `tests/unit/engine/fx_chain_preset_test.cpp`

- [ ] **Step 1: Write the failing test** — apply a chain and assert the live processor, rebuild, assert again:

```cpp
TEST(FxChainPreset, ApplySurvivesRebuildLive) {
    // fixture: engine with one track (copy track_mixer_state_test.cpp fixture)
    ChainPreset p; p.name = "T";
    ChainPreset::Slot a; a.fxType = "compressor";
    a.params = { {"param_0", -12.0} };
    ChainPreset::Slot b; b.fxType = "filter";
    b.params = { {"param_0", 800.0} }; b.bypassed = true;
    p.slots = { a, b };
    ASSERT_TRUE(commands.applyFxChain(0, p));
    auto* track = processor->getTrack(0);
    ASSERT_EQ(track->getFXChain().size(), 2u);
    EXPECT_EQ(track->getFXChain()[0]->getType(), "compressor");
    commands.rebuildTrackFX(0);  // RoutingManager::rebuildTrackFX path (RoutingManager.cpp:943-958)
    track = processor->getTrack(0);
    ASSERT_EQ(track->getFXChain().size(), 2u);
    EXPECT_DOUBLE_EQ(track->getFXChain()[0]->getInternalParam(0), -12.0);
    EXPECT_TRUE(track->getFXChain()[1]->isBypassed());
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `.\build\hdaw_tests.exe --gtest_filter=FxChainPreset.*`
Expected: FAIL — `applyFxChain` does not exist.

- [ ] **Step 3: Write minimal implementation**

`ProjectCommands.h` (next to `:145-160`):

```cpp
virtual ChainPreset exportFxChain(int trackIndex) = 0;
virtual bool applyFxChain(int trackIndex, const ChainPreset& preset, juce::String* error = nullptr) = 0;
```

`AudioEngineCommands_Fx.cpp` — export reuses the read path (ReadModelImpl::getFxSlots `:453-478`, getInternalFxParams `:516-551`, getSamplerState `:553+`) **after** the `ProjectSerializer::save`-style plugin-state pre-pass (`ProjectSerializer.cpp:35-87`: loop `proc->getTrack(ti)->getFXChain()`, `getStateInformation`, match by index+`pluginID`):

```cpp
ChainPreset AudioEngineCommands::exportFxChain(int trackIndex) {
    // 1. pre-pass: capture live plugin state into the tree (ProjectSerializer.cpp:74-83 pattern)
    // 2. walk FX_CHAIN children (Track.cpp:740-746 resolveFXChainTree):
    //    fxType, bypassed, name, param_N per getParamDefsForType count,
    //    pluginID/pluginFormat/pluginPath/pluginState,
    //    sampler keys incl. slicePoints, psyFmMatrix/psyFmSweepRate
    // 3. return ChainPreset (no tree mutation, no undo, no rebuild)
}

bool AudioEngineCommands::applyFxChain(int trackIndex, const ChainPreset& preset, juce::String* error) {
    // 1. validate: track exists; every slot fxType known (getParamDefsForType non-empty or "plugin"/"none"/"sampler");
    //    every param index < def count (Gate 9); sampler sampleFile exists else filename-search libraries else warn+skip sample.
    // 2. beginTransaction("Apply FX chain preset") (AudioEngineCommands_Undo.cpp:39-47)
    // 3. remove all existing slots (removeFxSlot path :261, no rebuild), add each via addFxSlotInternal
    //    (AudioEngineCommands.h:433-437 no-rebuild worker), set params via setFxSlotParam (write-side clamp :295-304),
    //    plugin slots via setFxSlotPlugin path (:339-351), sampler/psy-fm setters.
    // 4. ONE proc->rebuildTrackFX(trackIndex); endTransaction(). Return false + error text on any failure.
}
```

Sampler file fallback order: absolute path as stored → library filename search (`search_library` engine side) → apply slot without sample and append `"sample '<file>' not found"` to the returned warning string (Gate 2: warn, never silently pass).

- [ ] **Step 4: Run test to verify it passes**

Run: `.\build\hdaw_tests.exe --gtest_filter=FxChainPreset.*`
Expected: PASS, including the post-rebuild live asserts.

- [ ] **Step 5: Commit**

Run: `git add src/common/ProjectCommands.h src/engine/AudioEngineCommands.h src/engine/AudioEngineCommands_Fx.cpp tests/unit/engine/fx_chain_preset_test.cpp`
Run: `git commit -m "feat: exportFxChain/applyFxChain engine commands"`

---

## Task 3: RPC routes + MCP tools

**Files:**
- Modify: `src/frontend/router/Router_Project.cpp` (branches near `:272-338`)
- Modify: `src/mcp/McpTools_FxSlot.cpp` or create `src/mcp/McpTools_FxChain.cpp` + wire `src/mcp/McpTools_Private.h:44-48`, `src/mcp/McpTools_Fx.cpp:8-15`
- Modify: `frontend/src/rpc/types.ts` + `frontend/src/rpc/client.ts` (chain types, only if UI needs them — Task 4 does)
- Test: `tests/integration/mcp/mcp_coverage_test.cpp` (append `FxChainPresetMcp` suite near `:1786-1920`)

- [ ] **Step 1: Write the failing test** (MCP round-trip):

```cpp
TEST_F(McpCoverage, FxChainPresetRoundTrip) {
    auto r1 = callTool("add_fx", {{"trackId", 0}, {"fxType", "compressor"}});
    ASSERT_TRUE(r1.ok);
    auto r2 = callTool("save_fx_chain", {{"trackId", 0}, {"name", "MCP Test"}});
    ASSERT_TRUE(r2.ok);
    auto r3 = callTool("list_fx_chains", {});
    ASSERT_TRUE(r3.ok); EXPECT_TRUE(r3.text.contains("MCP Test"));
    ASSERT_TRUE(callTool("remove_fx", {{"trackId", 0}, {"slotIndex", 0}}).ok);
    auto r4 = callTool("load_fx_chain", {{"trackId", 0}, {"name", "MCP Test"}});
    ASSERT_TRUE(r4.ok);
    EXPECT_EQ(getMainProcessor()->getTrack(0)->getFXChain().size(), 1u);  // live, not ReadModel
}
```

(Follow the exact `callTool` helper shape used at `mcp_coverage_test.cpp:1786-1832`.)

- [ ] **Step 2: Run test to verify it fails**

Run: `.\build\hdaw_tests.exe --gtest_filter=McpCoverage.FxChainPresetRoundTrip`
Expected: FAIL — unknown tool `save_fx_chain`.

- [ ] **Step 3: Write minimal implementation** — RPC branches in `Router_Project.cpp` (helpers `requireInt/requireString`, error `makeError(-32602,...)` per `:272-338`):

```cpp
if (m == "saveFxChainPreset") { /* requireInt trackIndex, requireString name → lib.savePreset(exportFxChain(i)) → {id} */ }
if (m == "listFxChainPresets") { /* → [{id,name,slotCount}] */ }
if (m == "loadFxChainPreset") { /* require id or name → applyFxChain → {ok,warnings[]} */ }
if (m == "deleteFxChainPreset") { /* → {ok} */ }
```

MCP tools (`save_fx_chain`, `list_fx_chains`, `load_fx_chain`, `delete_fx_chain`, category `"fx"`), registered beside `registerFxSlotTools` and aggregated through `registerFxTools` (`McpTools_Fx.cpp:8-15`); `load_fx_chain` returns the warnings array verbatim. `delete` refuses nothing (user presets only; factory chains, if ever added, are read-only by path prefix like `PatternLibrary` factory dir).

- [ ] **Step 4: Run test to verify it passes**

Run: `.\build\hdaw_tests.exe --gtest_filter=McpCoverage.FxChainPresetRoundTrip`
Expected: PASS.

- [ ] **Step 5: Commit**

Run: `git add src/frontend/router/Router_Project.cpp src/mcp/McpTools_FxChain.cpp src/mcp/McpTools_Fx.cpp src/mcp/McpTools_Private.h tests/integration/mcp/mcp_coverage_test.cpp`
Run: `git commit -m "feat: fx chain preset RPC routes and MCP tools"`

---

## Task 4: FXChain.tsx preset bar

**Files:**
- Modify: `frontend/src/components/FXChain.tsx`
- Modify: `frontend/src/components/FXChain.css` (theme tokens only)
- Test: `frontend/src/components/FXChain.test.tsx` (extend) + `frontend/e2e/fxChainPreset.spec.ts` (new)

- [ ] **Step 1: Write the failing test** (Vitest — chain bar renders, applies a preset):

```tsx
it("applies a chain preset from the dropdown", async () => {
  (rpc.call as jest.Mock)
    .mockResolvedValueOnce({ slots: [] })                       // read.getFxSlots
    .mockResolvedValueOnce([{ id: "1", name: "Driven", slotCount: 2 }]) // project.listFxChainPresets
    .mockResolvedValueOnce({ ok: true, warnings: [] });         // project.loadFxChainPreset
  render(<FXChain trackIndex={0} />);
  fireEvent.click(screen.getByTitle("FX chain presets"));
  fireEvent.click(await screen.findByText("Driven"));
  await waitFor(() => expect(rpc.call).toHaveBeenCalledWith(
    "project.loadFxChainPreset", expect.objectContaining({ trackIndex: 0 })));
});
```

(Follow existing `FXChain.test.tsx` rpc-mock shape.)

- [ ] **Step 2: Run test to verify it fails**

Run: `npm test -- FXChain`
Expected: FAIL — no "FX chain presets" control.

- [ ] **Step 3: Write minimal implementation** — a preset row docked in the FXChain panel header (no floating windows; spatial-stability rule): `<select>` populated from `project.listFxChainPresets`, Apply button → `project.loadFxChainPreset` (show `warnings[]` in the existing Toaster), Save button → `prompt`-free inline name input → `project.saveFxChainPreset`. Reuse the optimistic + rollback mutation style (`addSlot :157-184`). CSS: `var(--accent)`, `var(--bg-panel)` etc. only — `grep -rn "#[0-9a-f]"` on the new CSS must return zero hits (Gate 8). E2E: drive the real app, apply a preset, assert `.fx-slot` count changes (poll with `expect.toPass`, 16 ms delta debounce).

- [ ] **Step 4: Run tests to verify they pass**

Run: `npm test -- FXChain`
Run: `npm run test:e2e -- fxChainPreset`
Expected: PASS.

- [ ] **Step 5: Commit**

Run: `git add frontend/src/components/FXChain.tsx frontend/src/components/FXChain.css frontend/src/components/FXChain.test.tsx frontend/e2e/fxChainPreset.spec.ts`
Run: `git commit -m "feat: FX chain preset bar in FXChain panel"`

---

## Task 5: Docs, parity flips, graph refresh

**Files:**
- Modify: `FEATURES.md:50`, `feature_parity.md:70`, `roadmap.md:68` (❌→✅ + tool names)
- Modify: `docs/psytrance-composition-guide.md` (chain-preset recipe section)
- Run: codebase-memory `index_repository` (mode `fast`, repo_path `D:\pdf\roo projects\hdaw3`)

- [ ] **Step 1:** Flip the three backlog markers and document the four MCP tools + four RPC routes with one Jordan cave-dub example (save "Dusty Skank" chain: sampler → filter + LFO → delay).
- [ ] **Step 2:** Full suite gate — `cmd /c build-fast.bat all`, `.\build\hdaw_tests.exe` (zero failures), `npm test` green.
- [ ] **Step 3:** Refresh the knowledge graph so blast-radius queries see the new commands.
- [ ] **Step 4: Commit**

Run: `git add FEATURES.md feature_parity.md roadmap.md docs/psytrance-composition-guide.md`
Run: `git commit -m "docs: FX chain presets parity and recipe"`

---

## Self-Review

- [x] Spec coverage: save/list/load/delete/import/export? — import/export file transport deferred (YAGNI; JSON files are already portable via the chains dir). All requested surfaces (RPC+MCP+UI) have tasks.
- [x] Placeholder scan: no TBDs; every step names files, code, commands, expected output.
- [x] Type consistency: `ChainPreset`/`ChainPreset::Slot` shapes identical across Tasks 1–4; RPC field names (`trackIndex`, `name`/`id`) match MCP args.
