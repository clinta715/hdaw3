# Handoff — 2026-09-30: closing out the `ion_rift` session's bug list

**Date:** 2026-09-30 · **Supersedes:** [`2026-09-30-ion-rift-and-va-verification.md`](2026-09-30-ion-rift-and-va-verification.md)
**Plan:** [`docs/plans/2026-09-30-bug-fixes-from-ion-rift-session.md`](../plans/2026-09-30-bug-fixes-from-ion-rift-session.md) — 9 of 10 gates closed; P2-a's code landed, its handoff clause is the subject of §4 below.
**Engine source touched:** yes (this is the session that fixed the bugs the composition session found).

## 1. What was closed

Every finding from the composition session's handoff §2/§4/§5 that was actionable, in the
plan's own gate order. All are in the tree; nothing is left open on the engine side.

| Gate | What it was | What landed |
|---|---|---|
| P1-a | A deviceless engine blamed the TRACK (`track not found: N`) for a DEVICE problem | `src/common/LiveTrackLookupError.h` — a pure formatter; both live-slot failure sites name `set_audio_output_device` and the configured output when there is one. Byte-identical legacy text when a device IS open. |
| P1-b | The stable `trackID` was undiscoverable (`list_tracks` reported only the positional index) | `src/common/TrackListJson.h`; every `list_tracks` row carries `trackID` read off the TRACK node, `id` unchanged; description states which is which. |
| P1-c | `trackID: 0` answered the misleading `trackId required` | Presence is now explicit (`stablePresent` from `contains()`) through `StableRefResolve.h` and both surfaces; a present non-positive id is `unknown trackID 0`. |
| P1-d | `apply_song_brief` silently ignored `sections[].kind` (lesson 34) | An explicit `kind` is honoured, `type` is the fallback, and a type/kind disagreement refuses the whole brief. |
| P1-e | Build + new tests | `dsh-build-fast.bat test` rc 0; the full sharded suite green (§5). |
| P1-f | Parity discipline | Ledger byte-unchanged; the payload additions (`snapshot_project` rows, layer-handoff rows) are asserted on both surfaces. |
| P2-a | Master gain was POST-master-FX: raising it clipped instead of adding loudness | Option (b) as decided: the enabled limiter's ceiling is re-applied AFTER the gain; limiter-free projects stay bit-identical. `set_master_gain`'s description states it. |
| P2-b | The hang watchdog wrote 1.5-2 GB dumps, and dumped during HEALTHY warmups | TWO defects: the **warmup-epoch carryover** (fixed with the per-warmup epoch counter the plan specified) and the **dump size** (now stack-only, measured 70,825 bytes vs the 330-670 MB class). |
| P3-a | A clean exit 0 mid-render was unexplained | Explained: stdin EOF → `quit()` → `app.exec()` = 0, the engine's only clean-exit path; the transport now logs it. |
| P3-b | A booting plugin slot looked exactly like a broken one | `src/common/PluginBootGate.h` — one shared bounded wait in both param read paths, so a warming-up child answers with its real parameters instead of `{}`. |

**Two extra defects found while closing out** (not in the plan, both fixed):
- `FrontendServer::start(0)` rewrote 0 → 8766, contradicting its own header's `port()` contract
  ("handy if start(0) was used to pick a free port"). It made every test fixture bind the LIVE
  engine's port: **all 24 `FrontendServer.*` tests were failing** with "server failed to bind"
  whenever an engine held 8766. Now 0 = OS-assigned.
- `audition_plugin`'s `style` is case-sensitive but neither its schema nor its refusal said so: the
  session's trap was `style:"lead"` (for `"Lead"`) rejected with a bare `unknown style: lead`. The
  schema now enumerates the vocabulary and the refusal lists the valid spellings
  (`... (valid: Standard, Arpeggio, …, Percussion; case-sensitive)`), from ONE constant next to the
  parser.

## 2. P2-b in detail (the two distinct defects)

