# Plan — Phase 2: live-verify the VA recipes and feed them back into the palette

**Context.** Three research deliverables feed this phase:
[`psy-va-technique-notes.md`](../psy-va-technique-notes.md) (37 falsifiable hypotheses + traps),
`psy-virus-recipes.md`, `psy-waldorf-recipes.md` (per-role parameter recipes from the patch
corpora). Their recipes are *derived* — distributions and device vocabularies. Nothing has yet been
**heard**. This phase applies the top recipe per role to the real emulation, measures it, and only
then changes the track.

**Why it is needed:** a corpus distribution can tell you what patches *look like*; it cannot tell you
whether the recipe survives contact with this engine (isolation, the host-name translation, the
post-limiter clamp, the ROM-program-only constraint). The failure mode we are guarding against is
writing a confident recipe doc that does not actually sound right.

## Success gates

- [ ] **G1** — for each verified role, the recipe is applied to a scratch slot of the REAL emulation
      and the slot's state is captured (`get_fx_capture_status` → `status=ok`), so it survives into
      renders. An un-captured patch is not a result.
- [ ] **G2** — each candidate is measured with a matched-window A/B against the incumbent palette
      sound, same window, same target: `verify_part` (solo peak/rms) **plus** a windowed full-mix
      `mix_report`. A candidate with no measured delta is not a result.
- [ ] **G3** — each applied recipe reproduces at least one of the technique-note **falsifiable
      predictions** for its role, or the mismatch is recorded as a refuted hypothesis. Either outcome
      is a valid gate pass; silence is not.
- [ ] **G4** — any recipe that needs a parameter the host does not expose is reported as
      **unactionable through host writes** (with the ROM-program workaround named), never quietly
      approximated.
- [ ] **G5** — the track still passes after the palette swap: `mix_verdict ok:true`, rms inside
      `[0.171, 0.189]`, `ceilingHitPct` ≤ 5, drop>build arc intact. A swap that breaks the verdict is
      reverted, and the revert is reported.
- [ ] **G6** — the recipe docs are updated with what the engine actually did (the measured values),
      so the docs end up describing verified rather than derived behaviour.

## Protocol (per role, one at a time — single writer)

1. **Pre-flight:** `whoami` (assert the fresh binary + the expected project), and
   `get_audio_current_setup` — **if `output` is empty the live graph has no tracks and every
   live-slot tool will lie with `track not found: N`**; call `set_audio_output_device` first.
2. **Scratch slot:** `add_track_with_fx {name:"_scratch_<role>", pluginId:"<Engine>.clap"}`. Wait for
   the child to boot (Virus ~25 s = two 12 s warmups; the others are seconds). Confirm the live
   surface with `list_fx_params` — a boot count (e.g. Virus 6939) means it is really up; **0 means
   still booting, not broken**.
3. **Apply the recipe as named `set_fx_param` writes**, every name `Ch <n> `-prefixed for the Virus
   (see [`psy-virus-host-param-map.md`](../psy-virus-host-param-map.md)). Log each write's
   `ok`/`unknown` — an unknown name is a finding, not a retry.
4. **Capture:** poll `get_fx_capture_status` to `status=ok`. For Xenia/NodalRed2x, `unchanged` is the
   documented correct outcome (their patch RAM is not in the child state; dumps replay instead) —
   do not treat it as failure.
5. **Measure:** render a short window that contains the role's own notes (`export_audio` with
   `startBeat`/`endBeat`, then `mix_report`), and take `verify_part {trackIndex, startBeat, endBeat}`
   for the per-role solo peak/rms. Record the peak/rms **pair** (same transient ⇒ peak holds while
   rms moves).
