# Plan: Plugin Preset Scan & Cache

## Goal

Add a persistent preset/program cache to the plugin scanner so that `list_plugins` and `list_plugin_presets` MCP tools can report which plugins expose host-enumerable presets without requiring a live plugin instance in the audio graph.

## Success Gates (all must pass)

- [ ] Gate 1: `cmake --build build --config Debug` succeeds
- [ ] Gate 2: `hdaw_tests.exe` — new `PresetCacheTest` suite passes (scan mock plugin, verify cache round-trip, verify `list_plugins` returns `hasPresets`/`presetCount`)
- [ ] Gate 3: `hdaw_tests.exe` — existing `McpServerTest` / `McpFunctionalityTest` suites pass (no regression)
- [ ] Gate 4: MCP `list_plugins` tool returns `hasPresets` (bool) and `presetCount` (int) for each plugin
- [ ] Gate 5: MCP `list_plugin_presets` tool checks cache before querying live instance; returns cached presets when available, falls back to live query
- [ ] Gate 6: `preset_cache.xml` is created at `%APPDATA%/HDAW/preset_cache.xml` after a scan, persists across engine restarts
- [ ] Gate 7: Cache is loaded at startup and available before any plugin is instantiated in the audio graph
- [ ] Gate 8: `graphify . --update` run after implementation

## Dependency Map

### Blast radius (from graph query)

- **Modified nodes**: `PluginManager` (scan/cache), `PluginScannerMain` (child process), `McpTools_Project.cpp` (`list_plugins`), `McpTools_Audio.cpp` (`list_plugin_presets`)
- **Communities touched**: Plugin Infrastructure (scanning/cache), MCP Server (tool surface), Proxy/Isolation (child process IPC)
- **God nodes in scope**: `PluginManager` — high-degree hub connecting scanning, caching, instantiation, and recovery. Handle with care.

### Upstream callers

| Caller | What it calls | Impact |
|--------|--------------|--------|
| `scanAll()` | `scanPluginIsolated()` | Modified: ScanResult gains `numPrograms`, `programNames` |
| `onScanFinished()` | `saveCache()` | Modified: also saves preset cache |
| `PluginManager constructor` | `loadCache()` | Modified: also loads preset cache |
| `list_plugins` MCP tool | `getPlugins()` | Modified: enriches output with preset data |
| `list_plugin_presets` MCP tool | `getFxProgramList()` | Modified: checks preset cache first |

### Downstream consumers

| Consumer | What it reads | Impact |
|----------|--------------|--------|
| `FXChain.tsx` preset browser | `pluginParam.listPrograms` RPC | No change (uses live instance path) |
| `list_plugin_presets` MCP tool | `getFxProgramList()` | Modified: cache-first lookup |
| `list_plugins` MCP tool | `getPlugins()` | Modified: includes preset metadata |

### Projections affected

- **None** — preset cache is a side-channel data store, not part of the ValueTree/ReadModel/AudioGraph projections. No delta/fullSync impact.

### SPSC paths touched

- **None** — preset scanning happens in the scanner subprocess (message thread), cache is read/written on message thread. No audio-thread involvement.

### Community boundaries crossed

- Plugin Infrastructure ↔ MCP Server: new data flows from cache to MCP tool output
- Plugin Infrastructure ↔ Proxy/Isolation: scanner child process gains program enumeration

## Pitfall Gates Triggered

### Gate 2: Unimplemented Code Path Silently Failing

**Risk**: New cache data structure exists but MCP tools don't read it, or cache is never populated.

**Mitigation**: 
- `PresetCacheTest` verifies full round-trip: scan → cache → MCP tool reads cache
- Trace full path: `PluginScannerMain` outputs programs → `scanPluginIsolated` parses → `savePresetCache` persists → `loadPresetCache` loads → `list_plugins` reads → `list_plugin_presets` reads

### Gate 3: Audio-Thread Safety Violations

**Risk**: None — all preset cache I/O happens on message thread / scanner subprocess.

**Verification**: No new code in `processBlock`, `Track`, `RoutingManager`, or `TrackFXSlot`.

### Gate 4: Build / Packaging Stale Binaries

