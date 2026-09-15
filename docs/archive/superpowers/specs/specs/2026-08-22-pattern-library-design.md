# Pattern Library & Extended Generation Styles

**Date:** 2026-08-22
**Status:** Approved
**Version:** 1

## Goal

Expand the phrase/pattern generator with 15 new styles across 6 categories, and add a storable JSON format for patterns that enables user presets, a shipped factory library, and external import/export.

## Scope

- 15 new `PhraseGenerator` styles (indices 10–24)
- JSON pattern format (params-only, deterministic)
- Pattern library (user presets in `%APPDATA%/HDAW/patterns/`, factory presets bundled)
- RPC + MCP tools for pattern CRUD
- Frontend preset browser in `PhraseGeneratorDialog`

## Non-Goals (for this iteration)

- Composite/layered patterns (style index 24 `Layered` is a stub — full layering system deferred)
- Pattern sharing/sync between users
- Audio preview of patterns before insertion
- Clip-level pattern storage (patterns live in the library, not in clips)

---

## 1. New Generation Styles

### 1.1 Style Enum Extension

Add to `PhraseGenerator::Style` enum (current range 0–9, new range 10–24):

| Index | Name | Category | Description |
|-------|------|----------|-------------|
| 10 | `TrapHiHat` | Trap/Drill | 32nd-note rolls with ratchet bursts, configurable roll density and velocity decay |
| 11 | `DrillBass` | Trap/Drill | Displaced 808 patterns with pitch slides (glide-to-note), long sustain tails |
| 12 | `Counterpoint` | Classical/Jazz | 2-voice species counterpoint — configurable voice count, interval constraints |
| 13 | `WalkingBass` | Classical/Jazz | Stepwise root-5th approach-note bass, chord-tone targeting on downbeats |
| 14 | `SwingComping` | Classical/Jazz | Syncopated chord stabs with swing feel, Charleston/shifted-beat patterns |
| 15 | `MarkovMelody` | Generative/Ambient | Markov-chain pitch transitions with configurable transition matrix, evolving rhythms |
| 16 | `EvolvingTexture` | Generative/Ambient | Slowly evolving pad/textural layers — long notes with timbral drift over time |
| 17 | `Aleatoric` | Generative/Ambient | Random choices within constraint bands — density, pitch range, rhythmic grid |
| 18 | `ScalarRun` | Melodic Sequence | Stepwise scalar passages, configurable direction (up/down/bounce), octave range |
| 19 | `ChordToneSeq` | Melodic Sequence | Arpeggiated chord tones with approach notes, follows chord progression |
| 20 | `CallResponse` | Melodic Sequence | Phrased melody with antecedent/consequent structure, rest gaps between phrases |
| 21 | `PhaseShift` | Polyrhythm/Minimal | Two voices on different grids gradually phase against each other |
| 22 | `AdditiveRhythm` | Polyrhythm/Minimal | Rhythms built from additive grouping (e.g., 3+3+2 over 8) |
| 23 | `MinimalistLoop` | Polyrhythm/Minimal | Short repeating cells with gradual pattern mutation (Reich-style) |
| 24 | `Layered` | Combinator | Combines 2-3 sub-patterns (each referencing another style) with layer rules — **stub for future** |

### 1.2 Style-Specific Parameters

Each new style adds fields to a `StyleParams` variant (or nested struct). The `getStyleParams` RPC returns the schema for dynamic UI rendering.

**TrapHiHat:**
- `rollDensity` (int, 2–8, default 4): notes per beat in roll bursts
- `velocityDecay` (float, 0.1–1.0, default 0.7): velocity multiplier per successive roll note
- `ratchetChance` (float, 0.0–1.0, default 0.3): probability of a ratchet burst at each hit

**DrillBass:**
- `glideDuration` (float, 0.05–0.5, default 0.15): portamento time in beats
- `slideIntensity` (float, 0.0–1.0, default 0.8): pitch slide range (0=none, 1=full octave)
- `sustainTail` (bool, default true): extend last note of each phrase to fill gap
- `displacement` (float, 0.0–1.0, default 0.5): how far kick patterns are displaced from grid

