# Handoff — 2026-09-30: VA recipe research, live verification, palette feed-back — and what is left

**Status:** the research goal is COMPLETE (research + live verification + palette feed-back). This
handoff exists for the **remaining issues**, all of which are open and none of which block the
deliverables. Nothing in this session is committed (see §4).

---

## 1. What was produced (the deliverables)

| File | Size | What |
|---|---|---|
| [`docs/psy-va-technique-notes.md`](../psy-va-technique-notes.md) | 641 lines | Genre + per-role technique from 24 external sources + local docs; **37 numbered hypotheses**, each with an explicit *"refuted if"*; 14 traps; source table |
| [`docs/psy-virus-host-param-map.md`](../psy-virus-host-param-map.md) | 79 lines | **Name authority** — manual/vocabulary names → real `Ch <n> ` host names |
| [`docs/psy-virus-recipes.md`](../psy-virus-recipes.md) | 782 lines | Bass / acid / lead / pluck recipes; **all 25 applicable hypotheses verdicted (7 CONFIRMED / 7 REFUTED / 11 INCONCLUSIVE)**; example patches; MCP contracts; predictions |
| [`docs/psy-waldorf-recipes.md`](../psy-waldorf-recipes.md) | 1,617 lines | 5 role recipes; per-role microQ-vs-XT verdict; 5,870 XT patches decoded; wavetable selection; movement; apply-via-MCP; predictions |
| [`docs/plans/2026-09-30-psy-va-recipe-verification.md`](../plans/2026-09-30-psy-va-recipe-verification.md) | — | The Phase-2 method + the 7 measured method findings (read this before doing more live verification) |