**(a) The spurious warmup dump — a real bug the plan hypothesised and this session confirmed.**
The watchdog samples `processBlockActive`/`warmupActive` every 250 ms and resets its hang counter on
a `justFinishedWarmup` edge. Two warmups on ONE child can be ~1 ms apart (warmup #1 clears
`processBlockActive`; the control loop starts #2 immediately), so the edge is never observed,
`hangMs` carries warmup #1's elapsed time into #2, and #2 trips its own
`warmupExpectedMs + 1000` threshold after only ~1 s — a **spurious dump during a healthy warmup**
(live evidence: warmups 26 ms apart, dump 1.87 s into #2).
*Fix:* `warmupEpoch` (`PluginHost.h`) is bumped before `warmupActive` is raised; the watchdog resets
`hangMs` when the epoch changes, so each warmup owns its own budget. It cannot hide a real hang — a
genuinely stuck `processBlock` still reaches the threshold inside its own window.
*Pin:* `PluginIsolation.BackToBackWarmupsWriteNoHangDump` (two back-to-back PREPAREs on one
Virus-named child) — **proven to fail without the fix** (measured: a 71,065-byte dump during warmup
#2) and pass with it (`dumps=(none)`).

**(b) The 1.5-2 GB dump size.** The watchdog used `MiniDumpWithFullMemory`; the emulated device's
firmware image dominates the child's address space. The watchdog's question is *where* `processBlock`
is stuck, which thread stacks answer. *Fix:* `src/proxy/DumpPolicy.h` — hang → `MiniDumpNormal`
(stack-only), crash (SEH) → full memory unchanged. *Measured:* a real hang now writes **70,825
bytes** (`PluginIsolation.RealHangWritesHangDump`), against the 330-670 MB / 1.5-2 GB pre-fix class.
*Not widened:* the 1 s non-warmup threshold still fires for a legitimately long offline-render block;
widening it would hide real hangs, and the artefact is now cheap. Documented rather than changed.

## 3. P2-a's loudness consequence (the statement the plan's gate asks for)

**The clamp only binds when the master gain is ABOVE unity AND a limiter slot is enabled.** With the
gain at or below 1 the post-gain clamp cannot bind (the chain's limiter already holds the output at
its ceiling, and multiplying down only moves it further away), so those projects render
**bit-identically**. With no limiter slot enabled the clamp is skipped entirely, so limiter-free
projects are bit-identical too. What changes: a project with `masterGain > 1` and an enabled limiter
no longer exceeds the ceiling — it used to clip at full scale (measured: ceiling 0.97 + gain 1.6 →
peak 1.0 with 22.7% of frames at FS), and now stops at the ceiling. **Loudness is preserved; the
clipping is removed.**

**The session's own project is provably unaffected:** `compositions/ion_rift/ion_rift.hdaw` has
`masterGain="0.5479999780654907"` (below unity) with the limiter slot enabled, so the clamp cannot
bind — and the arithmetic confirms it: 0.548 × the limiter's 0.97 ceiling = **0.53**, exactly the
final-master peak the composition session measured (its §6 item 1, "peaks at 0.53 (−5.5 dBFS)").
No render A/B is needed for this project; its render is unchanged by construction.

## 4. What is NOT closed

- **Nothing on the engine side.** Every gate in the plan except P2-a's documentation clause is
  ticked, and that clause is now this handoff (the plan's P2-a box can be ticked).
- **Composition-side choices from the session's §6** — not bugs, and not this session's call:
  the 0.53 master peak is the honest consequence of a `masterRms 0.18` brief target on dense
  material (normalise on delivery, or lower the target next time); the intro downlifter cell was
  removed because it landed on beat 0.
- **`begin_batch` remains stdio-only** (documented): HTTP/CLI agents must split into small groups and
  checkpoint between them.
- **Optional, deliberately not taken:** a readiness bit on the child's HEARTBEAT/READY reply would
  make "booting" explicit rather than inferred from a bounded wait — it is a two-process protocol
  change (both binaries + the version guard, lessons 41/42).

## 5. Verification (this session)

- `dsh-build-fast.bat test` → rc 0; **`dsh-build-fast.bat test hdaw_plugin_host` → rc 0** — needed
  because `test` mode does not compile `PluginHost.cpp`, so a child-side fix is otherwise left
  unbuilt (Gate 4/15).
- `powershell -File run-tests-sharded.ps1 -Shards 2` → **2130 passed, 0 failed, 2169 of 2169
  intended tests, 15.5 min, exit 0** (every shard `ran == intended`), then full-binary re-runs after
  the last edits: frontend 281/281, mcp 370/370, platform 217/217.
- `node tools/rpc_parity_map.mjs` → `tools 317 rpc 419 {"mapped":303,"mcp-only":14}`;
  `tests/unit/frontend/rpc_parity_map.inc` byte-unchanged.
- New/updated tests this session: `LiveRoutingSeam.Deviceless*` (4), `LiveRoutingSeam.*TrackNotFound*`
  (2), `SongPlan.Brief*` (2), `McpCoverageTest.ListTracks*` (2),
  `McpCoverageTest.ExplicitZeroTrackIdIsUnknownNotRequired`, `McpCoverageTest.SnapshotProject*`,
  `McpCoverageTest.LayerHandoffs*`, `Commands.StableTrackRef/SendRefZeroId*` (2),
  `FrontendServer.StartZeroBindsAnEphemeralPortAndTwoServersCoexist`,
  `DumpPolicy.HangDumpIsStackOnlyWhileTheCrashDumpKeepsFullMemory`, `PluginBootGate.*` (4),
  `PluginIsolation.BackToBackWarmupsWriteNoHangDump`, `Audition.UnknownStyleRefusalNamesTheValidSpellings`.

## 6. Where the traps now live (docs made true)

- `docs/skills/psy-song-session/reference.md` — the stable-id section (all read payloads now carry
  `trackID`), the stack-only dump note, and a new "A BOOTING plugin slot is not a broken one" section.
- `docs/build-and-testing.md` — the watchdog section records the stack-only policy and that the
  330-670 MB figures are pre-fix history.
- `docs/mcp-server-ops.md` — the exit-0/EOF path plus the new engine log line.
- `docs/psytrance-composition-guide.md` / `docs/composition-toolkit.md` — the brief `kind` contract
  (honoured, `type` fallback, conflicting pair refused).
