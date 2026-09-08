#!/usr/bin/env node
// extract_role_bank.mjs — corpus-level drum pattern cell extraction.
// Scans a directory of MIDI loops, tags each file's role from its filename
// (single-instrument pack convention: instrument = name, hits share one pitch),
// quantizes to a 16-step grid, and merges distinct per-bar cells ACROSS files
// per role, so a role's pattern bank = { unique cells with usage weight }.
//
// Usage: node tools/extract_role_bank.mjs <dir> [--json out.json] [--top n] [--min-count m]

import fs from 'node:fs';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
import { parseMidi, roleOf, ROLE_PITCHES } from './analyze_drum_midis.mjs';

// role hint from filename (matches pack labelling); null = rely on pitch
function roleFromName(name) {
  const n = name.toLowerCase();
  const has = (re) => re.test(n);
  if (has(/open.?hat|open/i)) return 'openHat';
  if (has(/hh|hi.?hat|hats?/i) && !has(/crash/)) return 'hats';
  if (has(/clap/)) return 'clap';
  if (has(/snare/)) return 'snare';
  if (has(/kick|808/i) && !has(/bassline|melod/i)) return 'kick';
  if (has(/tom/i)) return 'tom';
  if (has(/crash/)) return 'crash';
  if (has(/ride/)) return 'ride';
  if (has(/perc/i)) return 'perc';
  return null;
}

function quantize(parsed) {
  const { tpqn, notes } = parsed;
  if (!tpqn || notes.length === 0) return null;
  const denVal = Math.pow(2, parsed.timeSig.den);
  const quartersPerBar = parsed.timeSig.num * (4 / denVal);
  const stepsPerBar = Math.round(quartersPerBar * 4);
  if (stepsPerBar < 8 || stepsPerBar > 64) return { skip: 'sig ' + parsed.timeSig.num + '/' + denVal };
  const nb = notes.map((n) => ({ start: n.tick / tpqn, vel: n.vel, pitch: n.pitch })).sort((a, b) => a.start - b.start);
  const totalBeats = nb.reduce((m, n) => Math.max(m, n.start + 0.05), 0);
  const nBars = Math.min(32, Math.max(1, Math.floor(totalBeats / quartersPerBar)));
  const bars = [];
  for (let i = 0; i < nBars; i++) {
    const s0 = i * quartersPerBar;
    const map = new Map();
    for (const n of nb) {
      if (n.start < s0 || n.start >= s0 + quartersPerBar) continue;
      let step = Math.round((n.start - s0) * 4);
      if (step >= stepsPerBar) step = stepsPerBar - 1;
      const arr = map.get('hits') ?? [];
      arr.push({ step, vel: n.vel, pitch: n.pitch });
      map.set('hits', arr);
    }
    if (map.size) bars.push(map);
  }
  return { stepsPerBar, bars, melodic: nb.filter((n) => roleOf(n.pitch) === null).length / Math.max(1, nb.length), pitchSet: [...new Set(nb.map((n) => n.pitch))].sort((a, b) => a - b) };
}

const isMain = process.argv[1] && pathToFileURL(path.resolve(process.argv[1])).href === import.meta.url;

export { roleFromName };

