#!/usr/bin/env node
// extract_kit_phrases.mjs — role-from-PITCH multi-bar phrase extraction for
// FULL-KIT MIDI packs (multi-instrument per file). Each note's role is
// classified by GM percussion pitch, each bar is decomposed into per-role
// 16-step vectors, the phrase period is detected on the combined groove, and
// per-role phrases are deduped ACROSS files. Output JSON matches the shape
// curate_bank.mjs expects (role, bars, grid=rows, total, nFiles, sources, bpm).
//
// Usage: node tools/extract_kit_phrases.mjs <dir...> [--json out.json] [--top n]

import fs from 'node:fs';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
import { parseMidi } from './analyze_drum_midis.mjs';

const PITCH_ROLE = {
  35: 'kick', 36: 'kick', 38: 'snare', 40: 'snare', 37: 'snare', 39: 'clap',
  42: 'hats', 44: 'hats', 46: 'hats', 49: 'crash', 52: 'crash', 57: 'crash',
  51: 'ride', 59: 'ride', 53: 'ride',
  41: 'tom', 43: 'tom', 45: 'tom', 47: 'tom', 48: 'tom', 50: 'tom',
};
function roleOfPitch(p) { return (p >= 27 && p <= 87) ? (PITCH_ROLE[p] ?? 'perc') : null; }

function quantize(parsed) {
  const { tpqn, notes } = parsed;
  if (!tpqn || notes.length === 0) return null;
  const denVal = Math.pow(2, parsed.timeSig.den);
  const qpb = parsed.timeSig.num * (4 / denVal);
  const spb = Math.round(qpb * 4);
  if (spb < 8 || spb > 64) return { skip: 'sig ' + parsed.timeSig.num + '/' + denVal };
  const nb = notes.map((n) => ({ start: n.tick / tpqn, pitch: n.pitch })).sort((a, b) => a.start - b.start);
  const total = nb.reduce((m, n) => Math.max(m, n.start + 0.05), 0);
  const nBars = Math.max(1, Math.ceil(total / qpb));
  const bars = []; // bars[b] = Map role -> sorted step array
  for (let b = 0; b < nBars; b++) {
    const s0 = b * qpb, s1 = s0 + qpb;
    const m = new Map();
    for (const n of nb) {
      if (n.start < s0 || n.start >= s1) continue;
      const role = roleOfPitch(n.pitch);
      if (!role) continue;
      let st = Math.round((n.start - s0) * 4);
      if (st >= spb) st = spb - 1;
      if (!m.has(role)) m.set(role, []);
      const arr = m.get(role);
      if (!arr.includes(st)) arr.push(st);
    }
    for (const [, a] of m) a.sort((x, y) => x - y);
    bars.push(m);
  }
  return { spb, nBars, bars, bpm: 60_000_000 / parsed.tempoUS };
}

const barSig = (m) => { const o = {}; for (const [r, a] of m) o[r] = a; return JSON.stringify(o); };
function detectPeriod(bars) {
  const n = bars.length, sig = bars.map(barSig);
  for (const P of [1, 2, 4, 8]) {
    if (P >= n) continue;
    let ok = true;
    for (let i = 0; i + P < n; i++) if (sig[i] !== sig[i + P]) { ok = false; break; }
    if (ok) return P;
  }
  return n;
}

const isMain = process.argv[1] && pathToFileURL(path.resolve(process.argv[1])).href === import.meta.url;
export { roleOfPitch };