6. **A/B:** the same window with the incumbent sound. Prefer a *ratio* over an absolute (the emulated
   engines carry free-running state: ~±2% RMS between renders — use spectral/band properties, or the
   probe's own measured spread, as the trust threshold; sample-level equality is not available).
7. **Decide and record:** keep only if it improves the role's target metric without failing G5;
   otherwise keep the incumbent and report the measurement that rejected it.
8. **Clean up:** `remove_track` the scratch track. Leave no `_scratch_*` behind.

## Known traps this phase must respect (all measured this session)

| Trap | Consequence if ignored |
|---|---|
| No audio device open | Live graph has no tracks; live-slot tools fail with a misleading `track not found: N` |
| `trackID` is 1-based and undiscoverable | A positional index passed as `trackID` silently writes to the **previous** track |
| Plugin slots take **ROM programs only** | `send_fx_midi`'s contract: "plugin slots ignore injected SysEx"; `apply_preset` sends an Access header to the *internal* `sub_synth`. So a real Virus = `load_virus_preset` (CC0+PC) + named writes |
| Master gain is **post**-FX | Master gain is an output trim, not limiter drive (it can clip past the ceiling without the post-gain clamp) |
| `verify_part` solo peak is clamped by the master chain | A loud solo's peak is pinned at `ceiling × master gain`, which **hides transient deltas** — read the pair, and prefer a window `mix_report` for band evidence |
| Isolation non-determinism (~±2% RMS) | A single A/B render cannot resolve a small change; repeat or use spectral properties |
| `export_audio {trackIds:[…]}` **does not isolate** the requested track | A "kick" stem returned 0% energy below 300 Hz (reported by the remix pass, independent scipy check). Do not build per-role evidence on stems |

## Feed-back step

Only after G1–G6: change the `ion_rift_remix` palette from the internal instruments currently carrying
`acid_stab` / `lead` / `arp` (`psy_fm`) and `pad` (`sub_synth`) to the verified Waldorf/Virus recipes,
keeping the bass (already a real OsTIrus). Then re-run the mixer gates. The `ion_rift` portability is
1:1 for percussion and FX layout (checked on disk), so any accepted swap ports to the first track too.

---

## Method findings from the first verifications (measured 2026-09-30 — obey these)

The first two roles were verified live. What the runs taught, so the next ones are cheap:

1. **`verify_part` returns TEXT, not JSON** — `ok=1 soloRms=… soloPeak=… … audible=1`. Parse it with a
   regex; piping it into `ConvertFrom-Json` fails and yields empty fields that *look* like zero.
2. **`verify_part` rejects `startBeat=0`**: `"need 0 < startBeat (< endBeat)"`. You cannot verify a
   window that starts exactly at beat 0 — either start at `0.5` or use `windowSeconds` alone.
3. **`remove_track` refuses a track that still has clips** unless `force:true` (a good guard — but it
   means a failed cleanup silently leaves a stray track behind).
4. **Track indices shift when a create/remove pair half-fails.** A leftover `_scratch_*` track moved
   a later probe's target by one, and the probe then measured the WRONG track: `verify_part` returned
   `?` for every point (it errored with `track has no clips`), which is indistinguishable from "the
   parameter did nothing" if you are not reading the payloads. **Always re-read `list_tracks` and
   resolve the index from the name immediately before measuring; never reuse an index across a
   create/remove step.** (This cost a whole sweep.)
5. **One silent render is not a result.** A single A/B point returned `soloRms=0, audible=0` for
   `AmpEnvAttack` 52→0 on the microQ pad, and it could not be reproduced before the probe was retired —
   it is recorded as **UNRESOLVED**, not as a finding. **Repeat any single-parameter A/B** (the
   isolation variance floor is ~±2% RMS, and a spawn failure is worse), and treat an isolated silent
   measurement as unresolved until it repeats.
6. **Values are normalized `byte/127` and round-trip EXACTLY** — confirmed on both engines (Xenia: 8/8
   params; microQ/Vavra: 13/13, including `Wave`, `W1StartW`, `MixW1`, `O1Shape`, `Lfo1Speed`). So a
   recipe table in raw 0..127 bytes is directly usable: `value = byte / 127`. Non-normalized param
   metadata is **absent** (`min`/`max` empty) and writing a raw byte silently clamps to 1.0.
7. **`audible=1` is the gate that matters first.** Both verified recipes returned `audible=1` with a
   non-zero `soloRms`, i.e. they make a sound before any A/B is attempted — that ordering (audibility
   → parameter control → mix fit) is the cheapest way to fail fast.
