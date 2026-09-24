# Plan: B3 — durable references migrate to stable ids

Status: **IMPLEMENTED 2026-09-24** (decisions below; slices S1/S2/S3 landed, gates G1-G6 verified in
`tests/unit/engine/durable_ref_migration_test.cpp` + the rewritten pin tests).

Follows B1 (`40ebe3b`, stable `trackID`/`sendID` shipped + echoed) and B2 (ids usable as arguments).

## Goal
Stop storing POSITIONAL indices in the four durable reference vocabularies, so a splice (removeTrack /
moveTrack / removeSend) cannot silently re-point them — and delete the remap machinery that exists
only to patch them up.

## The references (measured, all in the ValueTree and all persisted)
| # | Vocabulary | Stored as | Today's patch-up |
| --- | --- | --- | --- |
| 1 | Folder membership — child's `parentId` | int TRACK_LIST index | `HDAW::remapTrackPositionalRefs` (`AudioEngineCommands_Helpers.h:98-167`) |
| 2 | Folder membership — folder's `childIds` | CSV of TRACK_LIST indices | same walk |
| 3 | `SONG_PLAN/CELLS/CELL` `cellTrack` | int track index | same walk |
| 4 | Automation lane `paramID` for a send level | `2000 + sendIndex` | inline walk in `removeSend` (`AudioEngineCommands.cpp:596-628`) |
| 5 | LFO `targetParamID` for a send level | `2000 + sendIndex` | the same walk (added 2026-09-23) |

The walk is correct and tested; the objection is structural — every new splice site must remember to
call it, and every consumer must remember that the number is an index (two bug classes were fixed
today by remembering: `move_track`'s forward splice, and `removeSend`'s missing `MODULATION_LIST`).
Indices are also what make `remove_track`/`move_track` return a shift payload at all.