**Counterpoint:**
- `voiceCount` (int, 2–4, default 2): number of independent voices
- `species` (int, 1–5, default 2): species of counterpoint (1st=note-against-note, 2nd=two-against-one, etc.)
- `intervalConstraint` (int, 0–3, default 1): 0=parallel, 1=contrary, 2=oblique, 3=free

**WalkingBass:**
- `approachNotes` (bool, default true): use chromatic approach notes on non-downbeats
- `ghostNotes` (float, 0.0–1.0, default 0.1): chance of ghost note (low velocity)
- `chromaticism` (float, 0.0–1.0, default 0.3): how chromatic vs diatonic the walking line is

**SwingComping:**
- `swingPercent` (int, 50–75, default 65): swing ratio (50=straight, 75=triplet)
- `compPattern` (int, 0–3, default 0): 0=Charleston, 1=shifted-beat, 2=sparse, 3=dense
- `voicingSpread` (int, 0–2, default 1): 0=close, 1=open, 2=spread

**MarkovMelody:**
- `transitionMatrix` (string, default "major"): name of transition matrix preset (major, minor, blues, chromatic, custom)
- `rhythmGrid` (int, 4/8/16/32, default 16): rhythmic grid resolution
- `stateCount` (int, 3–12, default 7): number of Markov states (scale degrees)

**EvolvingTexture:**
- `layerCount` (int, 2–8, default 4): number of sustained voices
- `driftSpeed` (float, 0.1–2.0, default 0.5): how fast pitches drift (beats per semitone)
- `densitySwell` (float, 0.0–1.0, default 0.5): how much density increases over the phrase

**Aleatoric:**
- `constraintTightness` (float, 0.0–1.0, default 0.5): 0=fully random, 1=tightly constrained
- `rhythmVariety` (float, 0.0–1.0, default 0.7): variety of rhythmic values used
- `restProbability` (float, 0.0–1.0, default 0.2): chance of rest at each grid position

**ScalarRun:**
- `direction` (int, 0–2, default 0): 0=up, 1=down, 2=bounce (reverse at boundaries)
- `octaveSpan` (int, 1–4, default 2): number of octaves to traverse
- `runSpeed` (int, 4/8/16, default 16): note grid for the run

**ChordToneSeq:**
- `approachType` (int, 0–3, default 1): 0=none, 1=chromatic, 2=scalar, 3=neighbor tone
- `patternShape` (int, 0–3, default 0): 0=ascending, 1=descending, 2=up-down, 3=random
- `targetChords` (string, default ""): optional chord progression to follow (e.g., "I-IV-V-I")

**CallResponse:**
- `phraseLength` (int, 2–8, default 4): beats per phrase
- `responseVariation` (float, 0.0–1.0, default 0.5): how much the response differs from the call
- `restBeats` (float, 0–2, default 1): rest between call and response

**PhaseShift:**
- `voice1Grid` (int, 4/8/16, default 8): grid for voice 1
- `voice2Grid` (int, 4/8/16, default 6): grid for voice 2 (different from voice 1)
- `phaseRate` (float, 0.1–1.0, default 0.3): how fast the phase relationship changes

**AdditiveRhythm:**
- `grouping` (string, default "3+3+2"): additive grouping string (e.g., "5+3", "2+2+3+1")
- `subdivision` (int, 4/8/16, default 8): base subdivision
- `accentPattern` (string, default ""): optional accent pattern (louder on group boundaries)

**MinimalistLoop:**
- `cellLength` (int, 3–12, default 6): notes per repeating cell
- `mutationRate` (float, 0.0–1.0, default 0.2): probability of a note changing per repetition
- `phaseOffset` (int, 0–11, default 0): rhythmic offset for the repeating cell

---

## 2. JSON Pattern Format

### 2.1 Schema