**Risk**: `PluginScannerMain.cpp` changes require `hdaw_plugin_scanner.exe` to be rebuilt.

**Verification**: Build succeeds, scanner exe timestamp is newer than source.

### Gate 9: ID Namespace Collisions / Missing Validation

**Risk**: Plugin identifier used as cache key — must match exactly between scan-time and query-time.

**Mitigation**: Use `PluginDescription.createIdentifierString()` (same as `list_plugins` returns) as the canonical key.

## Anti-Pattern Scan

- No N-loop RPC calls (this is C++ engine work, not frontend)
- No SPSC violations (no audio-thread code touched)
- No full-tree walks (preset cache is a separate file, not ValueTree)
- New `.cpp` files added to `CMakeLists.txt` if any

## Implementation Plan

### Step 1: Extend `PluginScannerMain.cpp` — probe programs during scan

**File**: `src/proxy/scanner/PluginScannerMain.cpp`

After `instance` is created (line 55) and before printing JSON output:

```cpp
// Probe program/preset enumeration
int numPrograms = instance->getNumPrograms();
obj->setProperty("numPrograms", numPrograms);
if (numPrograms > 1) {
    // Only enumerate if plugin reports more than 1 program
    // (1 program = trivially "Init", not useful presets)
    auto* programNames = new juce::Array<juce::String>();
    for (int i = 0; i < numPrograms; ++i)
        programNames->add(instance->getProgramName(i));
    obj->setProperty("programNames", juce::JSON::toString(juce::var(*programNames)));
    delete programNames;
}
```

**Why > 1**: Plugins with exactly 1 program (e.g., many FX plugins) have a trivial "Init" program that's not useful as a preset. We only cache preset names when `numPrograms > 1`.

**Safety**: This runs in the isolated scanner subprocess. If `getNumPrograms()` or `getProgramName()` crashes, the dead-man's-pedal handles it (plugin gets blacklisted as crash).

### Step 2: Extend `ScanResult` struct and `scanPluginIsolated` parsing

**File**: `src/engine/PluginManager.h` — add to `ScanResult`:
```cpp
int numPrograms = 0;
juce::StringArray programNames;
```

**File**: `src/engine/PluginManager.cpp` — in `scanPluginIsolated()` (line ~452-461), parse the new fields:
```cpp
result.numPrograms = obj->hasProperty("numPrograms") ? static_cast<int>(obj->getProperty("numPrograms")) : 0;
if (obj->hasProperty("programNames"))
    result.programNames = juce::StringArray::fromTokens(obj->getProperty("programNames").toString(), ",", "");
```

### Step 3: Add preset cache data structure and persistence to `PluginManager`

**File**: `src/engine/PluginManager.h` — add:
```cpp
// Preset cache: maps plugin identifier string → preset info
struct PluginPresetInfo {
    int numPrograms = 0;
    juce::StringArray programNames;
};
std::unordered_map<juce::String, PluginPresetInfo> presetCache;
juce::File presetCacheFile;

void loadPresetCache();
void savePresetCache();
const PluginPresetInfo* getPresetInfo(const juce::String& pluginId) const;
```

**File**: `src/engine/PluginManager.cpp`:

Constructor — add:
```cpp
presetCacheFile = hdawDir.getChildFile("preset_cache.xml");
```

`loadPresetCache()` — read XML file, populate `presetCache` map:
```cpp
void PluginManager::loadPresetCache()
{
    if (!presetCacheFile.existsAsFile()) return;
    auto xml = juce::XmlDocument::parse(presetCacheFile);
    if (!xml) return;
    for (auto* child : xml->getChildIterator())
    {
        if (child->getTagName() == "PLUGIN")
        {
            PluginPresetInfo info;
            info.numPrograms = child->getIntAttribute("numPrograms", 0);
            auto namesStr = child->getStringAttribute("programNames", {});
            if (namesStr.isNotEmpty())
                info.programNames = juce::StringArray::fromTokens(namesStr, "|", {});
            auto id = child->getStringAttribute("id", {});
            if (id.isNotEmpty())
                presetCache[id] = info;
        }
    }
}
```

