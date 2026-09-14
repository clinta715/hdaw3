# Plan: CLAP param metadata + effect explorer (scope B)

Date: 2026-09-14. Status: approved, implementing.

## Goal
Installed CLAP effects are explorable and drivable from MCP with the same
self-describing richness as internal FX (min/max/default/stepped/units).

## Success Gates (all must pass to declare done)
- [ ] Gate 1: `list_fx_params` on a CLAP slot returns min/max/default/
  stepped per param, verified against a real CLAP plugin on the isolated
  path (service + JSON). In-process branch reviewed + compiled; live
  execution blocked by a PRE-EXISTING headless crash on isolation-off
  audition render (no getParams precedes it; zero in-process CLAP test
  precedent in repo) — filed as follow-up, not this change.
- [ ] Gate 2: kind-filtered plugin listing works (`list_plugins` effect
  filter) and an agent can go installed-effect -> loaded slot -> meaningful
  param writes with no guessing (demonstrated end-to-end via MCP).
- [ ] Gate 3: `getParamText` at min/default/max exposed so agents see real
  units for the range endpoints (the describe flow).
- [ ] Gate 4: BLOCKED (environment, not code) — new tests green 3/3 +
  affected suites 79/79 with device live. Full/fast tiers now fail on
  session-audio outage (RDP session 1 Disc → "No driver" on every engine
  init; proven by log + qwinsta 2026-09-14). Re-run when audio returns.
  Pre-existing failures also recorded (MasterBusFx AV on pristine main).
- [ ] Gate 5: pitfall review clean — #3 (query path off audio thread), #12/
  #18 (no instantiation inside parked sections; explorer reuses scanner
  patterns), #14 (bounded strings over proxy pipe).

## Dependency Map (from graphify)
- Blast radius: PluginParamService.h (additive fields) ->
  PluginParamServiceImpl::getParams -> list_fx_params/set_fx_param ->
  Router_Plugin. Consumers of PluginParamSnapshot: list_fx_params,
  set_fx_param name lookup, Router_Plugin. Additive = backward compatible.
- Upstream: JUCE AudioProcessorParameter (normalized, no ranges) — the
  spike must confirm where CLAP clap_param_info_t is reachable per path
  (in-process wrapper vs isolated proxy pipe).
- Downstream: MCP list_fx_params JSON gains fields; set_fx_param unchanged
  (normalized) with mapping documented via min/max.
- God nodes in scope: none (param plumbing only, no graph/DSP changes).
- Community boundaries crossed: engine <-> mcp (established interface).
- Projections affected: none (no ValueTree schema changes).
- SPSC paths touched: none (param queries already command-thread).
- Path integrity: getParams resolves LIVE instance per call; isolated
  instances proxy through the child pipe — metadata must be captured at
  query time on the command thread, same as values today.

## Pitfall Gates Triggered
- #3 Audio-thread safety: queries stay on command/message thread (as today);
  no new locks on audio path; verify no getParams callers on audio thread.
- #12/#18 Graph/pump: explorer flow must NOT instantiate inside parked
  sections; reuse scanner-process patterns for headless describe.
- #14 Proxy bounds: cap param name/text/unit strings crossing the pipe
  (names can be long on some plugins); chunk or truncate with marker.
- Engine-change test discipline: new gtest for snapshot fields +
  loopback metadata test; run affected suites (mcp, engine, proxy).

## Spike findings (confirmed in code)
- In-process: custom CLAPParameter holds full clap_param_info_t; values
  normalize plain[min,max]->[0,1]. dynamic_cast in getParams suffices.
- Isolated: GET_PARAM_INFO handshake packs float default + u8 automatable
  + chunked name; child reads live CLAP info. Extend with doubles min/max
  + u32 flags, dataSize-guarded unpack (mixed-version safe).
- ProxiedParameter carries idx/name/default/automatable only; extend +
  accessors. getText stays empty isolated (documented; numerics suffice).
- list_plugins: juce::PluginDescription::isInstrument populated +
  persisted; just emit kind + filter. No engine risk.

## Steps
2. Extend PluginParamSnapshot (minVal/maxVal/defaultVal/stepped/unit).
3. Populate both paths; surface in list_fx_params; endpoint texts in
describe flow.
4. list_plugins kind filter (effect vs instrument).
5. Tests + docs (tool descriptions) + build + fast tier + full suite.