```json
{
  "version": 1,
  "name": "string (required, 1-64 chars)",
  "description": "string (optional, max 256 chars)",
  "category": "string (required: trap|jazz|ambient|melodic|polyrhythm|user)",
  "tags": ["string array, optional, max 10 tags"],
  "author": "string (optional, default 'User')",
  "createdAt": "ISO 8601 timestamp (auto-set on save)",
  "style": "string (required: Style enum name, e.g. 'TrapHiHat')",
  "params": {
    "scaleRoot": "int 0-11 (default 0)",
    "scaleMode": "int 0-12 (default 0)",
    "lowNote": "int 24-96 (default 48)",
    "highNote": "int 36-127 (default 84)",
    "minVelocity": "int 1-127 (default 60)",
    "maxVelocity": "int 1-127 (default 110)",
    "seed": "uint64 (default 0)",
    "lengthBeats": "double > 0 (default 4.0)",
    "density": "int > 0 (default 8)",
    "noteDuration": "double > 0 (default 0.5)"
  },
  "styleParams": {
    "fieldName": "value (style-specific, see §1.2)"
  }
}
```

### 2.2 Validation Rules

- `version` must be ≤ current schema version (1). Unknown future versions → error.
- `style` must match a known `Style` enum name. Unknown → error.
- `params` fields clamped to valid ranges on load (with warning log).
- `styleParams` fields validated against `getStyleParams` schema for the given style. Unknown fields → ignored with warning.
- `name` sanitized for filesystem: no `<>:"/\|?*`, max 64 chars, stripped of leading/trailing whitespace.

### 2.3 Versioning

- `version` field incremented on schema-breaking changes.
- Forward-compatible: version N loader reads ≤N files. Unknown fields are ignored.
- Backward-compatible: version N+1 loader reads version N files, applying defaults for new fields.

---

## 3. Pattern Library

### 3.1 File System Layout

```
%APPDATA%/HDAW/patterns/
├── _factory/                    # Shipped patterns (read-only)
│   ├── trap/
│   │   ├── dark-drill-bass.json
│   │   └── trap-hihat-rolls.json
│   ├── jazz/
│   │   ├── walking-bass-walk.json
│   │   └── swing-charleston.json
│   ├── ambient/
│   │   ├── evolving-pad.json
│   │   └── aleatoric-cloud.json
│   ├── melodic/
│   │   ├── scalar-run-up.json
│   │   └── call-response-jazz.json
│   └── polyrhythm/
│       ├── phase-shift-8-6.json
│       └── additive-332.json
├── user/                        # User-saved presets
│   └── <name>.json
└── index.json                   # Auto-generated catalog
```

### 3.2 Index File (`index.json`)

Auto-generated on startup and after any save/delete/import. Enables fast UI listing without scanning individual files.

```json
{
  "version": 1,
  "generatedAt": "2026-08-22T10:30:00Z",
  "patterns": [
    {
      "id": "factory/trap/dark-drill-bass",
      "path": "_factory/trap/dark-drill-bass.json",
      "name": "Dark Drill Bass",
      "style": "DrillBass",
      "category": "trap",
      "tags": ["808", "drill", "bass"],
      "source": "factory"
    }
  ]
}
```

- `id`: unique identifier, format `<source>/<category>/<filename-without-ext>`
- `source`: `"factory"` or `"user"`
- Factory patterns cannot be deleted or overwritten by user operations

### 3.3 Factory Pattern Bundling

Factory patterns are shipped in a `patterns/` directory adjacent to the executable (not embedded in BinaryData — keeps them editable and diffable). The engine scans `_factory/` relative to its executable path on startup.

### 3.4 Operations

| Operation | Implementation | Notes |
|-----------|---------------|-------|
| **Save** | Write JSON to `user/<name>.json`, rebuild `index.json` | Fails if name collides (ask overwrite?) |
| **Delete** | Remove file from `user/`, rebuild `index.json` | Factory patterns blocked |
| **List** | Read `index.json`, filter by category/style/tag | Client-side search on loaded index |
| **Load** | Read JSON file by path from index | Validate schema on load |
| **Import** | Validate JSON → write to `user/` → rebuild index | Accepts file path or raw JSON string |
| **Export** | Read file, return JSON string | Factory or user patterns |
| **Refresh** | Scan `_factory/` + `user/`, rebuild `index.json` | On startup + manual trigger |

