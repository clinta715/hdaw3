# CorpusArranger — interface contract (for other models / MCP consumers)

## 1. Naming
- Method: **CorpusArranger** (corpus-sampled arrangement grammar).
- MCP tool (proposed): **`generate_arrangement_corpus`** (parallels existing `generate_arrangement`; `_corpus` suffix marks the source, distinguishes from `generate_psytrance_markov`).
- RPC (proposed): **`composition.generateArrangementCorpus`**.
- Engine class (future): `CorpusArranger` next to `MarkovArranger` (kept, deprecated).

## 2. Why this interface shape (model-efficiency decisions)
1. **One call to plan.** Structure is a single deterministic seed → one compact JSON. The consuming model does NOT loop over sections or re-ask for each part.
2. **Deterministic.** Same seed → same plan (RPC contract, testable). Model can iterate by only changing the seed.
3. **Compact + complete.** Numbers only (bars), role names = the same palette tokens the model already maps via `paletteTrackIds`. No note payloads; composition is a separate stage.
4. **Explicit flags.** The choice-axes are returned (`flags`) so the model can see WHAT was sampled and override next call (e.g. `lateNovelty:true`).
5. **Mirrors existing conventions.** Same return shape style as `generate_psytrance_markov` (, skipped, determinism), so models that learned one tool can reuse the pattern.

## 3. Proposed tool schema
```
generate_arrangement_corpus
  params (all optional except paletteTrackIds):
    seed            int             seeded; deterministic plan
    bars?           int             28..256; overrides length sampling
    lengthMode?     short|mid|extended
    kickIntro?      fourOnFloor|shortIntro|midIntro|longIntro
    constBass?      bool
    lateNovelty?    bool
    breakdown?      bool
    noveltyRole?    lead|chord|arp2|fx
    paletteTrackIds obj             role name -> track index
                                   (kick,bass,hat,snare,clap,arp,stab,pad,riser,down,lead)
  returns:
    { method, version, seed, length:{bars,mode}, flags: {...},
      sections:[{name,barStart,bars,barEnd,roles[],density}],
      layers:{role:{firstBar,lastBar,const?,novelty?}},
      skipped:[unmapped roles] }
```

## 4. Example deterministic output (seed 7, 95 bars)
```json
{ "method":"corpusArranger","version":1,"seed":"7",
  "length":{"bars":95,"mode":"mid"},
  "flags":{"kickIntro":"shortIntro","constBass":false,"lateNovelty":true,"breakdown":true,"noveltyRole":"lead","kicklessBreakdown":true},
  "sections":[
    {"name":"intro","barStart":0,"bars":4,"roles":["pad","fx"],"density":0.3},
    {"name":"build1","barStart":4,"bars":22,"roles":["pad","fx","kick","hats"],"density":0.55},
    {"name":"dropA","barStart":26,"bars":18,"roles":["kick","bass","hats","snare","clap","arp","stabs","fx"],"density":0.85},
    {"name":"minibreak","barStart":44,"bars":4,"roles":["bass","pad"],"density":0.45},
    {"name":"build2","barStart":48,"bars":6,"roles":["kick","bass","hats","arp","fx","riser"],"density":0.7},
    {"name":"dropB","barStart":54,"bars":21,"roles":["kick","bass","hats","snare","clap","arp","stabs","fx","lead"],"density":0.95},
    {"name":"outro","barStart":75,"bars":20,"roles":["bass","pad","fx"],"density":0.35}
  ],
  "layers":{"pad":{"firstBar":0,"lastBar":95},"kick":{"firstBar":2,"lastBar":81},"lead":{"firstBar":54,"lastBar":75,"novelty":true}} }
```
Model workflow: call once → map `paletteTrackIds` → mute/arrange per `sections` → add notes per `layers` → humanize. No structure iteration needed.

## 5. Prototype files
- `tools/corpus_arrange/generate.mjs` — seeded sampler (dependency-free). CLI: `node generate.mjs <seed> [seed...]`.
- `tools/corpus_arrange/distributions.json` — measured axis distributions (source `compositions/psytrance_corpus_fulltracks.tsv`, n=533).
- `compositions/psytrance_corpus_fulltracks.tsv` — the 533-track structural tags.

## 6. Markov deprecation plan (keep, do-not-nuke)
- KEEP `generate_psytrance_markov` + `MarkovArranger.cpp` + `PsytranceMarkovGenerator.h`.
- It produced genuinely interesting results (vocal micro-patterns, section-energy jitter, theme rotation).
- Deprecation scope: DOCUMENT as legacy structural path in the MCP tool description and AGENTS.md; do not remove code or existing callers.
- Markov remains the **micro-pattern / variation engine** inside sections (per-section cells, theme rotation, velocity arcs); CorpusArranger becomes the **structure engine** (enter/exit schedule).
- Migration: keep `paletteTrackIds` contract identical so models can swap the call without re-mapping.

## 7. Next (engine phase, when session is on the fixed binary)
1. Port `sampleArrangement` to C++ (`CorpusArranger` next to MarkovArranger) OR expose the JS plan via RPC and keep the sampler in tools/.
2. Add MCP  `generate_arrangement_corpus` + RPC `composition.generateArrangementCorpus`, one undo unit, one clip per role (mirrors markov).
3. Mark Markov deprecated in tool docs. 4. Regression test: seed determinism + apply-to-project smoke. 5. Rebuild with trackIds fix for stems/RAVE hygiene.
## 8. ENGINE PHASE — DONE (2026-09-0X)
- `src/engine/CorpusArranger.{h,cpp}` — planner + score generator (HarmonyEngine reuse).
- Command `AudioEngineCommands::generateArrangementCorpus` + RPC `composition.generateArrangementCorpus` + MCP `generate_arrangement_corpus`.
- Tests: `tests/unit/engine/corpus_arranger_test.cpp` (5 tests: determinism, contiguity, forced axes, score writing, RPC round-trip) — all pass; 69-test regression sweep green (markov + export fixes).
- Markov: kept, doc-deprecated as the structural generator; still the micro-pattern source.
- Known V1 limits: per-role note writers are basic (no corpus velocity arcs yet); no MCP-level test yet (command/RPC covered). Next: use the tool post-session-restart; add velocity/density arcs from the corpus TSV.
