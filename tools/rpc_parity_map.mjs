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
//   2. rule    — a verified alias/prefix/fan-out rule from the slice 0-4 audit (RULES below)
//   3. mcp-only — no 1:1 route by design, with a reason
//   4. unresolved — the REVIEW QUEUE (counted by the gate): either no name-derived route
//      at all, or a reason from FORCE_REVIEW (a route exists but is NOT this operation) /
//      NO_ROUTE (no route exists; the missing one is named).
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
const intercepts = new Map();   // "ns.method" -> 'FrontendRouter.cpp'
// A route predicate is `if (m == "…")`, but a branch may CHAIN methods:
//   if (m == "setMasterFxParam" || m == "setMasterFxBypassed") { … }
// (landed by the 2026-09-24 parity wave). The old single-literal pattern required the
// literal to be immediately followed by `)` and so silently dropped BOTH chained
// methods. Match the whole condition and harvest EVERY literal in it, so a chained
// branch attributes all its methods instead of none.
const METHOD_LITERAL = /"([A-Za-z0-9_]+)"/g;
const IF_M_CHAIN     = /if\s*\(\s*(m\s*(?:==|!=)\s*"[A-Za-z0-9_]+"(?:\s*\|\|\s*m\s*(?:==|!=)\s*"[A-Za-z0-9_]+")*)\s*\)/g;
{
  const t = fs.readFileSync(path.join(ROOT, 'src/frontend/FrontendRouter.cpp'), 'utf8');
  // Split the dispatch chain into branches at each `method::X` and take the dispatch
  // functions called inside that branch. Handles BOTH forms used here: the one-liner
  // (`else if (ns == method::Settings) return dispatchSettings(...)`) and the multi-line
  // block (`if (ns == method::Project) { ... return dispatchProject(...); }`).
  // EVERY call in the branch is registered, not just the first: a branch may dispatch
  // to several functions (method::Project now intercepts removeTrack / addTrackWithFx
  // before falling through to dispatchProject), and taking only the first dropped
  // `dispatchProject` from the map — which made the enclosing-function attribution in
  // Router_Project.cpp fall back to the file's first KNOWN function and relabel every
  // project.* route as settings.* (caught 2026-09-23 by regenerating after the
  // parity-twin slice; the ratchet's live dispatch probe is what keeps this honest).
  const marks = [...t.matchAll(/method::(\w+)/g)];
  for (let i = 0; i < marks.length; ++i) {
    const ns = nsConst[marks[i][1]];
    if (!ns) continue;
    const seg = t.slice(marks[i].index, i + 1 < marks.length ? marks[i + 1].index : t.length);
    for (const call of seg.matchAll(/(dispatch\w+)\s*\(/g)) fnToNs[call[1]] = ns;
    // Branch-level intercepts: a method whose route lives in a dispatch function that
    // does NOT spell the method name itself (removeTrack / addTrackWithFx are resolved
    // here and then delegated) never appears in a router file's `if (m == "…")` chain.
    // Record them with the branch's namespace so they stay in the ledger.
    for (const hit of seg.matchAll(IF_M_CHAIN))
      for (const name of hit[1].matchAll(METHOD_LITERAL))
        intercepts.set(ns + '.' + name[1], 'FrontendRouter.cpp');
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
  for (const m of txt.matchAll(IF_M_CHAIN))
    for (const name of m[1].matchAll(METHOD_LITERAL))
      rpc.set(nsAt(m.index) + '.' + name[1], f);
}
// Branch-level intercepts resolved in FrontendRouter.cpp itself (see above). Only added
// when no router file already claims the route, so a real file attribution always wins.
for (const [key, file] of intercepts)
  if (!rpc.has(key)) rpc.set(key, file);

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

  // ---- 2026-09-23 review pass: the unresolved queue, classified ------------------
  // Every target below was read out of the router body, not guessed from the name: it is
  // the route that reaches the SAME entry point the tool calls (ProjectCommands /
  // AudioEngineCommands / a service / the shared shaping header). Where the route's
  // interface differs (a wider/narrower argument form) the note says so, so a `mapped`
  // row never claims more equivalence than the code supports. A value is either the route
  // or [route, note] — the note replaces the generic alias note for that row.
  //
  // FX slots (Router_Project.cpp "--- FX ---"): the tools and these routes call the same
  // ProjectCommands methods, which is why the mapping is by entry point, not by name.
  add_fx: ['project.addFxSlot', 'shared entry point: ProjectCommands::addFxSlot (internal fxType or pluginId)'],
  remove_fx: ['project.removeFxSlot', 'shared entry point: ProjectCommands::removeFxSlot'],
  set_fx_bypass: ['project.setFxSlotBypassed', 'shared entry point: ProjectCommands::setFxSlotBypassed'],
  add_midi_fx: ['project.addMidiFxSlot', 'shared entry point: ProjectCommands::addMidiFxSlot'],
  remove_midi_fx: ['project.removeMidiFxSlot', 'shared entry point: ProjectCommands::removeMidiFxSlot'],
  set_midi_fx_bypass: ['project.setMidiFxSlotBypassed', 'shared entry point: ProjectCommands::setMidiFxSlotBypassed'],
  set_midi_fx_param: ['project.setMidiFxSlotParam', 'shared entry point: ProjectCommands::setMidiFxSlotParam (paramName + value, same keys)'],
  set_internal_fx_param: ['project.setFxSlotParam', 'shared entry point: ProjectCommands::setFxSlotParam — the route takes paramIndex; the tool also resolves paramName to it'],
  get_internal_fx_param: ['read.getInternalFxParams', 'same ReadModel::getInternalFxParams payload; the tool can project one paramIndex'],
  list_fx: ['read.getFxSlots', 'identical payload by construction (common/SendJson.h shaping, read by both)'],
  list_midi_fx_params: ['read.getMidiFxSlots', 'same ReadModel::getMidiFxSlots snapshot; the tool projects one slotIndex'],
  restart_fx: ['project.respawnPlugin', 'shared entry point: ProjectCommands::respawnFxSlot (the route is named for the plugin, the command respawns the slot)'],
  toggle_plugin_editor: ['audioGraph.toggleFXEditor', 'shared entry point: ProjectCommands::toggleFXEditor'],
  // FX chain presets (Router_Project.cpp "FX chain presets"): both sides drive
  // ChainLibrary + the export/apply chain commands.
  list_fx_chains: ['project.listFxChainPresets', 'shared entry point: ChainLibrary::userLibrary().listPresets()'],
  save_fx_chain: ['project.saveFxChainPreset', 'shared entry point: ProjectCommands::exportFxChain + ChainLibrary::savePreset'],
  load_fx_chain: ['project.loadFxChainPreset', 'shared entry point: ChainLibrary::loadPreset + ProjectCommands::applyFxChain'],
  delete_fx_chain: ['project.deleteFxChainPreset', 'shared entry point: ChainLibrary::userLibrary().deletePreset'],
  // Plugin presets / scanning.
  load_plugin_preset: ['pluginParam.setCurrentProgram', 'same PluginParamService::setCurrentProgram (the tool resolves the slot\'s pluginId first)'],
  list_plugin_presets: ['pluginParam.listPrograms', 'same program list; the route asks the live instance via PluginParamService, the tool prefers the scan-cache preset info then falls back'],
  search_plugin_presets: ['plugin.searchPresets', 'same PluginManager preset-cache walk (PluginServiceImpl::searchPresets)'],
  is_plugin_blacklisted: ['plugin.isBlacklisted', 'same PluginService::isBlacklisted'],
  // File libraries (Router_Library.cpp): 1:1 with FileLibraryManager; the cluster/preset
  // routes even carry "same params/shape as the MCP tool" in their own comments.
  remove_library: ['library.remove', 'same FileLibraryManager::removeLibrary'],
  list_libraries: ['library.list', 'same FileLibraryManager::getLibraryIds/getLibraryInfo fields'],
  scan_library: ['library.scan', 'same FileLibraryManager::scanAll/scanLibrary (id or all)'],
  search_library: ['library.search', 'same FileLibraryManager::search filters'],
  get_library_entry: ['library.getEntry', 'same FileLibraryManager::getEntry fields'],
  cluster_library: ['library.cluster', 'route comment: same params/shape as the MCP cluster_library tool (FileLibraryManager::clusterLibrary); the tool\'s memberLimit truncation is MCP-side, the route returns full member arrays'],
  related_samples: ['library.related', 'route comment: same params/shape as the MCP related_samples tool (FileLibraryManager::relatedSamples)'],
  list_cluster_presets: ['library.clusterPresetsList', 'route comment: mirror of the MCP list_cluster_presets tool'],
  get_cluster_preset: ['library.clusterPresetsGet', 'route comment: mirror of the MCP get_cluster_preset tool (refresh + missing count)'],
  delete_cluster_preset: ['library.clusterPresetsDelete', 'route comment: mirror of the MCP delete_cluster_preset tool'],
  set_library_autoscan: ['library.setAutoScan', 'same FileLibraryManager::setAutoScan'],
  // Sampler (Router_Sampler.cpp): same AudioEngineCommands calls.
  detect_sampler_slices: ['sampler.detectSlices', 'shared entry point: AudioEngineCommands::detectSamplerSlices'],
  set_sampler_key_range: ['sampler.setKeyRange', 'shared entry point: AudioEngineCommands::setSamplerKeyRange'],
  set_sampler_mode: ['sampler.setMode', 'shared entry point: AudioEngineCommands::setSamplerMode'],
  set_sampler_param: ['sampler.setParam', 'shared entry point: setSamplerProperty / setFxSlotParam — the route takes the same property|paramIndex split'],
  trigger_sampler_slice: ['sampler.triggerSlice', 'shared entry point: AudioEngineCommands::triggerSamplerSlice'],
  // MIDI devices (Router_Midi.cpp): same MidiService calls; the routes additionally
  // persist (open) / clear (close) the device key in QSettings.
  open_midi_device: ['midi.openDevice', 'same MidiService::openDevice (+ the route persists the device key)'],
  close_midi_device: ['midi.closeDevice', 'same MidiService::closeDevice (+ the route clears the persisted key)'],
  get_midi_devices: ['midi.getAvailableDevices', 'same MidiService::getAvailableDevices'],
  get_open_midi_device: ['midi.getOpenDevice', 'same MidiService::getOpenDevice'],
  // Export (Router_Export.cpp): same ExportManager; the route drives it synchronously
  // (it waits for onComplete), the tool returns at start and reports by notification.
  export_audio: ['export.audio', 'same ExportManager::startExport + progress notifications; the route waits for completion, the tool returns immediately'],
  cancel_export: ['export.cancel', 'same ExportManager::cancel'],
  // Read views whose per-object route already carries the same data.
  list_notes: ['read.getNotes', 'read.getNotes is the clip\'s whole note list; the tool adds pitches/startGte/startLt/noteIds filters'],
  list_automation_lanes: ['read.getAutomationLanes', 'same ReadModel AutomationLane list'],
  // 2026-09-24 parity wave: the routes these rows name were added by the RPC-parity slice
  // (backend siblings of the ledger edit), which is what drains them from NO_ROUTE. Notes
  // name the shared entry point, because the route is a thin hand-off to it.
  // Measured limit of TEXT parity for the whole group (tests/unit/frontend/
  // missing_route_parity_test.cpp): src/mcp validates a tool's JSON schema BEFORE its
  // handler runs, so a missing/out-of-range REQUIRED argument is rejected there with
  // "invalid params: …" while the route reaches the shared validator and answers its text.
  // Both are -32602 and both refuse; only the wording differs on that class of input.
  automation_preset: ['project.applyAutomationPreset', 'shared entry point: HDAW::automationPresetToolText (src/common/AutomationPresetRequest.h) — the route hands back the SAME compact JSON the tool emits, parsed into the reply'],
  apply_movement_plan: ['project.applyMovementPlan', 'shared entry point: HDAW::applyMovementPlanToolText (src/common/MovementPlanJson.h) — same {okCount, failCount, events[]} payload, parsed into the reply'],
  set_master_fx_param: ['project.setMasterFxParam', 'shared entry point: HDAW::setMasterFxParamToolText (src/common/MasterFxAccess.h) — the route returns the tool text itself as the payload string, clamp report included'],
  set_master_fx_bypassed: ['project.setMasterFxBypassed', 'shared entry point: HDAW::setMasterFxBypassedToolText (src/common/MasterFxAccess.h) — same text-as-payload'],
  place_patterns: ['composition.placePatterns', 'shared entry point: HDAW::parsePlacePatternsRequest + placePatternsJson (src/common/PlacePatternsRequest.h) + AudioEngineCommands::placePatterns; clipId is read on the surface by both'],
  scale_note: ['composition.scaleDegreeToPitch', 'shared entry point: HDAW::resolveScaleNote / scaleNoteJson (src/common/ScaleNote.h); the route name states the primitive, the tool name the degree map'],
  session_get_clip_states: ['session.getClipStates', 'shared payload builder: HDAW::sessionClipStatesJson (src/common/SessionClipStateJson.h)'],

  // ---- 2026-09-24 ledger decisions 1+2 (add_library / fm sysex + preset load) ----
  add_library: ['library.add', 'SAME FileLibraryManager::addLibrary call on both surfaces — the route no longer '
    + 'pre-empts the manager type gate (type="patch" accepted; empty id -> the tool\'s "unknown library type" '
    + '-32602 text). Twin test: FmLibraryParityTest.AddLibraryPatchTypeMatchesOnBothSurfaces'],
  fm_synth_import_sysex: ['audio.fm_synthImportSysex', 'shared entry point: HDAW::fmImportSysexToolText '
    + '(src/common/PresetApply.h, moved from the MCP-only runFmImportSysex) — parse + ProjectCommands::setFmPatch '
    + '(fmPatchData persist + live load) + the tool\'s payload/VMEM acceptance on BOTH surfaces; the route keeps '
    + 'its trackIndex key. Twin test: FmLibraryParityTest.FmImportSysexRoutePersistsAndMatchesToolPayload'],
  fm_synth_load_preset: ['audio.fmSynthLoadPreset', 'shared entry point: HDAW::fmLoadPresetToolText '
    + '(src/common/FmPatchLoad.h) -> ProjectCommands::setFmPatch — raw 312-hex patch over RPC now persists like '
    + 'the tool (the route also accepts the tool\'s trackId spelling). Twin test: '
    + 'FmLibraryParityTest.FmLoadPresetRouteMatchesToolAndPersists'],

  // ---- 2026-09-24 capability wave (decisions 3-9): the seven remaining rows ----
  // All notes name the shared entry point: each route is a thin hand-off to the SAME
  // src/common/ body the MCP tool runs, so the payloads cannot drift.
  apply_preset: ['audio.applyPreset', 'shared entry point: HDAW::applyPresetToolText (src/common/PresetApply.h) — '
    + 'the tool\'s own dispatch-by-slot+file-header composite, whole argument object on both surfaces (the '
    + 'name-derived matrix.applyPreset match was a DIFFERENT family, hence the old FORCE_REVIEW). Twin test: '
    + 'CapabilityRouteParityTest.ApplyPreset*'],
  fm_synth_get_state: ['read.getFmSynthState', 'shared entry point: HDAW::fmSynthStateToolText '
    + '(src/common/FmSynthStateJson.h) — the tool\'s own sources (live activeVoiceCount for the caller-chosen '
    + 'trackId/slotIndex + the tree\'s param_0), NOT read.getFmAnalysis (different sources, documented). '
    + 'Twin test: CapabilityRouteParityTest.FmSynthState*'],
  get_master_fx_params: ['read.getMasterFxParams', 'shared entry point: HDAW::masterFxParamsToolText '
    + '(src/common/MasterFxAccess.h) — reads the MASTER_FX node (the read-side accessor the ledger row asked '
    + 'for), payload == the tool\'s parsed text. Twin test: CapabilityRouteParityTest.MasterFxParams*'],
  list_clip_takes: ['read.getClipTakes', 'shared entry point: HDAW::clipTakesToolText (src/common/ClipTakesJson.h) '
    + '— the tool\'s TAKE_LIST walk ({index,name,sourceFile,active} + activeTake); audioGraph keeps switching, '
    + 'read now lists. Twin test: CapabilityRouteParityTest.ClipTakes*'],
  sub_synth_import_sysex: ['audio.subSynthImportSysex', 'shared entry point: HDAW::subSynthImportSysexToolText '
    + '(src/common/PresetApply.h) -> AudioEngineCommands::loadVirusPatch — the internal sub_synth engine is '
    + 'reached through the command layer (composition.sendFxMidi stays plugin-slots-only, unchanged). '
    + 'Twin test: CapabilityRouteParityTest.SubSynthImportSysex*'],
  load_plugin_preset_file: ['plugin.loadPresetFile', 'shared entry point: HDAW::loadPluginPresetFileToolText '
    + '(src/common/PresetApply.h) — parsePresetFile + setStateInformation + the pluginState tree capture, '
    + 'same message-thread context as the tool (see the Gate 16 note in Router_Plugin.cpp). Twin test: '
    + 'CapabilityRouteParityTest.LoadPresetFile*'],
  audition_patch: ['audio.auditionPatch', 'shared entry point: HDAW::auditionPatchToolText '
    + '(src/common/PresetApply.h) — the tool\'s composite (probe track/clip placement + the shared '
    + 'sub_synth/fm_synth loaders), whole argument object on both surfaces. Twin test: '
    + 'CapabilityRouteParityTest.AuditionPatch*'],
};

// Prefix rules: a tool family that maps onto a namespace, remainder camelCased. Verified
// per family against the router's method list by the ratchet test.
//   [prefix, namespace]         -> ns.<camel(remainder)>
//   [prefix, namespace, verb]   -> ns.<verb><Camel(remainder)> — for families whose tool
//                                  name carries the verb before the namespace word:
//                                  get_audio_buffer_sizes -> audio.getBufferSizes.
const PREFIXES = [
  ['psy_fm_', 'psy_fm'],
  ['rave_', 'rave'],
  ['preview_', 'preview'],
  ['session_', 'session'],
  ['sampler_', 'sampler'],
  // Audio device/driver surface (Router_Audio.cpp): both sides drive AudioDeviceManager
  // and persist the same SettingsKeys entry. All 11 derived routes exist.
  ['get_audio_', 'audio', 'get'],
  ['set_audio_', 'audio', 'set'],
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
  //
  // 2026-09-23 review pass.
  validate_sample: 'MCP-side file probe (ProjectPool format manager: header/stream read) — no engine or '
    + 'project state to route, and library.scan validates files on the library side instead',
  set_midi_fx_param_normalized: 'live real-time write that bypasses the ValueTree BY DESIGN '
    + '(the tool says so); the persistent twin is project.setMidiFxSlotParam',
  list_envelope_shapes: 'static vocabulary of the `shape` argument project.generateAutomationEnvelope / '
    + 'project.generateClipGainEnvelope accept (router_helpers::parseShape — the same 11 names); no route enumerates it',
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

// Tools the name heuristic maps WRONG, or an alias/route that is close but NOT the same
// operation: a name-derived match that is a semantically DIFFERENT capability, or a route
// whose effect differs from the tool's (a subset, a different data source). Emitted as
// unresolved (review queue) with the reason, so the ledger never reads as a verified
// mapping. The live-probe gate cannot catch this class — both routes exist.
const FORCE_REVIEW = {
  // apply_preset + fm_synth_get_state drained by the 2026-09-24 capability wave
  // (audio.applyPreset / read.getFmSynthState — mapped in ALIASES above).
};

// The reverse of an ALIAS: ONE tool whose capability is spread over MANY routes (or one
// route applied N times, with the batch/recipe glue on the MCP side), so no 1:1
// name-derived route can ever exist and the heuristic would park it in the review
// queue forever. The listed target is a REAL route (the ratchet's live dispatch probe
// verifies it); the note is where the 1:many shape is stated, so the row never reads as
// a 1:1 mapping. `set_track` is the MCP partial-update tool for the 13 project.setTrack*
// routes (2026-09-23 parity slice).
const FANOUT = {
  set_track: ['project.setTrackName',
    'fan-out: one tool covers the 13 project.setTrack* routes (Name/Color/Volume/Pan/'
    + 'Muted/Soloed/Hidden/Armed/InputMonitor/Height/MidiChannel/Type/Collapsed) — '
    + 'twin test: AddFxParityTest.SetTrackPropertiesMirrorRouteArgumentNames'],
  // 2026-09-23 review pass — partial update over a route family (same shape as set_track).
  set_note: ['project.setNotePitch',
    'fan-out: partial note update over project.setNotePitch / setNoteStart / setNoteDuration / '
    + 'setNoteVelocity (the tool writes the NOTE_LIST properties directly, no project command per field)'],
  set_clip: ['project.setClipName',
    'fan-out: partial clip update over project.setClipName / setClipStart / setClipDuration / setClipGain / '
    + 'setClipFadeIn / setClipFadeOut / setClipLooping'],
  // Batch/recipe tools: N calls of one route, glue on the MCP side.
  add_notes: ['project.addNote',
    'fan-out: N x project.addNote in one undo unit (batch wrapper; there is no batch route)'],
  remove_notes: ['project.removeNote',
    'fan-out: the pitches/startGte/startLt/noteIds filter is MCP-side, then N x project.removeNote'],
  set_note_velocities: ['project.setNoteVelocity',
    'fan-out: N x project.setNoteVelocity with the absolute/relative/random modes computed MCP-side'],
  set_automation_points: ['project.addAutomationPoint',
    'fan-out: bulk lane write — append = N x project.addAutomationPoint, replace = project.removeAutomationPoint '
    + 'for the lane first, then N x add (plus audioGraph.rebuildAutomationCache)'],
  batch_import_samples: ['project.importAudioFile',
    'fan-out: N x project.importAudioFile in one transaction'],
  loop_clip: ['project.setClipDuration',
    'fan-out: repeats the clip notes (N x project.addNote) then project.setClipDuration extends the clip — '
    + 'the loop math is MCP-side'],
  create_section: ['project.addArrangerRegion',
    'fan-out: project.addArrangerRegion + project.addTrack + project.addAudioClip (the tool passes its own '
    + 'description: "using existing clip-placement commands")'],
  setup_remix: ['project.setTempo',
    'fan-out: project.setTempo + project.setScaleRoot / setScaleMode + project.addArrangerRegion per section'],
  set_scale: ['project.setScaleRoot', 'fan-out: root+mode in one call = project.setScaleRoot + project.setScaleMode'],
  get_scale: ['read.getScaleRoot', 'fan-out: root+mode in one call = read.getScaleRoot + read.getScaleMode'],
  // Read views: covered by the bulk/per-object read routes.
  list_tracks: ['read.snapshot',
    'fan-out: read.snapshot.tracks[] carries the same per-track fields; single-track reads are read.getTrack '
    + 'and the count is read.getTrackCount'],
  list_clips: ['read.snapshot',
    'fan-out: read.snapshot.clips[] carries the same clip objects; the per-clip read is read.getClip'],
  get_project_summary: ['read.snapshot',
    'fan-out: read.snapshot (name/tracks/clips) + read.getTransport (position/isPlaying) — the one-line text '
    + 'format is MCP-side'],
  debug_audio: ['read.snapshot',
    'fan-out: composite read-only debug view = read.snapshot + read.getMasterMeter / read.getTrackMeter / read.getFxSlots'],
  // Analysis helpers whose primitives ARE routed.
  diagnose_intro_blast: ['audio.mixVerdict',
    'fan-out: the blast detection is the audio.mixVerdict introBlast gate (the same shared '
    + 'MixReportAnalyzer::analyzeBlast); the per-bin trace and the per-track attribution render are MCP-side'],
  mix_diff: ['audio.mixReport',
    'fan-out: two runMixReportAnalysis payloads (the shared builder audio.mixReport returns) + the MCP-side delta math'],
  // Preset loaders that funnel into an existing route.
  load_virus_preset: ['composition.sendFxMidi',
    'fan-out: the bank-select + program-change recipe (CC0 + PC) is built MCP-side and queued through '
    + 'composition.sendFxMidi (same ProjectCommands::FxMidiParams, captureToTree included)'],
  load_je8086_preset: ['composition.sendFxMidi',
    'fan-out: the .syx/.mid is parsed and checksum-validated MCP-side (JP-8080 DT1 split), then at most 64 '
    + 'SysEx events go through composition.sendFxMidi'],
};

// Rows with NO route today whose missing route can be named: the sharpened half of the
// review queue. Emitted as unresolved (never mapped) — the ratchet's live probe cannot
// verify a route that does not exist, so the note is where the next pass starts.
// (The 2026-09-24 parity wave drained seven of the previously named routes —
// set_master_fx_param, set_master_fx_bypassed, automation_preset, apply_movement_plan,
// place_patterns, session_get_clip_states, scale_note — now mapped in ALIASES above.)
// Distinct from FORCE_REVIEW (a route exists but is NOT this) and MCP_ONLY (no route by
// design, with the covering route named).
const NO_ROUTE = {
  // 2026-09-24 capability wave: get_master_fx_params, sub_synth_import_sysex,
  // load_plugin_preset_file, audition_patch and list_clip_takes all gained their named
  // routes — mapped in ALIASES above/below.
};

const rows = [];
for (const { tool } of tools) {
  if (FORCE_REVIEW[tool]) { rows.push([tool, 'unresolved', '-', FORCE_REVIEW[tool]]); continue; }
  if (NO_ROUTE[tool]) { rows.push([tool, 'unresolved', '-', NO_ROUTE[tool]]); continue; }
  const alias = ALIASES[tool];
  if (alias) {
    const route = Array.isArray(alias) ? alias[0] : alias;
    const note = Array.isArray(alias) ? alias[1] : 'alias (verified; see the ledger notes)';
    if (rpc.has(route)) { rows.push([tool, 'mapped', route, note]); continue; }
  }
  if (FANOUT[tool] && rpc.has(FANOUT[tool][0])) { rows.push([tool, 'mapped', FANOUT[tool][0], FANOUT[tool][1]]); continue; }
  const c = camel(tool);
  if (methodToNs.has(c)) {
    const ns = methodToNs.get(c)[0];
    if (ns !== '?') { rows.push([tool, 'mapped', ns + '.' + c, 'exact camelCase match (name-derived)']); continue; }
  }
  let hit = null;
  for (const [pre, ns, verb] of PREFIXES) {
    if (tool.startsWith(pre)) {
      const rest = camel(tool.slice(pre.length));
      const r = ns + '.' + (verb ? verb + rest.charAt(0).toUpperCase() + rest.slice(1) : rest);
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