`savePresetCache()` — serialize map to XML:
```cpp
void PluginManager::savePresetCache()
{
    presetCacheFile.getParentDirectory().createDirectory();
    juce::XmlElement root("PRESET_CACHE");
    for (const auto& [id, info] : presetCache)
    {
        auto* child = root.createNewChildElement("PLUGIN");
        child->setAttribute("id", id);
        child->setAttribute("numPrograms", info.numPrograms);
        if (info.programNames.isNotEmpty())
            child->setAttribute("programNames", info.programNames.joinIntoString("|"));
    }
    root.writeTo(presetCacheFile, {});
}
```

`getPresetInfo()` — const lookup:
```cpp
const PluginManager::PluginPresetInfo* PluginManager::getPresetInfo(const juce::String& pluginId) const
{
    auto it = presetCache.find(pluginId);
    return it != presetCache.end() ? &it->second : nullptr;
}
```

### Step 4: Populate cache during scan

**File**: `src/engine/PluginManager.cpp` — in `scanAll()`, after `scanPluginIsolated()` succeeds (line ~196-210):

```cpp
if (scanResult.ok)
{
    // ... existing desc population ...

    // Cache preset info if plugin reports programs
    if (scanResult.numPrograms > 1)
    {
        PluginPresetInfo info;
        info.numPrograms = scanResult.numPrograms;
        info.programNames = scanResult.programNames;
        auto id = desc.createIdentifierString();
        presetCache[id] = info;
        juce::Logger::writeToLog("PluginManager: cached " + juce::String(scanResult.numPrograms)
                                 + " presets for " + scanResult.name);
    }
}
```

In `onScanFinished()` (line ~348-377) — add `savePresetCache()` call:
```cpp
void PluginManager::onScanFinished()
{
    // ... existing code ...
    saveCache();
    savePresetCache();  // ADD THIS
    scanning.store(false);
    // ...
}
```

In `loadCache()` — add `loadPresetCache()` call:
```cpp
void PluginManager::loadCache()
{
    // ... existing code ...
    loadPresetCache();  // ADD THIS
}
```

### Step 5: Enrich `list_plugins` MCP tool output

**File**: `src/mcp/McpTools_Project.cpp` — in `registerProjectDomain()`, modify `list_plugins` handler (line ~1028-1042):

```cpp
s.registerTool({"list_plugins", "List all scanned plugins.",
    objSchema({}),
    [e](const QJsonObject&) {
        auto& pm = e->getPluginManager();
        QJsonArray arr;
        for (const auto& pd : pm.getPlugins()) {
            QJsonObject o;
            o["name"] = jstr(pd.name);
            o["manufacturer"] = jstr(pd.manufacturerName);
            o["format"] = jstr(pd.pluginFormatName);
            o["category"] = jstr(pd.category);
            o["id"] = jstr(pd.createIdentifierString());
            // NEW: preset metadata from cache
            auto* presetInfo = pm.getPresetInfo(pd.createIdentifierString());
            if (presetInfo && presetInfo->numPrograms > 1) {
                o["hasPresets"] = true;
                o["presetCount"] = presetInfo->numPrograms;
            } else {
                o["hasPresets"] = false;
                o["presetCount"] = 0;
            }
            arr.append(o);
        }
        return McpToolResult::text(
            QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
    }});
```

### Step 6: Update `list_plugin_presets` MCP tool to use cache

**File**: `src/mcp/McpTools_Audio.cpp` — modify `list_plugin_presets` handler (line ~177-196):