if (isMain) {
  const args = process.argv.slice(2);
  const val = (n) => { const i = args.indexOf(n); return i >= 0 ? args[i + 1] : null; };
  const jsonOut = val('--json');
  const topN = val('--top') ? parseInt(val('--top'), 10) : 4;
  const dirs = args.filter((a, i) => args[i - 1] !== '--json' && args[i - 1] !== '--top' && a !== '--json' && a !== '--top');

  const phrases = new Map();
  const filesSeen = [];
  for (const dir of dirs) {
    if (!fs.existsSync(dir)) { console.log('SKIP: ' + dir); continue; }
    const files = [];
    const walk = (d) => { for (const e of fs.readdirSync(d, { withFileTypes: true })) { const p = path.join(d, e.name); if (e.isDirectory()) walk(p); else if (/\.mid$/i.test(e.name)) files.push(p); } };
    walk(dir);
    files.sort();
    console.log('==== ' + dir + ' (' + files.length + ') ====');
    for (const f of files) {
      const name = path.basename(f);
      let parsed;
      try { parsed = parseMidi(fs.readFileSync(f)); } catch (e) { console.log('  [err] ' + name + ' — ' + e.message); continue; }
      if (parsed.error) { console.log('  [err] ' + name + ' — ' + parsed.error); continue; }
      const q = quantize(parsed);
      if (!q || q.skip) { console.log('  [skip] ' + name + (q && q.skip ? ' — ' + q.skip : '')); continue; }
      const P = detectPeriod(q.bars);
      const roleRows = {};
      for (let b = 0; b < P; b++) for (const [role, steps] of q.bars[b]) (roleRows[role] ??= {})[b] = steps;
      const roles = Object.keys(roleRows);
      for (const role of roles) {
        const rows = [];
        for (let b = 0; b < P; b++) rows.push(roleRows[role][b] ?? []);
        while (rows.length > 1 && rows[rows.length - 1].length === 0) rows.pop();
        const key = role + '|' + P + '|' + JSON.stringify(rows);
        const hit = phrases.get(key);
        if (hit) { hit.sources.set(name, (hit.sources.get(name) ?? 0) + 1); hit.bpms.add(q.bpm); }
        else phrases.set(key, { role, bars: rows.length, rows, sources: new Map([[name, 1]]), bpms: new Set([q.bpm]) });
      }
      filesSeen.push({ file: name, period: P, roles });
      console.log('  ' + name.padEnd(38) + ' period ' + P + ' | ' + roles.join(','));
    }
  }

  console.log('\n==== KIT PHRASE BANK ====');
  const byRole = new Map();
  for (const [k, ph] of phrases) {
    ph.total = [...ph.sources.values()].reduce((a, b) => a + b, 0);
    ph.nFiles = ph.sources.size;
    if (!byRole.has(ph.role)) byRole.set(ph.role, []);
    byRole.get(ph.role).push(ph);
  }
  const renderRow = (steps, spb) => { const s = new Array(spb).fill('.'); for (const st of steps) s[st] = 'X'; return s.join(''); };
  const order = ['kick', 'snare', 'clap', 'hats', 'openHat', 'perc', 'tom', 'crash', 'ride'];
  for (const role of order) {
    if (!byRole.has(role)) continue;
    const list = byRole.get(role).sort((a, b) => b.total - a.total);
    console.log('\n-- ' + role + ': ' + list.length + ' distinct --');
    for (const ph of list.slice(0, topN)) {
      console.log('  x' + ph.total + ' (in ' + ph.nFiles + ') ' + ph.bars + ' bar, ' + (ph.bpms.size === 1 ? Math.round([...ph.bpms][0]) + 'bpm' : 'mixed'));
      ph.rows.forEach((row, bi) => console.log('    b' + bi + ': ' + renderRow(row, 16)));
    }
  }

  if (jsonOut) {
    const dump = { generatedAt: new Date().toISOString(), dirs, files: filesSeen,
      phrases: [...phrases.values()].map((ph) => ({
        role: ph.role, bars: ph.bars, grid: ph.rows, stepsPerBar: 16,
        total: ph.total, nFiles: ph.nFiles,
        sources: [...ph.sources.entries()].map(([f, n]) => f + 'x' + n),
        bpm: [...ph.bpms].sort((a, b) => a - b),
      })) };
    fs.writeFileSync(jsonOut, JSON.stringify(dump, null, 1));
    console.log('\nJSON → ' + jsonOut + ' (' + phrases.size + ' phrases)');
  }
}