---

## 4. RPC & MCP Surface

### 4.1 New RPC Methods (`Router_Composition.cpp`)

| Method | Input | Output | Description |
|--------|-------|--------|-------------|
| `composition.listPatterns` | `{category?, style?, tag?}` | `{patterns: [{id, name, style, category, tags, source}]}` | Browse patterns |
| `composition.savePattern` | `{name, style, params, styleParams, description?, tags?, category?}` | `{id, path}` | Save user preset |
| `composition.loadPattern` | `{id}` | `{name, style, params, styleParams, description, tags}` | Load preset params |
| `composition.deletePattern` | `{id}` | `{success: bool}` | Delete user preset |
| `composition.importPattern` | `{json: string}` | `{id, name}` | Import JSON pattern |
| `composition.exportPattern` | `{id}` | `{json: string}` | Export as JSON |
| `composition.getStyleParams` | `{style: string}` | `{fields: [{name, type, min, max, default, label}]}` | Param schema for UI |

### 4.2 New MCP Tools (`McpTools_Project.cpp`)

| Tool | Description |
|------|-------------|
| `list_patterns` | Browse pattern library with optional filters |
| `save_pattern` | Save generation params as user preset |
| `load_pattern` | Load a preset's params |
| `delete_pattern` | Delete a user preset |
| `import_pattern` | Import a JSON pattern file or string |
| `export_pattern` | Export a pattern as JSON |

### 4.3 Style Names Extension

`composition.getStyleNames` (existing RPC) updated to include all 25 styles (indices 0–24). The `Percussion` style (index 9) is also exposed in this RPC (currently missing).

---

## 5. Frontend UI

### 5.1 Preset Browser in PhraseGeneratorDialog

The dialog gains a **left sidebar** (200px, collapsible) containing the preset browser:

**Layout:**
```
┌──────────────┬───────────────────────────────────┐
│ PRESETS      │  STYLE: TrapHiHat ▼               │
│              │                                    │
│ [Save] [Imp] │  Scale: C  Minor Pentatonic ▼     │
│              │  Range: 36 — 84                   │
│ 🔍 Search... │  Density: 6  Length: 8 beats      │
│              │                                    │
│ ▼ Trap       │  ── TrapHiHat ──                  │
│   Dark Drill │  Roll Density: [4]                │
│   Trap Rolls │  Velocity Decay: [0.7]            │
│ ▼ Jazz       │  Ratchet Chance: [0.3]            │
│   Walking    │                                    │
│   Swing      │  Seed: [0]                        │
│ ▼ User       │                                    │
│   My Bass    │  [Generate]                       │
└──────────────┴───────────────────────────────────┘
```

**Behaviors:**
- **Left panel**: category tree populated from `listPatterns`, grouped by `category`
- **Search bar**: client-side filter by name + tags
- **Click preset**: calls `loadPattern`, populates all dialog fields (style + params + styleParams)
- **Save button**: mini inline form for name + tags → calls `savePattern`
- **Import button**: file picker (`.json`) → calls `importPattern`
- **Style switch**: when user changes style via dropdown, `getStyleParams` is called and style-specific controls swap dynamically
- **Collapsible**: left panel can be collapsed to save space (hamburger toggle)

### 5.2 Dynamic Style-Specific Controls

The existing dialog hardcodes controls per mode. The new system uses `getStyleParams` to render controls dynamically:

1. User selects a style (or loads a preset with a style)
2. Frontend calls `getStyleParams({style})` → receives field schema
3. React renders the appropriate input for each field (slider for float, number input for int, toggle for bool)
4. Field values stored in component state, sent with `generatePhrase` call

### 5.3 Dialog State Persistence

Dialog state still resets on unmount (current behavior). The preset browser is the intended save/reload mechanism — not implicit persistence.

---

## 6. Testing Strategy

