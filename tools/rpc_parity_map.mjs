#!/usr/bin/env node
// rpc_parity_map.mjs — derive the MCP-tool <-> RPC-method parity ledger.
//
// WHY THIS EXISTS
//   AGENTS.md's standing rule is "every MCP tool must also be reachable as namespace.method
//   over the frontend JSON-RPC surface". Nothing enforced it, which is how the whole matrix
//   and tuning domains stayed MCP-only (docs/plans/2026-09-21-rpc-parity-retrofit.md).
//   The mapping is SEMANTIC, not mechanical (rave_get_config -> settings.getRaveConfig,
//   pool_list -> pool.list), so a name-only check cannot prove parity. What this script +
//   the ratchet test CAN do:
//     * require every registered MCP tool to be CLASSIFIED (no silent additions),
//     * require every mapped RPC method to EXIST (a phantom mapping fails),
//     * mark unresolved tools as an explicit, reviewable queue.
//   It does not prove semantic equivalence; it makes drift impossible to introduce quietly.
//
// CLASSIFICATION (in order)
//   1. exact   — tool name == camelCase of an RPC method
//   2. rule    — a verified alias/prefix rule from the slice 0-4 audit (RULES below)
//   3. mcp-only — no 1:1 route by design, with a reason
//   4. unresolved — no name-derived route; the REVIEW QUEUE (counted by the gate)
//
// A token-overlap matcher was TRIED AND REJECTED here (2026-09-21): it produced 37 rows
// such as add_notes -> settings.clearNotes, open_midi_device -> midi.getOpenDevice and
// list_clips -> settings.removeClips — mostly wrong, because dropping generic verbs
// (set/get/add/remove) collapses opposites. Shipping those as "candidates" would have put
// false claims in the ledger and PASSED the gate (the targets do exist). Same
// false-positive class as the keyword screen this retrofit exists to eliminate.
//
// OUTPUT: tests/unit/frontend/rpc_parity_map.inc  (a C++ raw string literal, included by
// tests/unit/frontend/rpc_parity_ratchet_test.cpp, so the gate needs no runtime path
// resolution). Regenerate with:  node tools/rpc_parity_map.mjs
//
// Line format (TAB separated):  tool <TAB> status <TAB> rpc-or-dash <TAB> note

import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');

