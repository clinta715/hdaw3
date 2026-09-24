# Handoffs index

**What handoffs are:** per-session records of what a session did, found, and left
open. **The newest handoff supersedes older statements** about the same system;
older ones are kept for evidence trails. **Live specs live in `docs/*.md`, not in
handoffs** — treat handoff claims as history, verify against the docs.

| Date | File | Scope | Superseded-by |
|---|---|---|---|
| 2026-08-16 | wasapi-com-init.md | WASAPI/COM init fix: choppy audio, DirectSound-only device list | historical |
| 2026-08-17 | dx7-drop-handoff.md | DX7 SysEx import: drop handler + cartridge voice picker done, full-suite remaining | historical |
| 2026-08-17 | fm-analysis-channel.md | FM analysis channel end-to-end (C++ → frontend), continues fm-synth-audio-fixes | historical |
| 2026-08-17 | fm-synth-audio-fixes.md | FM synth audio-quality fixes — all 3 Dexed-analysis bugs closed | historical |
| 2026-08-17 | piano-roll-declutter.md | Piano roll de-clutter + per-tab bottom panel height | historical |
| 2026-08-17 | sampler-rpc-family-handoff.md | Sampler RPC/MCP family + slice UI — complete | historical |
| 2026-08-17 | staticy-audio-proxy-resync.md | Staticy audio + wedged exports: proxy ring resync, pacing, export lifecycle | historical |
| 2026-08-18 | disconnect-during-export-fix.md | Disconnect-during-export crash fix | historical |
| 2026-08-18 | fm-pitch-fix.md | FM pitch fix + export volume investigation | historical |
| 2026-08-18 | track-building-automation.md | Track-building automation: "HDAW builds the track, not the LLM" | historical |
| 2026-08-18 | track-tooling-handoff.md | Track-building via HDAW tools vs LLM hand-coding | historical |
| 2026-08-18 | track-tooling-next-steps.md | Track tooling next steps for a fresh context | historical |
| 2026-08-19 | clap-preset-wiring-shipped.md | CLAP preset→program wiring shipped; start role defaults + namespace gaps | historical |
| 2026-08-19 | composer-agenda-remaining.md | Composer agenda #4/#2/#1 shipped; remaining items (session 3) | historical |
| 2026-08-19 | composer-remaining-items.md | Instrument-part composer shipped; remaining composer work | historical |
| 2026-08-19 | part-templates-shipped.md | Part-templates/role-defaults shipped; start namespace gaps (session 5) | historical |
| 2026-08-19 | plugin-audition-next-steps.md | Plugin preset audition shipped; remaining composer items | historical |
| 2026-08-20 | composition-tooling-handoff.md | Composition tooling improvements | historical |
| 2026-08-20 | namespace-fix-shipped.md | Proxy namespace fix shipped; v0.23.2 (session 6) | historical |
| 2026-08-20 | verification-handoff.md | Composition tooling verification & commit | historical |
| 2026-08-21 | b7-shipped-b6-plan.md | v0.24.0+batch-c: Batch B + B5 + B6 + B7 complete | historical |
| 2026-08-21 | batch-a-shipped-batch-b-plan.md | Batch A UI coverage + Batch B plan | historical |
| 2026-08-21 | batch-b-shipped-batch-c-plan.md | Batch B shipped + Batch C plan | historical |
| 2026-08-23 | fm-synth-sysex-parse-fix.md | FM synth SysEx parsing fix | historical |
| 2026-08-23 | test-failures-and-engine-bugs-fixed.md | 7 test failures + 3 engine bugs fixed | historical |
| 2026-08-23 | voltage-dnb-bugs-streamlining-shipped.md | Voltage DnB bugs + streamlining shipped | historical |
| 2026-08-23 | voltage-dnb-mcp-composition.md | Voltage DnB MCP composition session | historical |
| 2026-08-24 | dnb-crash-generator-bugs-backlog.md | DnB session 2: verify/save crash + generator bugs + backlog | historical |
| 2026-08-25 | timbrelib-vendored-and-drop-polish.md | TimbreLib vendored into hdaw3 + in-flight drop polish | historical |
| 2026-08-27 | mcp-cluster-compose-session-bugs.md | MCP cluster-compose session: engine bugs found (6 tracks, ~1h audio) | historical |
| 2026-08-29 | jungle-dnb-composition-session.md | Jungle/DnB composition: knowledge, feature gaps, bugs | historical |
| 2026-08-30 | pi-mcp-compose-session.md | First pi-hosted psytrance composition via MCP + launcher fix | historical |
| 2026-08-31 | export-silence-bug.md | Export bug: audio cuts off after 0.6 s | historical |
| 2026-08-31 | psytrance-v6-bugs-handoff.md | Psytrance v6 composition bugs (restarts, render hangs) | historical |
| 2026-09-01 | psyfm-bugs-handoff.md | PsyFm integration bugs (open) + what shipped | historical |
| 2026-09-01 | psytrance-composition-handoff.md | Psytrance composition session + engine hardening | historical |
| 2026-09-02 | export-silence-0.6s.md | Export silence after exactly 0.6 s — RESOLVED, see 2026-09-17 investigation | historical |
| 2026-09-02 | markov-pads-vague-structure-timbre-keybpm.md | Markov pads + vague macro-structure + timbre key/BPM (v0.26.0) | historical |
| 2026-09-03 | markov-floor-canon-presets-p0-p1-style-packs.md | Markov floor canon + factory FX presets + ontology P0/P1 + style-pack decision | historical |
| 2026-09-04 | composition-demo.md | End-to-end composition demo (sweep → libraries → cluster/search → markov) | historical |
| 2026-09-04 | fm-session-handoff.md | FM/DX7 patch pipeline completed (persistence, export, sweep, search) | historical |
| 2026-09-05 | chorus-noop-investigation.md | Internal chorus FX is a no-op (byte-identical output) | historical |
| 2026-09-06 | psytrance-remix-session.md | Psytrance remix session | historical |
| 2026-09-07 | rave-latent-space-features.md | RAVE latent-space features (morph / generate / probe) | historical |
| 2026-09-08 | corpus-arranger-engine-bugs.md | CorpusArranger shipped; engine bugs surfaced | historical |
| 2026-09-08 | drum-phrase-bank-corpus.md | Corpus-derived drum phrase bank (v0.30.0) | historical |
| 2026-09-08 | filter-fix-and-key-quantization.md | Filter fix, key quantization, arrangement fix | historical |
| 2026-09-08 | serum2-preset-catalog-investigation.md | Serum 2 preset semantic catalog investigation | historical |
| 2026-09-09 | rave-virus-engine-bugs.md | RAVE-in-Mix workflow, Virus patch pipeline, engine bugs (session 2) | historical |
| 2026-09-13 | modular-dawn-session-audit.md | Modular Dawn composition + engine/MCP/workflow audit | historical |
| 2026-09-14 | concrete-bloom-session-audit.md | Concrete Bloom (techno-psy) + full session audit | historical |
| 2026-09-16 | matrix-presets-handoff.md | Matrix presets: harvest → curate → apply (EXECUTED, closed 2026-09-16/17) | historical |
| 2026-09-16 | terra-signal-reimagined.md | Terra Signal re-imagined with VA-suite tooling | historical |
| 2026-09-17 | export-silence-investigation.md | Export silence bug (0.6 s cutoff) — RESOLVED | historical |
| 2026-09-18 | gearmulator-custom-builds.md | Custom gearmulator builds: patch-state delivery + offline fidelity | historical |
| 2026-09-20 | je8086-userpatch-dt1-fix.md | JE8086 UserPatch DT1 dumps now apply (wrapper retarget + route recall removal) | historical |
| 2026-09-21 | mcp-dogfood-composition.md | MCP dogfood: compose via MCP surface only, record what the surface needs | historical |
| 2026-09-21 | plugin-param-persistence-handoff.md | Plugin-slot param persistence A+B+C shipped and gated | historical |
| 2026-09-22 | agent-composition-session.md | Verification pipeline, sound corpus, crash forensics (consolidated) | historical |
| 2026-09-22 | core-synth-isolated-children-silent.md | Core-synth isolated children render silence — re-voice pass blocked | historical |
| 2026-09-22 | engine-heap-corruption-investigation.md | Engine heap-corruption crash + "empty project after reconnect" | historical |
| 2026-09-22 | patch-selection-and-sweep.md | Patch-selection tooling + dsp sweep status | historical |
| 2026-09-23 | remaining-issues.md | What remains after mixer-return / automation / key work | 2026-09-24-track-identity-and-parity.md |
| 2026-09-24 | track-identity-and-parity.md | Track surface parity, stable ids (B1/B2), native-Windows tooling | **CURRENT** |
