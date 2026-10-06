# 2026-10-05/06 — param batch tools + internal-synth expansion (A–E)

Session handoff. Two related workstreams on the Linux box (engine v0.39.4),
plus a documentation-hygiene pass. Plan close-outs:
`docs/plans/2026-10-05-param-batch-and-bugfixes.md`,
`docs/plans/2026-10-05-internal-synth-expansion.md`.

## 1. Batched parameter writes (+ 4 bug fixes)

Motivated by a measured cost: rebuilding `mangrove_dub` took **278 individual MCP
calls**, 226 of them single-value parameter writes.

New MCP tools + RPC twins (partial-apply, ONE undo unit on every transport):
- `set_fx_params` / `project.setFxParams` — mixed `paramIndex`/`paramName`/`intent`,
  `mode: real|normalized`.
- `set_bus_fx_params`, `set_lfo_params`, `add_buses`, `add_sends` + twins.

Foundation: `src/common/FxParamBatchJson.h` (strict parser),
`src/common/FxParamResolve.h` (ONE resolver+writer, shared by the batch AND the
four single-write tools — byte parity proven, `texts_not_preserved: NONE`).
`ProjectCommands::{writeFxParam,setFxParams,setBusFxParams,setLfoParams,
createBuses,createSends}`; `paramBatchActive_` at the `transactionBoundary`
choke point.

Bugs fixed: `verify_part{soloOnly:true}` booleans (was 0/false for a clean part),
`set_notes_gain` gain clamp 0..2 with a visible report, `verify_window`
ceilingHitPctMax prose, `masterRms` unit wording.

**Measured result**: the same song rebuilt in **57 calls vs 278 (−79%)** with a
byte-identical release verdict (7/7 gates). Parity ledger **325 tools / 427
methods / mapped 311 / mcp-only 14 / 0 unresolved**.

## 2. Internal-synth expansion (A–E)

Palette reality on Linux: no VA CLAPs, so the internal synths ARE the palette.

| Slice | Change | Nature | Evidence |
|---|---|---|---|
| A | psyarp HP/BP filter + Pulse/Noise osc (params 21–23, `Osc Shape` 0..4) | additive | FNV goldens bit-identical; `param_verity` audible |
| B | psy_fm post-FM per-voice filter (33–37) | additive | HEAD-revert goldens identical |
| C | PolyBLEP AA on psyarp + sub_synth (saw/square/pulse/supersaw) | **non-additive** | alias −17→−89 dB (psyarp), −25→−140 dB (sub_synth); low-note harmonics <1% |
| D | psy_fm 6 algorithms (param 32 0..5) + 9 presets | additive | existing-4 + default hashes unchanged |
| E | presets carry filter params 33–37 | additive | existing-4 neutral ⇒ hash-identical; live round-trip |

Counts: psyarp 21→24, psy_fm 33→38 params. Device maps regenerated + `--check`
deterministic. C rewrites existing renders by design (approved); everything else
is provably transparent.

## 3. Documentation hygiene

- Corrected tool/parity counts (319/317 → 325) in `docs/composition-toolkit.md`,
  `AGENTS.md`, both skill trees' `reference.md`.
- Fixed the false "modulation targets 300–308" claim in
  `docs/core-synths-agentic-guide.md` + `docs/hardware-va-suite.md` (unreachable:
  `pid≥100` decodes 306 as slot2/param6).
- Documented the param-batch tools in the toolkit + testing docs; added them to
  the psy-song-session role files (they previously prescribed N-call loops).
- Archived `docs/plans/psyarp-engine-implementation.md` and
  `docs/plans/2026-09-22-agentic-batch-commands.md` → `docs/archive/plans/`.

## Open / not done

- `increment_fx_params` (delta writes) — deferred, no use case surfaced.
- Slice D note: a psy_fm preset now sets the whole sound including the filter;
  presets do NOT set the modulation matrix beyond their row's `matrix` string.
- `docs/plans/` still holds genre design notes (Astral Projection, FM synthesis,
  Growl Bass, Subbass Generator) — reference material, left in place.