// ---- inventories ------------------------------------------------------------
const tools = [];
for (const f of fs.readdirSync(path.join(ROOT, 'src/mcp'))) {
  if (!f.endsWith('.cpp')) continue;
  const txt = fs.readFileSync(path.join(ROOT, 'src/mcp', f), 'utf8');
  const re = /registerTool\(\{\s*"([a-z0-9_]+)"/g;
  let m;
  while ((m = re.exec(txt)) !== null) tools.push({ tool: m[1], file: f });
}

const nsConst = {};
{
  const h = fs.readFileSync(path.join(ROOT, 'src/frontend/FrontendRpc.h'), 'utf8');
  const re = /const char\*\s+(\w+)\s*=\s*"([^"]+)"/g;
  let m;
  while ((m = re.exec(h)) !== null) if (!m[2].includes('.')) nsConst[m[1]] = m[2];
}
const fnToNs = {};
{
  const t = fs.readFileSync(path.join(ROOT, 'src/frontend/FrontendRouter.cpp'), 'utf8');
  // Split the dispatch chain into branches at each `method::X` and take the dispatch
  // function called inside that branch. Handles BOTH forms used here: the one-liner
  // (`else if (ns == method::Settings) return dispatchSettings(...)`) and the multi-line
  // block (`if (ns == method::Project) { ... return dispatchProject(...); }`).
  const marks = [...t.matchAll(/method::(\w+)/g)];
  for (let i = 0; i < marks.length; ++i) {
    const ns = nsConst[marks[i][1]];
    if (!ns) continue;
    const seg = t.slice(marks[i].index, i + 1 < marks.length ? marks[i + 1].index : t.length);
    const call = seg.match(/(dispatch\w+)\s*\(/);
    if (call) fnToNs[call[1]] = ns;
  }
}
const rpc = new Map();   // "ns.method" -> router file
const nsUnresolved = [];
for (const f of fs.readdirSync(path.join(ROOT, 'src/frontend/router'))) {
  if (!f.endsWith('.cpp')) continue;
  const txt = fs.readFileSync(path.join(ROOT, 'src/frontend/router', f), 'utf8');
  // Attribute each method to the ENCLOSING dispatch function: Router_Project.cpp hosts
  // both dispatchProject and dispatchSettings, and attributing to the union of a file's
  // namespaces invented "settings.addAudioClip" (caught by the ratchet gate's dispatch
  // probe, 2026-09-21).
  const fns = [];
  const fre = /DispatchResult\s+(\w+)\s*\(/g;
  let fm;
  while ((fm = fre.exec(txt)) !== null)
    if (fnToNs[fm[1]]) fns.push({ at: fm.index, ns: fnToNs[fm[1]] });
  if (!fns.length) { nsUnresolved.push(f); continue; }
  const nsAt = (pos) => { let ns = fns[0].ns; for (const e of fns) if (e.at <= pos) ns = e.ns; return ns; };
  const re = /if\s*\(\s*m\s*(?:==|!=)\s*"([A-Za-z0-9_]+)"\s*\)/g;
  let m;
  while ((m = re.exec(txt)) !== null) rpc.set(nsAt(m.index) + '.' + m[1], f);
}

// ---- classification rules ---------------------------------------------------
const camel = s => s.replace(/_([a-z0-9])/g, (_, c) => c.toUpperCase());

// Verified aliases: the tools whose RPC twin has a different name or lives in another
// namespace. Each of these came out of the slice 0-4 semantic audit (docs/plans/
// 2026-09-21-rpc-parity-retrofit.md) — not from a guess. The ratchet test re-verifies that
// every target below still EXISTS, so a rename here fails the build instead of rotting.
const ALIASES = {
  rave_get_config: 'settings.getRaveConfig',
  rave_set_config: 'settings.setRaveConfig',
  list_matrix_presets: 'matrix.listPresets',
  apply_matrix_preset: 'matrix.applyPreset',
  analyze_tuning: 'tuning.analyze',
  pool_list: 'pool.list',
  set_fx_param: 'pluginParam.setParam',
  list_fx_params: 'pluginParam.getParams',
  clear_fx_param_overrides: 'project.clearPluginParamOverrides',
  get_fx_capture_status: 'audio.getFxCaptureStatus',
  set_cells: 'composition.setCellRecipes',
  set_cell: 'composition.setCellRecipe',
  remove_cell: 'composition.removeCellRecipe',
  get_cells: 'composition.getCells',
  fill_cells: 'composition.fillCells',
  reroll: 'composition.rerollCells',
  audit_song_structure: 'composition.auditSongStructure',
  get_layer_handoffs: 'composition.getLayerHandoffs',
  set_layer_handoff: 'project.setLayerHandoff',
  clear_layer_handoff: 'project.clearLayerHandoff',
  get_clip_provenance: 'composition.getClipProvenance',
  set_song_plan: 'composition.setSongPlan',
  get_song_plan: 'composition.getSongPlan',
  apply_song_brief: 'composition.applySongBrief',
  export_song_brief: 'composition.exportSongBrief',
  save_section_template: 'composition.saveSectionTemplate',
  load_section_template: 'composition.loadSectionTemplate',
  list_section_templates: 'composition.listSectionTemplates',
  list_device_params: 'device.listParams',
  param_verity: 'composition.verifyParamSweep',
  param_verity_corpus: 'composition.verifyParamCorpus',
  tone_verity: 'audio.verifyTone',
  select_patch: 'library.selectPatch',
  // Verified while completing the verdict (2026-09-21): the audit moved to
  // src/common/ModulationCoverage.cpp and gained this RPC twin; list_lfos' own description says
  // it mirrors read.getModulationLfos.
  audit_modulation_coverage: 'modulation.coverage',
  list_lfos: 'read.getModulationLfos',
};

// Prefix rules: a tool family that maps onto a namespace, remainder camelCased. Verified
// per family against the router's method list by the ratchet test.
const PREFIXES = [
  ['psy_fm_', 'psy_fm'],
  ['rave_', 'rave'],
  ['preview_', 'preview'],
  ['session_', 'session'],
  ['sampler_', 'sampler'],
];

// Tools with no 1:1 route by design. The note is the review reason the ratchet test
// requires (an empty note fails). Aggregates dispatch several RPC methods by argument;
// plumbing exists only for the MCP surface.
const MCP_ONLY = {
  poll_job: 'MCP-side async job poller (rave.jobStatus / tuning.jobStatus cover the RPC side)',
  transport: 'aggregate: dispatches transport.play/pause/stop by argument (no 1:1 route)',
  seek: 'aggregate: dispatches transport.seekToSeconds/seekToSample by unit',
  engine_info: 'MCP server introspection (engine version/binary), no engine route needed',
  engine_restart: 'MCP/engine lifecycle control for the test harness',
  snapshot_project: 'covered by read.snapshot (whole-project JSON)',
  project_info: 'covered by read.snapshot / project.* accessors',
  scan_plugins: 'plugin scan progress is an MCP job; settings.* covers the device surface',
  list_plugins: 'covered by settings.listPlugins routes',
  // (anything else unresolved is emitted as status=unresolved with a review note)
};

// ---- classify --------------------------------------------------------------
const rpcNames = [...rpc.keys()];
const methodToNs = new Map();
for (const r of rpcNames) {
  const [ns, m] = r.split('.');
  if (!methodToNs.has(m)) methodToNs.set(m, []);
  methodToNs.get(m).push(ns);
}
const STOP = new Set(['set','get','list','add','remove','delete','apply','load','save','create','update','clear','is','toggle','start','stop','do','make','new']);
const words = s => s.replace(/([a-z0-9])([A-Z])/g, '$1 $2').toLowerCase().split(/[^a-z0-9]+/).filter(Boolean);
const sig = s => words(s).filter(w => !STOP.has(w));
const rpcToks = rpcNames.map(r => ({ r, t: new Set(sig(r.split('.')[1])) }));

// Tools the name heuristic maps WRONG: a name-derived match that is a semantically DIFFERENT
// capability. Emitted as unresolved (review queue) with the reason, so the ledger never reads as a
// verified mapping. The live-probe gate cannot catch this class — both routes exist.
const FORCE_REVIEW = {
  apply_preset: 'name-derived match to matrix.applyPreset is WRONG (matrix presets are a different '
    + 'family) — this preset front door needs its own route',
};

const rows = [];
for (const { tool } of tools) {
  if (FORCE_REVIEW[tool]) { rows.push([tool, 'unresolved', '-', FORCE_REVIEW[tool]]); continue; }
  if (ALIASES[tool] && rpc.has(ALIASES[tool])) { rows.push([tool, 'mapped', ALIASES[tool], 'alias (verified; see the ledger notes)']); continue; }
  const c = camel(tool);
  if (methodToNs.has(c)) {
    const ns = methodToNs.get(c)[0];
    if (ns !== '?') { rows.push([tool, 'mapped', ns + '.' + c, 'exact camelCase match (name-derived)']); continue; }
  }
  let hit = null;
  for (const [pre, ns] of PREFIXES) {
    if (tool.startsWith(pre)) {
      const r = ns + '.' + camel(tool.slice(pre.length));
      if (rpc.has(r)) { hit = r; break; }
    }
  }
  if (hit) { rows.push([tool, 'mapped', hit, 'prefix rule']); continue; }
  if (MCP_ONLY[tool]) { rows.push([tool, 'mcp-only', '-', MCP_ONLY[tool]]); continue; }
  rows.push([tool, 'unresolved', '-', 'no name-derived route — REVIEW (semantic audit needed)']);
}

// ---- emit ------------------------------------------------------------------
const counts = rows.reduce((a, r) => (a[r[1]] = (a[r[1]] || 0) + 1, a), {});
rows.sort((a, b) => a[0].localeCompare(b[0]));
const header = [
  '// GENERATED by tools/rpc_parity_map.mjs — do not edit by hand.',
  '// ' + `${tools.length} MCP tools, ${rpcNames.length} RPC methods`,
  '// ' + Object.entries(counts).map(([k, v]) => `${k}=${v}`).join(' '),
  '// mapped targets are verified to EXIST by the ratchet test (live dispatch probe);',
  '// mcp-only and unresolved rows are an explicit review queue, not silent omissions.',
].join('\n');
const body = rows.map(r => [r[0], r[1], r[2], r[3]].join('\t')).join('\n');
const out = header + '\ninline constexpr const char* kRpcParityMap = R"RPCPARITYMAP(\n' + body + '\n)RPCPARITYMAP";\n';
const dest = path.join(ROOT, 'tests/unit/frontend/rpc_parity_map.inc');
fs.writeFileSync(dest, out);
console.log('wrote', dest);
console.log('tools', tools.length, 'rpc', rpcNames.length, JSON.stringify(counts));
if (nsUnresolved.length) console.log('WARNING routers with no resolved namespace:', nsUnresolved.join(', '));
console.log('RPC_ONLY methods (no MCP tool claims them):', rpcNames.length - new Set(rows.map(r => r[2]).filter(r => r !== '-')).size);
if (process.argv.includes('--show-unresolved')) {
  for (const r of rows.filter(r => r[1] === 'unresolved' || r[1] === 'candidate'))
    console.log(' ', r[1].padEnd(10), r[0].padEnd(32), r[2]);
}