Corpora used: Virus 5,296 rows → **2,818 unique after dedup**; microQ 528 sidecars; XT 5,870 decoded
(more than the shipped survey's 1,791). **Neither corpus is a psytrance library** — no genre label
exists in either — so the docs give structure and priors, not targets; every value outside the corpus
p25–p75 is labelled extrapolation.

## 2. What live verification actually established (the corrections matter most)

- **The vocabularies' `isPublic` flag does NOT reflect the live host surface** (both engines). Xenia
  exposes **2151** params, Vavra **7557**; everything the Waldorf doc called "not host-writable →
  needs a dump" turned out to be writable, including `Wave`, `W1StartW`, `MixW1/2`, `WaveEnvTime1`,
  `WaveEnvLevel1`, `O1Shape`, `GlideRate`, `UnisonoDetune`, `Lfo1Speed` and the whole microQ arp.
  Corrections are in the doc's top banner. **Read the live list, never the file flag.**
- **Values are normalized `byte/127`** and round-trip *exactly* on both engines; writing a raw byte
  silently clamps to 1.0.
- **The microQ arp is functional, not just exposed:** `ArpMode` 0→3 = **+68%** solo RMS;
  `ArpClock` 9→4 = **+37%**.
- **A measured constraint:** the XT (`Xenia.clap`) does **not sustain a held note** — same patch,
  same track: held triad `soloRms 1.2e-06 / audible=0`, 16ths `soloRms 0.102418 / audible=1`. Not
  the amp envelope (`AmpEnvSustain` 0.87 did not help) and no single wave parameter causes it.
  **Mechanism NOT pinned — open.** Documented in `psy-waldorf-recipes.md` §7.
- **Feed-back done for two roles** (gates green, variants saved): `acid_stab` → XT acid
  (metric-neutral), `pad` → microQ (kickProminence **0.5755 → 0.7280**, bass −27.6%, body −28.1%,
  no clipping).
- Also this session: **master re-staging** (crest **8.04 → 12.74 dB** at the same RMS, `ceilingHitPct`
  0 — see `ion_rift_remix.wav`) and the **Phase 5 microQ matrix-preset fix** (build rc 0, 6 new tests).

## 3. OPEN ISSUES — highest value first

### O1. `set_audio_output_device` silently accepts an unknown device name (CONFIRMED, worst of these)
Full write-up + gates **P6-a…P6-e**: [`docs/plans/2026-09-30-bug-fixes-from-ion-rift-session.md`](../plans/2026-09-30-bug-fixes-from-ion-rift-session.md) §Phase 6.
Reproduced 3× on engine 29592: a name not in that engine's list returns **`ok`** and **drops the
device** (`output` becomes `""`). Why it is the worst: deviceless is *exactly* the state that makes
every live-slot tool fail with the misleading **`track not found: N`** — the trap that cost this
session real time. It presents as "the engine/plugin is broken". Fix = refuse unknown names, keep the
current device on failure, and document the close spelling.

### O2. `export_audio {trackIds:[…]}` does not isolate the requested track (REPORTED, **not verified by me**)
Reported by the `ion_rift_remix` sample pass: a "kick" stem came back with **0% energy below 300 Hz**,
confirmed there with an independent scipy read. I did **not** reproduce it. Per-role evidence in this
session therefore used `verify_part {soloOnly:true}` instead. **Verify first, then either fix it or
make the argument refuse** — a stem that silently returns the wrong content is worse than an error.

### O3. The HTTP-MCP helper's default truncation CORRUPTS JSON (CONFIRMED by me)
`scripts/hdaw_mcp_http.py call` truncates at ~4000 chars and **appends a notice**, which makes long
payloads unparseable — I lost an experiment to it (`mix_report` without `--full` →
`ConvertFrom-Json` failed with `Unexpected character …`). This is item **P4-d**, but note it is a
**correctness** problem for machine parsing, not just ergonomics: the truncation must not be
interleaved with the payload (or `--full` must be the default for `call`).

### O4. Phase 4 tool-surface batch (planned, NOT started)
`P4-a` `apply_song_brief` `brief` type object|string (`McpTools_SongPlan.cpp:192`); `P4-b` `add_fx`
`fxType` enum omits `plugin` while the description says "or a pluginId" (and the pluginId path
demonstrably works — check the RPC twin `project.addFx`); `P4-c` **enum refusals must name the
offending value and the allowed set** (`McpSchema.cpp:29` returns a bare `"value not in enum"`) —
highest-value of the four, it improves every enum-bearing tool on both surfaces; `P4-e` build + tests
+ `node tools/rpc_parity_map.mjs`. Locations verified; details in the plan §Phase 4.

### O5. Phase 5 residual — the guard boundary (deliberate, needs a decision)
The matrix dump route validates only `F0`/`F7` framing and size, **not** the Waldorf `3E`/machine
header, so a mislabeled 392-byte non-Waldorf dump inside a `vavra` sheet would also be retargeted.
Only `engine:"vavra"` can reach it (every other engine id maps to mw2 ⇒ no-op), so the blast radius is
that one sheet. Adding `d[1]==0x3E && d[2]==machine` to `retargetWaldorfDumpForSingleEditBuffer`
closes it with **zero** change for valid dumps. **Also:** the fix agent's `grep 'd[5] = 0x20'` gate is
**parameter-name-sensitive** (it named the pointer `d`) — a future zero-hit result must NOT be read as
"no implementation".

### O6. UNRESOLVED measurement anomaly (do not treat as a finding)
On the microQ pad, a single `verify_part` after `AmpEnvAttack` 52→0 returned **`soloRms=0, audible=0`**
and **did not reproduce** before the probe was retired. Either a real microQ behaviour or a
`verify_part` spawn flake. **Repeat any single-parameter A/B** before believing it (recorded in the
Phase 2 plan §"Method findings", item 5).

### O7. `verify_part` rejects `startBeat=0` (wart, undocumented?)
It errors `"need 0 < startBeat (< endBeat)"`, so a track's very first beat **cannot** be verified that
way — use `windowSeconds` alone, or start at `0.5`. Either document it or allow `0`.

### O8. `bandsPresent` is inconsistent (not investigated)
Measured `0` for the XT acid recipe (audible=1, soloRms 0.029), `1` for the microQ pad, `0` for the
silent texture case. With `audible=1` but `bandsPresent=0`, the field looks unreliable as a gate —
worth checking whether it is window-dependent or simply misreported. **Do not build a gate on it**
until this is settled.

### O9. Track-index shift on a half-failed create/remove (method trap, cost a whole sweep)
`remove_track` correctly refuses a track that still has clips without `force:true` — but that means a
failed cleanup leaves a **stray track**, which shifted a later probe's index so it measured the **wrong
track** and returned `?` for every point (indistinguishable from "the parameter did nothing").
**Always re-resolve the index from `list_tracks` immediately before measuring.** Also in the Phase 2
plan's method findings.

### O10. Intermittent mcp combined-filter crash class (pre-existing, not this session's change)
The fix agent saw `0xC0000005` / exit `0x80004005` under the broad at-risk filter — **reproduced with
its new tests excluded**, at a suite it never touched, while the focused A/B was 6/6 clean. Matches
the documented `docs/testing-mcp.md` intermittent-shard-death + lesson 43 class. No WER dump captured.

## 4. Repo / process state (verify before acting)

- **Nothing is committed this session.** `HEAD = d2e3708`; 13 uncommitted paths. Uncommitted work
  includes the **Phase 5 fix** (`src/common/WaldorfEditBuffer.h` new + `PresetApply.h`,
  `MatrixPresetService.cpp`, two test files) and the **four new research docs** + plan edits. This is
  the first thing to decide on.
- **Engines:** PID 29592 alive (8766 / 18765), currently holding
  `compositions/ion_rift_remix/ion_rift_remix_va_palette.hdaw` **in memory** (disk unchanged), with its
  output device set to `"Remote Audio"` — the only device that engine lists. PID 9604 + its plugin
  host were stopped by the fix agent for the build; **18766 is down**.
- **Scratch left deliberately:** `.tmp_virus/` (~1,400 files — the evidence trail whose
  `analysis.txt` line ranges the Virus doc cites; do not delete before trusting that doc) and
  `.scratch_sample_audition/` (from the earlier remix pass).
- **Remix artifacts:** `ion_rift_remix.wav` (delivered) · `ion_rift_remix_xt_palette.wav`
  (acid swapped) · `ion_rift_remix_va_palette.wav` (acid + pad swapped), each with a matching `.hdaw`.

## 5. Decisions for the user (choices, not defects)

1. **Which palette to promote.** The acid swap is metric-neutral (everything within ~1%); the pad swap
   measurably improved separation. Metrics cannot tell you which *sounds* better — that is an ear call,
   which is why the delivered track was left untouched. Listen to `ion_rift_remix.wav` vs
   `ion_rift_remix_va_palette.wav`.
2. **Port the accepted swaps to `ion_rift`** — the doc records that palette as 1:1 portable
   (verified from the `.hdaw` on disk, not by loading it).
3. **The `lead` recipe has no verdict to verify** — the doc's answer is "draw" (a choice). Nothing is
   broken; it simply was not render-verified like acid/pad/arp.
4. **Commit the Phase 5 fix + docs** (§4), then update `docs/handoffs/INDEX.md` if this file's claims
   supersede the earlier `2026-09-30-ion-rift-bug-closeout.md`.