### 6.1 C++ Tests (`hdaw_tests.exe`)

- **`PhraseGeneratorTest.NewStylesGenerateNotes`**: each new style produces non-empty `vector<GeneratedNote>` with valid pitches/velocities/timing
- **`PhraseGeneratorTest.NewStylesDeterminism`**: same seed + same params = identical output for each new style
- **`PhraseGeneratorTest.StyleParamsValidation`**: out-of-range styleParams are clamped, missing fields get defaults
- **`PatternLibraryTest.SaveLoadRoundtrip`**: save a pattern → load it → assert params match
- **`PatternLibraryTest.IndexRebuild`**: add/delete patterns → index.json is correct
- **`PatternLibraryTest.FactoryProtected`**: attempting to delete a factory pattern fails
- **`PatternLibraryTest.ImportValidation`**: invalid JSON, unknown style, missing fields → appropriate errors
- **`PatternLibraryTest.VersionForwardCompat`**: loading a version-1 file with version-2 loader applies defaults for new fields

### 6.2 Frontend Tests (Vitest)

- **`PhraseGeneratorDialog.test.tsx`**: preset browser renders, save/load/delete/import work
- **`PatternLibraryStore.test.ts`**: Zustand store for pattern library state

### 6.3 E2E Tests (Playwright)

- **`phrase-generator.spec.ts`**: extend with preset save/load journey
- **`pattern-import.spec.ts`**: import a JSON file, verify it appears in browser, load and generate

---

## 7. Implementation Phases

### Phase 1: JSON Format + Library Infrastructure
- Define JSON schema + validation in C++
- Implement `PatternLibrary` class (save/load/delete/list/import/export)
- Implement `index.json` management
- Add RPC methods + MCP tools
- Unit tests for library operations

### Phase 2: New Styles (batch 1 — 5 styles)
- `TrapHiHat`, `DrillBass` (Trap/Drill)
- `WalkingBass`, `SwingComping`, `Counterpoint` (Classical/Jazz)
- Add styleParams structs + validation
- Update `getStyleNames` RPC
- Style-specific generation tests

### Phase 3: New Styles (batch 2 — 5 styles)
- `MarkovMelody`, `EvolvingTexture`, `Aleatoric` (Generative/Ambient)
- `ScalarRun`, `ChordToneSeq` (Melodic Sequence)
- Style-specific generation tests

### Phase 4: New Styles (batch 3 — 4 styles)
- `CallResponse` (Melodic Sequence)
- `PhaseShift`, `AdditiveRhythm`, `MinimalistLoop` (Polyrhythm/Minimal)
- Style-specific generation tests

### Phase 5: Frontend Preset Browser
- Preset browser component (left sidebar)
- Dynamic style-specific controls via `getStyleParams`
- Save/load/delete/import UI flows
- Extend Vitest + Playwright tests

### Phase 6: Factory Patterns
- Create 15–20 curated factory patterns across all categories
- Bundle in `patterns/_factory/` directory
- Test factory pattern loading + generation determinism

---

## 8. Key File Paths

| File | Change |
|------|--------|
| `src/engine/PhraseGenerator.h` | Add 15 Style enum values, StyleParams structs |
| `src/engine/PhraseGenerator.cpp` | Implement 15 generation functions |
| `src/engine/PatternLibrary.h` | **New** — PatternLibrary class |
| `src/engine/PatternLibrary.cpp` | **New** — save/load/delete/list/import/export |
| `src/frontend/router/Router_Composition.cpp` | Add 7 new RPC methods |
| `src/mcp/McpTools_Project.cpp` | Add 6 new MCP tools |
| `frontend/src/components/PhraseGeneratorDialog.tsx` | Preset browser sidebar, dynamic controls |
| `frontend/src/components/PresetBrowser.tsx` | **New** — preset browser component |
| `tests/unit/engine/phrase_generator_test.cpp` | Extend with new style tests |
| `tests/unit/engine/pattern_library_test.cpp` | **New** — library operation tests |
| `patterns/_factory/` | **New** — factory pattern JSON files |
