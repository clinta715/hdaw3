# Vavra (microQ) live host-param delivery — localization plan

Status: **DIAGNOSED — verdict reverses the Vavra-specific reading; the gap is general
and HDAW-side.** Fix options await user sign-off (engine-adjacent: render/export paths).
Owner: session 2026-09-21
Related: `docs/plans/2026-09-21-device-param-map.md` (§Vavra finding — **this plan
supersedes its Vavra conclusion**), `docs/va-suite-status-log.md`,
`docs/handoffs/2026-09-18-gearmulator-custom-builds.md`

## 1. Question

Live host-param writes to Vavra (microQ) appeared to have no effect, while the same
params replayed offline did. Which stage loses the write?

## 2. Method

Ran the three existing real-plugin gates with the repo's own env-gated param-delivery
trace (`src/proxy/ParamTrace.h`, `HDAW_TRACE_PARAM=1` → `%TEMP%\hdaw_paramtrace_<pid>.log`;
parent and every isolated child get their own pid), plus static reading of both the
live and the render paths. No code was changed.

Stages: S1 parent stage → S2 100 ms flush into the shm ring → S3 child drain
(`params[i]->setValue`) → S4 child warm/idle clock → S5 wrapper → S6 emulator.

## 3. Evidence

**S1–S4 all PASS for Vavra.** Live gate
`FxMidiInjection.VavraHostParamsLiveReachability` (HDAW_TRACE_PARAM=1):

```
parent 29560:  P1 stageParam idx=6801 cache=7557 staged=1 STORE=1
parent 29560:  P3F FLUSHED tick=77 wrote=1 sw=1 sr=0
child  45288:  C1 SET idx=6801 v=0.050000
child  45288:  C1 WARM begin blocks=800 → C1 WARM done blocks=800
child  45288:  C1 DRAINED calls=1 pr=1 pw=1 → C1 IDLE arm → C1 IDLE clock begin
```
(`idx=6801` = `Ch 1 F1Cutoff`, `v=0.05` = exactly what the gate wrote.) The write is
delivered to the live child; there is **no Vavra delivery break**.

**The render that "proves" the gap never touches that child.** `auditionPlugin`
(`AudioEngineCommands_Composition.cpp:1291`) → `renderTrackWindow` (`:328`) →
**`em.startExport(treeCopy, …)`** (`:488`). Every audit/measurement render is an
**offline export of a tree copy into a fresh export child**, not a render of the live
graph. Trace corroborates: in the Vavra live gate the parent constructed 5 proxies
(`P2 ok n=7557` ×5) for 1 live slot + 4 renders; only the long-lived *live* child
(45288, 97207 trace lines) received `C1 SET` — the short-lived export children
(4.4k–25.5k lines) received none.

**A live plugin-param write can never be persisted into the tree.** For an external
(plugin) slot:
- `TrackFXSlot::getInternalParamDefs()` returns `{}` when `isExternal`
  (`TrackFXSlot.h:1316`), so `loadParamsFromTree` (`:1337`) is a no-op;
- the `param_N` tree listener early-returns for `fxType == "plugin"`
  (`AudioEngine.cpp:1303-1306`);
- the only cross-over channel is `IDs::pluginState` (device-dependent — a dead end
  for JE8086/Vavra, per `McpTools_Matrix.cpp:444-457`) or the
  `appliedParamOverrides` ledger, which is written **only** by
  `apply_matrix_preset` (`McpTools_Matrix.cpp:458-479`).

**The two "positive controls" are false.** Same trace on
`FxMidiInjection.XeniaHostParamsChangeRender`: parent staged `idx=1741`, and only the
long-lived live child (19848) received `C1 SET idx=1741 v=0.1`; the export children got
nothing — yet the gate passed on `|Δrms| = 5.1e-4`. That delta is fresh-child boot
noise, not a param effect. Likewise `NodalRed2xHostParamsChangeRender` Δ=2.7e-4. The
`OsirusBootPatchAwakening` "rms 0 → 0.047" control is the **ROM boot-patch fix**
(the test asserts `a.rms > 0.001` on its *first* audition — `fx_midi_injection_test.cpp`
:1357-1361), not a param write.

**The "same-child bimodality" premise is false.** The Vavra gate header describes two
modes "of the same child"; each render is a *different* fresh export child, so the
~2× swing is cross-child boot variation, not one child's mode flip.

## 4. Verdict

> The Vavra "live-path gap" does not exist as a Vavra defect. Host-param writes are
> delivered correctly to the **live** child for every isolated plugin; they are simply
> **never persisted into the project tree**, so *every* render built from a tree copy
> (export, `audition_plugin`, `verify_part`, `measure_*`) — i.e. every measurement the
> audit gates use — cannot see them. This is HDAW-side and fixable.

Corollaries: `XeniaHostParamsChangeRender` / `NodalRed2xHostParamsChangeRender` are not
valid live-audibility proofs; the documented `appliesVia: set_fx_param` route for
plugin params is not an end-to-end route (it is live-monitoring only); and the
RPC `project.setFxSlotParam` is a silent no-op for plugin slots (MCP `set_fx_param`
routes plugin slots to `PluginParamService::setParam` instead — an RPC/MCP divergence
for the #2 backlog).

## 5. Fix options (engine-adjacent — need sign-off before implementing)

**A — Durability (recommended).** Give plugin-slot param writes the same ledger
channel that `apply_matrix_preset` already uses: after the live write, merge
`appliedParamOverrides[idx]=value` into the slot. Put it in a **new shared command**
(so MCP `set_fx_param` and RPC `plugin.setParam` both get it — parity by construction),
plus a clear/reset tool so a stale override can be removed.
*Effect:* `set_fx_param` becomes render-visible and save/load-durable.
*Risk:* medium — the ledger currently has "replace semantics, one preset per render"
(`apply_matrix_preset`); a per-index merge changes that contract and both writers must
agree. A forgotten override applies to every later export.
*Effort:* ~1 new command + ledger merge + MCP/RPC wiring + a gate.

**B — Measurability.** In `renderTrackWindow`, before `startExport`, seed `treeCopy`'s
plugin-slot overrides from the **live** param cache so a windowed render reflects what
you can currently hear.
*Effect:* `audition_plugin` / `verify_part` become valid live-state probes; the live
audibility question becomes answerable and testable.
*Risk:* low-medium (windowed-render path + tree copy only). Caveat: it *masks* the
durability gap, so it must not be the only change.

**C — Honesty (no engine risk).** Fix the false claims in the gate headers (Xenia /
Nodal / "same child"), replace the local-cache reachability assertion with real
child-delivery evidence, and correct `docs/va-suite-status-log.md` +
`core-synths-agentic-guide.md` (the "third structural limit" I added 2026-09-21) to the
general rule: **plugin-slot host-param writes are live-monitoring only unless captured
via `pluginState` (`captureToTree`) or replayed via `appliedParamOverrides`.**

## 6. Deviation note

`hdaw-guard` §Execution Model requires implementation via subagent tasks. Subagents are
disabled by the user's global config, so this session works inline; the deviation is
recorded here and in `docs/plans/2026-09-21-rpc-parity-retrofit.md`. No code was written.