```cpp
s.registerTool({"list_plugin_presets",
    "List all preset/program names of a plugin FX slot.",
    objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
              {"slotIndex", QJsonObject{{"type","integer"}}}},
             {"trackId","slotIndex"}),
    [e](const QJsonObject& a) -> McpToolResult {
        int ti = a.value("trackId").toInt();
        int si = a.value("slotIndex").toInt();
        auto fxSlots = e->getReadModel().getFxSlots(ti);
        if (si < 0 || si >= (int)fxSlots.size())
            return McpToolResult::text("slot not found", true);
        if (fxSlots[si].fxType != "plugin")
            return McpToolResult::text("slot is not a plugin", true);

        // NEW: Try cache first (by pluginId)
        auto pluginId = fxSlots[si].pluginId.toStdString();
        auto* presetInfo = e->getPluginManager().getPresetInfo(juce::String(pluginId));
        if (presetInfo && presetInfo->numPrograms > 1)
        {
            QJsonArray arr;
            for (int i = 0; i < presetInfo->numPrograms; ++i)
            {
                juce::String name = i < presetInfo->programNames.size()
                    ? presetInfo->programNames[i]
                    : juce::String("Preset ") + juce::String(i);
                arr.append(QJsonObject{{"index", i}, {"name", name}});
            }
            return McpToolResult::text(
                QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
        }

        // Fallback: live query from instantiated plugin
        auto progs = e->getFxProgramList(ti, si);
        QJsonArray arr;
        for (const auto& p : progs)
            arr.append(QJsonObject{{"index", p.index}, {"name", QString::fromStdString(p.name)}});
        return McpToolResult::text(
            QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
    }});
```

### Step 7: Add tests

**New file**: `tests/unit/engine/preset_cache_test.cpp`

```cpp
// Test 1: ScanResult parsing includes numPrograms
// Test 2: Preset cache save/load round-trip
// Test 3: getPresetInfo returns nullptr for unknown plugin
// Test 4: getPresetInfo returns correct data for cached plugin
// Test 5: Cache persists across load/save cycles
// Test 6: list_plugins MCP tool includes hasPresets/presetCount
// Test 7: list_plugin_presets MCP tool uses cache when available
// Test 8: list_plugin_presets falls back to live query when cache miss
```

Use `PassthroughPlugin` (from `tests/test-plugin/PassthroughPlugin.h`) which already has 2 programs ("Init" and "Test Preset") — see `isolation_integration_test.cpp:1331`.

### Step 8: Update `list_plugin_presets` description

Update the tool description to mention the cache behavior:

```
"List all preset/program names of a plugin FX slot. Uses the preset cache when available (populated during plugin scanning); falls back to querying the live plugin instance."
```

### Step 9: Refresh knowledge graph

```bash
graphify . --update
```

## Files Modified

| File | Change |
|------|--------|
| `src/proxy/scanner/PluginScannerMain.cpp` | Add `getNumPrograms()`/`getProgramName()` probing, output to JSON |
| `src/engine/PluginManager.h` | Add `PluginPresetInfo` struct, `presetCache` map, `presetCacheFile`, cache methods |
| `src/engine/PluginManager.cpp` | Add `loadPresetCache()`/`savePresetCache()`/`getPresetInfo()`, extend `ScanResult` parsing, populate cache in `scanAll()`, call save/load |
| `src/mcp/McpTools_Project.cpp` | Enrich `list_plugins` output with `hasPresets`/`presetCount` |
| `src/mcp/McpTools_Audio.cpp` | Update `list_plugin_presets` to check cache first, fallback to live query |
| `tests/unit/engine/preset_cache_test.cpp` | New test suite for cache round-trip and MCP tool integration |

## Verification Commands

```bash
cmake --build build --config Debug
build\Debug\hdaw_tests.exe --gtest_filter=PresetCacheTest.*
build\Debug\hdaw_tests.exe --gtest_filter=McpServerTest.*
build\Debug\hdaw_tests.exe --gtest_filter=McpFunctionalityTest.*
```

## Open Questions

1. **Cache staleness**: If a user updates a plugin (new version with different presets), the cache becomes stale. Options:
   - (a) Re-scan clears and rebuilds preset cache (simplest — chosen approach)
   - (b) Version-aware cache invalidation (complex, not worth it now)
   - (c) "Rescan presets" button in UI (future enhancement)

2. **Vital-like plugins**: `getNumPrograms()` returns 0 for Vital. The cache correctly reports `hasPresets: false`. Bridging Vital's internal preset system is a separate feature (parsing `.vitalpreset` files from known directories) — out of scope for this plan.

3. **Large preset lists**: Some plugins (Dexed, Surge) have 1000+ presets. Caching all names in XML is fine (text is small). No pagination needed for the cache; the MCP tool returns the full list.
