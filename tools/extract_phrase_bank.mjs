#!/usr/bin/env node
// extract_phrase_bank.mjs — multi-bar drum phrase extraction.
// For each MIDI file: quantize to a 16th grid, detect the smallest repeating
// phrase period (1/2/4/8 bars) by autocorrelation, extract the phrase as
// {bars x stepsPerBar} hit rows, tag the role from filename/dir, and merge
// identical phrases ACROSS files into a weighted pattern bank.
//
// Usage: node tools/extract_phrase_bank.mjs <dir...> [--json out.json] [--top n]

import fs from 'node:fs';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
import { parseMidi } from './analyze_drum_midis.mjs';
import { roleFromName } from './extract_role_bank.mjs';

function quantizeBars(parsed) {
  const { tpqn, notes } = parsed;
  if (!tpqn || notes.length === 0) return null;
  const denVal = Math.pow(2, parsed.timeSig.den);
  const qpb = parsed.timeSig.num * (4 / denVal);       // quarters per bar
  const spb = Math.round(qpb * 4);                     // 16th grid
  if (spb < 8 || spb > 64) return { skip: 'sig ' + parsed.timeSig.num + '/' + denVal };
  const nb = notes.map((n) => ({ start: n.tick / tpqn })).sort((a, b) => a.start - b.start);
  const total = nb.reduce((m, n) => Math.max(m, n.start + 0.05), 0);
  const nBars = Math.max(1, Math.ceil(total / qpb));
  // bar -> sorted hit steps (16th grid within bar)
  const rows = [];
  for (let b = 0; b < nBars; b++) {
    const s0 = b * qpb, s1 = s0 + qpb;
    const steps = [];
    for (const n of nb) {
      if (n.start < s0 || n.start >= s1) continue;
      let st = Math.round((n.start - s0) * 4);
      if (st >= spb) st = spb - 1;
      if (!steps.includes(st)) steps.push(st);
    }
    rows.push(steps.sort((a, b) => a - b));
  }
  return { spb, nBars, rows, bpm: 60_000_000 / parsed.tempoUS };
}

// smallest period P in {1,2,4,8} with all bars matching P bars earlier (perfect repeat)
function detectPeriod(rows) {
  const n = rows.length;
  const eq = (a, b) => JSON.stringify(a) === JSON.stringify(b);
  for (const P of [1, 2, 4, 8]) {
    if (P >= n) continue;
    let ok = true;
    for (let i = 0; i + P < n; i++) if (!eq(rows[i], rows[i + P])) { ok = false; break; }
    if (ok) return P;
  }
  return n; // no perfect repeat -> whole file is the phrase
}

const isMain = process.argv[1] && pathToFileURL(path.resolve(process.argv[1])).href === import.meta.url;

if (isMain) {
  const args = process.argv.slice(2);
  const val = (nm) => { const i = args.indexOf(nm); return i >= 0 ? args[i + 1] : null; };
  const jsonOut = val('--json');
  const topN = val('--top') ? parseInt(val('--top'), 10) : 5;
  const dirs = args.filter((a, i) => args[i - 1] !== '--json' && args[i - 1] !== '--top' && a !== '--json' && a !== '--top');

  const phrases = new Map();   // role|JSON -> { role, bars, rows, sources: Map, bpms:Set, weights }
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
      const q = quantizeBars(parsed);
      if (!q || q.skip) { console.log('  [skip] ' + name + (q && q.skip ? ' — ' + q.skip : '')); continue; }
      const role = roleFromName(name);
      if (!role) { console.log('  [?role] ' + name); continue; }
      const P = detectPeriod(q.rows);
      const phraseRows = q.rows.slice(0, P);
      // trim trailing empty bars so a 2-bar phrase that's really "A,empty" stays honest
      while (phraseRows.length > 1 && phraseRows[phraseRows.length - 1].length === 0) phraseRows.pop();
      const key = role + '|' + P + '|' + JSON.stringify(phraseRows);
      const hit = phrases.get(key);
      if (hit) {
        hit.weights.set(name, (hit.weights.get(name) ?? 0) + 1);
        hit.bpms.add(q.bpm);
      } else {
        phrases.set(key, { role, bars: phraseRows.length, rows: phraseRows, weights: new Map([[name, 1]]), bpms: new Set([q.bpm]) });
      }
      filesSeen.push({ file: name, role, period: P, bars: phraseRows.length, totalBars: q.nBars, bpm: q.bpm });
      console.log('  ' + name.padEnd(30) + '[' + role + '] period ' + P + ' -> ' + phraseRows.length + ' bars');
    }
  }

  // renderer
  const renderRow = (steps, spb) => { const s = new Array(spb).fill('.'); for (const st of steps) s[st] = 'X'; return s.join(''); };

  console.log('\n==== PHRASE BANK ====');
  const byRole = new Map();
  for (const [k, ph] of phrases) {
    ph.total = [...ph.weights.values()].reduce((a, b) => a + b, 0);
    ph.nFiles = ph.weights.size;
    if (!byRole.has(ph.role)) byRole.set(ph.role, []);
    byRole.get(ph.role).push(ph);
  }
  const order = ['kick', 'snare', 'clap', 'hats', 'openHat', 'perc', 'tom', 'crash', 'ride'];
  for (const role of order) {
    if (!byRole.has(role)) continue;
    const list = byRole.get(role).sort((a, b) => b.total - a.total);
    console.log('\n-- ' + role + ': ' + list.length + ' distinct phrases --');
    for (const ph of list.slice(0, topN)) {
      const bpmNote = ph.bpms.size === 1 ? Math.round([...ph.bpms][0]) + 'bpm' : 'mixed';
      console.log('  x' + ph.total + ' (in ' + ph.nFiles + ' file' + (ph.nFiles > 1 ? 's' : '') + ') ' + ph.bars + ' bar, ' + bpmNote);
      ph.rows.forEach((row, bi) => console.log('    b' + bi + ': ' + renderRow(row, 16)));
      console.log('    from: ' + [...ph.weights.keys()].join(', '));
    }
  }

  if (jsonOut) {
    const dump = { generatedAt: new Date().toISOString(), dirs, files: filesSeen,
      phrases: [...phrases.values()].map((ph) => ({
        role: ph.role, bars: ph.bars, grid: ph.rows, stepsPerBar: 16,
        total: ph.total, nFiles: ph.nFiles,
        sources: [...ph.weights.entries()].map(([f, n]) => f + 'x' + n),
        bpm: [...ph.bpms].sort((a, b) => a - b),
      })) };
    fs.writeFileSync(jsonOut, JSON.stringify(dump, null, 1));
    console.log('\nJSON bank → ' + jsonOut + ' (' + phrases.size + ' phrases)');
  }
}