if (isMain) {
  const args = process.argv.slice(2);
  const val = (n) => { const i = args.indexOf(n); return i >= 0 ? args[i + 1] : null; };
  const jsonOut = val('--json');
  const topN = val('--top') ? parseInt(val('--top'), 10) : 6;
  const minCount = val('--min-count') ? parseInt(val('--min-count'), 10) : 1;
  const dir = args[0];
  if (!dir) { console.log('usage: node tools/extract_role_bank.mjs <dir> [--json out.json] [--top n]'); process.exit(1); }

  const cells = new Map();   // role + '|' + stepsJSON -> { role, steps, vel, sources: Map(file -> count), files }
  const fileMeta = [];
  const files = [];
  const walk = (d) => { for (const e of fs.readdirSync(d, { withFileTypes: true })) { const p = path.join(d, e.name); if (e.isDirectory()) walk(p); else if (/\.mid$/i.test(e.name)) files.push(p); } };
  walk(dir);
  files.sort();
  const stepAcc = (map, role, hitArr, stepsPerBar, file, velAcc) => {
    const byStep = new Map();
    for (const h of hitArr) byStep.set(h.step, (byStep.get(h.step) ?? 0) + h.vel);
    const steps = [...byStep.keys()].sort((a, b) => a - b);
    const key = role + '|' + JSON.stringify(steps);
    const avgVel = [...byStep.values()].reduce((a, b) => a + b, 0) / steps.length;
    const cell = cells.get(key);
    if (cell) {
      cell.sources.set(file, (cell.sources.get(file) ?? 0) + 1);
      velAcc.set(key, avgVel);
    } else {
      cells.set(key, { role, steps, sources: new Map([[file, 1]]), barLen: stepsPerBar });
    }
  };

  for (const f of files) {
    const name = path.basename(f);
    const parsed = parseMidi(fs.readFileSync(f));
    if (parsed.error) { console.log('  [err] ' + name + ' — ' + parsed.error); continue; }
    const q = quantize(parsed);
    if (!q || q.skip) { console.log('  [skip] ' + name + (q && q.skip ? ' — ' + q.skip : '')); continue; }
    const hint = roleFromName(name);
    if (q.melodic > 0.8 && !hint) { console.log('  [melodic] ' + name + ' (' + q.pitchSet.join(',') + ')'); continue; }
    let barCount = 0;
    for (const bar of q.bars) {
      const hitArr = bar.get('hits');
      if (!hitArr) continue;
      barCount++;
      const roles = hint ? [hint] : [...new Set(hitArr.map((h) => roleOf(h.pitch))).values()].filter(Boolean);
      if (roles.length === 0) continue;
      for (const role of roles) stepAcc(bar, role, hitArr.filter((h) => roleOf(h.pitch) === role || hint), q.stepsPerBar, name, new Map());
    }
    fileMeta.push({ file: name, bars: barCount, hint, melodic: q.melodic, pitches: q.pitchSet.join(',') });
    console.log('  ' + name + (hint ? ' [' + hint + ']' : '') + ' | bars:' + barCount + ' | p:' + q.pitchSet.join(','));
  }

  console.log('\n==== CELL BANK (' + files.length + ' files) ====');
  const byRole = new Map();
  for (const [k, cell] of cells) {
    const total = [...cell.sources.values()].reduce((a, b) => a + b, 0);
    cell.total = total;
    cell.files = cell.sources.size;
    if (!byRole.has(cell.role)) byRole.set(cell.role, []);
    byRole.get(cell.role).push(cell);
  }
  const order = ['kick', 'snare', 'clap', 'hats', 'openHat', 'perc', 'tom', 'crash', 'ride'];
  for (const role of order) {
    if (!byRole.has(role)) continue;
    const list = byRole.get(role).sort((a, b) => b.total - a.total);
    console.log('\n-- ' + role + ': ' + list.length + ' distinct cells --');
    for (const cell of list.slice(0, topN)) {
      const s = new Array(cell.barLen).fill('.');
      for (const st of cell.steps) s[st] = 'X';
      console.log('   x' + cell.total + ' (in ' + cell.files + ' file' + (cell.files > 1 ? 's' : '') + ')  ' + s.join('') + '  from: ' + [...cell.sources.keys()].join(', '));
    }
  }

  if (jsonOut) {
    const dump = { generatedAt: new Date().toISOString(), dir, files: fileMeta,
      cells: [...cells.values()].map((c) => ({ role: c.role, steps: c.steps, barLen: c.barLen, total: c.total,
        files: c.files, sources: [...c.sources.entries()].map(([f, n]) => f + 'x' + n) })) };
    fs.writeFileSync(jsonOut, JSON.stringify(dump, null, 1));
    console.log('\nJSON → ' + jsonOut + ' (' + cells.size + ' cells)');
  }
}
