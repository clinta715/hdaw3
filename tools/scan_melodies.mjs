#!/usr/bin/env node
// scan_melodies.mjs — Phase 0: corpus MELODY availability scan.
// Parses .mid files (reuses analyze_drum_midis.mjs SMF parser), detects
// scale+root over pitch classes, and reports per-pack how many files are
// USABLE MELODIC content (a detectable key, enough contour, not a pure
// block) plus top keys and median contour length. Gates Phase 1 of
// docs/plans/2026-09-08-corpus-melody-pipeline.md — do not build an
// extractor before knowing the data is usable.
//
// Usage: node tools/scan_melodies.mjs [dir...]
//        no dirs -> scans E:\\midi grouped by top-level directory

import fs from 'node:fs';
import path from 'node:path';
import { parseMidi } from './analyze_drum_midis.mjs';

// ── Scale library (interval patterns from root) ──
const SCALES = [
  ['major',      [0,2,4,5,7,9,11]],
  ['naturalMinor',[0,2,3,5,7,8,10]],
  ['harmonicMinor',[0,2,3,5,7,8,11]],
  ['melodicMinor',[0,2,3,5,7,9,11]],
  ['dorian',     [0,2,3,5,7,9,10]],
  ['phrygian',   [0,1,3,5,7,8,10]],
  ['lydian',     [0,2,4,6,7,9,11]],
  ['mixolydian', [0,2,4,5,7,9,10]],
  ['locrian',    [0,1,3,5,6,8,10]],
  ['majorPent',  [0,2,4,7,9]],
  ['minorPent',  [0,3,5,7,10]],
  ['bluesMinor', [0,3,5,6,7,10]],
];
const NOTE_NAMES = ['C','C#','D','D#','E','F','F#','G','G#','A','A#','B'];

// Best (root, scale) fit over a file's pitch classes; returns
// { root, scale, fit, chromaticism, keyName }.
function detectKey(pcs) {
  const total = pcs.size;
  if (total === 0) return null;
  let best = null;
  for (const [sname, intervals] of SCALES) {
    for (let r = 0; r < 12; r++) {
      const set = new Set(intervals.map((d) => (r + d) % 12));
      let match = 0;
      for (const pc of pcs) if (set.has(pc)) match++;
      const fit = match / total;
      if (!best || fit > best.fit ||
          (fit === best.fit && match > best.match)) {
        best = { root: r, scale: sname, fit, match, chromaticism: 1 - fit };
      }
    }
  }
  const modeMaj = ['major','lydian','mixolydian','majorPent'];
  best.keyName = NOTE_NAMES[best.root] + (modeMaj.includes(best.scale) ? ' ' + best.scale : ' minor-ish ' + best.scale);
  return best;
}

function analyze(filePath) {
  const parsed = parseMidi(fs.readFileSync(filePath));
  if (parsed.error) return { file: path.basename(filePath), error: parsed.error };
  const notes = parsed.notes;
  if (!notes || notes.length < 8) return { file: path.basename(filePath), error: 'too few notes', n: notes.length };
  const pcs = new Set();
  const pitches = new Set();
  const onsetCount = new Map(); // startTick -> count (polyphony at onset)
  let totalDur = 0;
  let minP = 128, maxP = 0;
  for (const n of notes) {
    pcs.add(n.pitch % 12);
    pitches.add(n.pitch);
    onsetCount.set(n.tick, (onsetCount.get(n.tick) ?? 0) + 1);
    totalDur += Math.max(0, n.dur);
    if (n.pitch < minP) minP = n.pitch;
    if (n.pitch > maxP) maxP = n.pitch;
  }
  const onsets = [...onsetCount.values()];
  const avgPoly = onsets.reduce((a, b) => a + b, 0) / Math.max(1, onsets.length);
  const key = detectKey(pcs);
  // Usable melody: a detectable key, enough pitch contour, and a low-ish
  // polyphony (a lead/arp line) OR at least not a dense block with no line.
  const usable =
    key && key.fit >= 0.7 &&
    pitches.size >= 4 &&
    avgPoly <= 3.0 &&
    notes.length >= 8;
  return {
    file: path.basename(filePath),
    n: notes.length,
    key: key ? key.keyName : null,
    chrom: key ? +(key.chromaticism.toFixed(2)) : 1,
    contour: pitches.size,
    poly: +avgPoly.toFixed(1),
    range: maxP - minP,
    usable: !!usable,
  };
}

// ── CLI ──
function collectMidis(dir) {
  const out = [];
  for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
    const p = path.join(dir, e.name);
    if (e.isDirectory()) out.push(...collectMidis(p));
    else if (e.name.toLowerCase().endsWith('.mid') || e.name.toLowerCase().endsWith('.midi')) out.push(p);
  }
  return out;
}

const roots = process.argv.slice(2).length ? process.argv.slice(2) : ['E:\\midi'];
for (const root of roots) {
  if (!fs.existsSync(root)) { console.log('missing: ' + root); continue; }
  const files = collectMidis(root);
  // group by top-level dir under E:\midi (or the root itself)
  const byPack = new Map();
  for (const f of files) {
    const parts = f.split(path.sep);
    const topIdx = parts.indexOf('midi');
    const pack = topIdx >= 0 && parts.length > topIdx + 1 ? parts[topIdx + 1] : path.basename(path.dirname(f));
    const a = analyze(f);
    if (!byPack.has(pack)) byPack.set(pack, { files: 0, usable: 0, keys: new Map(), contours: [], poly: [] });
    const p = byPack.get(pack);
    p.files++;
    if (a.usable) {
      p.usable++;
      p.keys.set(a.key, (p.keys.get(a.key) ?? 0) + 1);
      p.contours.push(a.contour);
      p.poly.push(a.poly);
    }
  }
  const rows = [...byPack.entries()]
    .filter(([, p]) => p.files > 0)
    .map(([name, p]) => {
      const topKeys = [...p.keys.entries()].sort((a, b) => b[1] - a[1]).slice(0, 3).map(([k, c]) => k + 'x' + c).join(', ');
      const med = (arr) => { if (!arr.length) return 0; const s = [...arr].sort((a, b) => a - b); return s[Math.floor(s.length / 2)]; };
      const ratio = p.files ? (p.usable / p.files) : 0;
      return { pack: name, files: p.files, usable: p.usable, ratio: +ratio.toFixed(2), medContour: med(p.contours), medPoly: med(p.poly), topKeys };
    })
    .sort((a, b) => b.usable - a.usable);
  console.log('\n=== ' + root + '  (' + files.length + ' files) ===');
  console.log('pack\tfiles\tusable\tratio\tmedContour\tmedPoly\ttopKeys');
  for (const r of rows) console.log([r.pack, r.files, r.usable, r.ratio, r.medContour, r.medPoly, r.topKeys].join('\t'));
  const totUsable = rows.reduce((a, r) => a + r.usable, 0);
  console.log('\nTOTAL usable melodic files: ' + totUsable);
}