## Proposed migration
**Storage:** ids only, in new properties, one vocabulary at a time:
- `parentId` → int `parentTrackID` (-1 = folder-less, as today's sentinel).
- `childIds` → CSV of **track ids** under a NEW name `childTrackIDs` (the CSV grammar stays: the
  reader keeps dropping non-int tokens, exactly as now — `remapTrackPositionalRefs` and
  `ReadModelImpl` are the two readers).
- `cellTrack` → int `cellTrackID` (a cell with no track keeps -1).
- Lane/LFO send targets → `2000 + sendID` (the modulus 1000 for "one send address per id" would need
  `2000..2999` to hold 999 ids — a send id above 999 would overflow into the bus range! See "Open
  questions" 3).

**Compatibility (the part that must be right):**
- On load, `ProjectSerializer::load` runs a one-way migration AFTER `scanAndSyncTrackIDs()` (so every
  track/send has an id): convert each positional ref to its id by INDEX → id, then move/set the new
  property. A file with the new properties is left alone; a file with both trusts the ids.
- The migration is ONE indexed walk per list (lesson 30), never undoable (identity/provenance, like
  the id backfill — `ProjectSerializer` clears the history immediately before), and idempotent.
- `save` writes the new properties (the whole tree is serialized verbatim, so nothing to do beyond
  not writing the old ones back: drop the legacy property when the new one is written, in the same
  unit as the migration).

**Retirements (the payoff, and the risky part):**
- `remapTrackPositionalRefs` + `trackRemovalIndexMap` + `trackMoveIndexMap` deleted;
  `removeTrack`/`moveTrack` stop remapping (they still return the shift report — see open question 1).
  **Correction found during implementation (2026-09-24):** `removeTrack` must still run ONE indexed
  walk that PRUNES the removed id from every durable ref (parent folders' `childTrackIDs`, the removed
  node's children's `parentTrackID`, cells' `cellTrackID` -> -1). Reason: `allocateTrackID()` is
  `max(existing)+1`, so a dangling ref can silently RE-POINT at a subsequently minted track (remove the
  highest-id track, add one, and the new one shares the dead id). "Leave it dangling and resolve to
  nothing" is only safe while ids are never reused — they are. This restores the pre-B3 removal
  semantics (removed -> sentinel) with one walk instead of the remap machinery.
- The lane/LFO send-target remap in `removeSend` deleted (nothing positional left to fix; a dead
  send's lane simply keeps its id and silently targets nothing — the same "silent no-op" contract the
  pid decode already documents — or, if we prefer explicitness, `removeSend` re-points/deletes by id).
- `ReadModelImpl`'s effective-mute/solo cascade reads `parentTrackID`; `fillOneCell`'s sentinel guard
  reads `cellTrackID`.

## Gates (all must pass with evidence)
1. **G1** Legacy project (written before B3, ids backfilled at load) → after load, every folder link,
   `cellTrack` and send-target lane addresses the SAME entity it did before the migration. Test with a
   saved file, not a hand-built tree.
2. **G2** New project: `removeTrack` / `moveTrack` / `removeSend` leave every reference pointing at the
   same entity (the assertions must compare WHAT A REF POINTS AT, not the raw number — that is the
   lesson from `MoveTrackRemapsDurableRefsOnMcpPath`).
3. **G3** Save → load → save is byte-stable for the reference properties (no oscillation, no rewrite
   churn on a plain load).
4. **G4** The existing design-A tests that pin the shift payloads (`Commands.RemoveTrack*`,
   `MoveTrack*`, `AutomationSendBusPids.RemoveSend*`, `BusSendRpcTest.RemoveSend*`,
   `McpCoverageTest.Remove/MoveTrack*`) are rewritten to the new contract — or the payloads are kept
   (open question 1).
5. **G5** A pre-B3 lane/LFO send target and a post-B3 one coexist: an old file's `2000+sendIndex` is
   migrated, a new file's `2000+sendID` is not re-migrated (the migration must be able to tell them
   apart — see open question 3).
6. **G6** The full suite passes; `node tools/rpc_parity_map.mjs` unchanged (no tool/route changes).

## Risks
- **Format migration is the highest-risk change class in the repo** (every saved project flows through
  it). Mitigation: migrate at ONE place (the load path), never in place on demand; add a
  `formatVersion`-style guard only if the id-vs-index ambiguity cannot be resolved (open question 3).
- **Retiring the shift payloads is a contract change** for the MCP/RPC surfaces that agents and the
  reference frontend already consume.
- Folder semantics are read in more places than the two writers (`ReadModelImpl` cascade, the UI, the
  song-plan fill); each reader must be found by grep, not by memory.

## Decisions taken (2026-09-24 — implementation started)

1. **Shift payloads: KEPT.** `{ok, removed, shifted[]}` stays byte-identical on both surfaces. With ids it
   becomes advisory ("these indices moved; your ids did not") — no wire change, no client update.
2. **Vocabulary: migrate + drop.** New storage `parentTrackID` / `childTrackIDs` / `cellTrackID` is the
   ONLY thing live code writes; the load-time migration converts and REMOVES the legacy `parentId` /
   `childIds` / `cellTrack`, so a saved file has exactly one vocabulary. Save stays verbatim (the
   serializer has no property whitelist).
3. **Scope: refs 1-3 only** (option (a) of question 3). Lane/LFO send targets keep `2000 + sendIndex`
   and `removeSend`'s remap walk stays — a send id above 999 would overflow into the bus range `3000+`,
   which is a pid-space design question of its own. Refs 4-5 are explicitly NOT migrated here.
4. **Public/wire contracts that do NOT change:** `CellRecipe.trackId` stays a TRACK_LIST index on the
   API/wire (converted at the storage boundary in `setCellRecipeImpl`/`getCells`), and
   `TrackSnapshot.parentId` stays a positional index (resolved from `parentTrackID` at read time). Only
   tree storage changes.

**Load-path fact that fixes the migration site:** `ProjectSerializer::load` runs `migrateProjectTree`
(:170) BEFORE `scanAndSyncTrackIDs()` (:179), so the ids a positional→id migration needs do not exist at
:170. The migration therefore runs immediately AFTER `scanAndSyncTrackIDs()`, with a null undo manager
(never undoable), and is idempotent (a node carrying the new property is left alone; its legacy property
is dropped).

Slices: **S1** vocabulary header + writers + deletion of `remapTrackPositionalRefs` /
`trackRemovalIndexMap` / `trackMoveIndexMap`; **S2** load-time migration + the `ReadModelImpl` id→index
conversion; **S3** test rewrites (raw-property pins → resolve-and-compare) + migration/byte-stability
tests; then the full suite (parity ledger unchanged — no tool/route change).

## Open questions (need the user's call before implementation)
1. **Keep the shift payloads?** `{ok, removed, shifted[]}` is shipped and twinned on both surfaces;
   with ids it becomes advisory ("these indices moved; your ids did not"). Keep it (compatible, and
   still useful for clients that hold indices) or drop it (cleaner, breaks those clients)?
2. **How much of the old vocabulary to keep legible?** A reader that supports both vocabularies forever
   is two code paths; a migration that drops the legacy properties is one. Recommend: migrate + drop.
3. **Send-target ids above 999.** Lane/LFO send addresses live in `2000..2999` (999 slots). Send ids are
   `max+1` and unbounded, so a project with >999 sends would collide with the bus range `3000+`. Either
   (a) keep `2000 + sendIndex` for lanes and let the remap stay (i.e. B3 covers refs 1-3 only), or
   (b) widen the send range (a format change to the pid space, with a migration), or (c) make the send
   id ALLOCATOR bound to `<= 999` per project (an artificial cap). Recommend (a) for B3 and treat the
   lane/LFO send address as a separate design item — it is the one place where an index is genuinely
   baked into an arithmetic address space shared with other ranges.
4. **Scope/priority.** B3 touches the project format; if the immediate need is agent ergonomics, B2
   (shipped) already lets a held id act. B3's payoff is robustness + deleting the remap machinery.
