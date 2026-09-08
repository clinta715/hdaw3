#!/usr/bin/env node
// analyze_drum_midis.mjs — standalone Standard-MIDI-File (SMF) drum corpus analyzer.
// Parses .mid files directly (no JUCE/engine needed), classifies hits by GM drum
// role, quantizes onsets to a 16th grid, extracts unique per-bar patterns per
// role, and writes a JSON corpus for review / PatternLibrary curation.
//
// Usage: node tools/analyze_drum_midis.mjs [dir...] [--json out.json] [--top n] [--pitches]
//        no dirs -> scans the probe set (E:\\midi\\[1] Drum MIDIs)

import fs from 'node:fs';
import path from 'node:path';
import { pathToFileURL } from 'node:url';

// ── GM drum role mapping (General MIDI percussion 27..87) ──
const ROLE_PITCHES = {
  kick:      [36, 35],
  snare:     [40, 38, 37],   // electric/acoustic snare + side stick
  clap:      [39],
  closedHat: [42, 44],       // closed + pedal hat
  openHat:   [46],
  crash:     [49, 52, 57],
  ride:      [51, 59, 53],
  tom:       [41, 43, 45, 47, 48, 50],
};
const ROLE_LOOKUP = new Map();
for (const [role, pitches] of Object.entries(ROLE_PITCHES))
  for (const p of pitches) ROLE_LOOKUP.set(p, role);
const roleOf = (p) => (p >= 27 && p <= 87) ? (ROLE_LOOKUP.get(p) ?? 'perc') : null;

// ── SMF binary parsing ──
const readU16 = (b, o) => (b[o] << 8) | b[o + 1];
const readU32 = (b, o) => (b[o] * 0x1000000) + (b[o + 1] << 16) + (b[o + 2] << 8) + b[o + 3];

function readVarLenFrom(bytes, c) {
  let v = 0, b;
  do { b = bytes[c.p++]; v = (v << 7) | (b & 0x7f); } while (b & 0x80);
  return v;
}

function parseTrack(bytes, start, end, out) {
  const open = new Map();
  const notes = [];
  const c = { p: start };
  let tick = 0, running = 0;
  let lastTick = 0;
  while (c.p < end) {
    tick += readVarLenFrom(bytes, c);
    lastTick = tick;
    let status = bytes[c.p];
    if (status < 0x80) { status = running; } else { running = status; c.p++; }
    const kind = status & 0xf0;
    if (kind === 0x80 || kind === 0x90) {
      const pitch = bytes[c.p], vel = bytes[c.p + 1]; c.p += 2;
      const key = (status & 0x0f) + ':' + pitch;
      if (kind === 0x90 && vel > 0) {
        const prev = open.get(key);
        if (prev) { open.delete(key); notes.push({ tick: prev.tick, dur: tick - prev.tick, pitch, vel: prev.vel }); }
        open.set(key, { tick, vel });
      } else {
        const prev = open.get(key);
        if (prev) { open.delete(key); notes.push({ tick: prev.tick, dur: tick - prev.tick, pitch, vel: prev.vel }); }
      }
    } else if (kind === 0xa0 || kind === 0xb0 || kind === 0xe0) {
      c.p += 2;
    } else if (kind === 0xc0 || kind === 0xd0) {
      c.p += 1;
    } else if (status === 0xff) {
      const type = bytes[c.p++];
      const len = readVarLenFrom(bytes, c);
      const bodyStart = c.p;
      c.p += len;
      if (type === 0x51 && len >= 3) out.tempoUS = (bytes[bodyStart] << 16) | (bytes[bodyStart + 1] << 8) | bytes[bodyStart + 2];
      else if (type === 0x58 && len >= 4) out.timeSig = { num: bytes[bodyStart], den: bytes[bodyStart + 1] };
      else if (type === 0x2f) break;
    } else if (status === 0xf0 || status === 0xf7) {
      const len = readVarLenFrom(bytes, c);
      c.p += len;
    } else if (status >= 0xf1 && status <= 0xf6) {
      c.p += (status === 0xf2) ? 2 : 1;
    }
  }
  for (const [key, prev] of open) {
    const pitch = Number(key.split(':')[1]);
    notes.push({ tick: prev.tick, dur: Math.max(1, lastTick - prev.tick), pitch, vel: prev.vel });
  }
  return notes;
}

function parseMidi(bytes) {
  if (bytes.length < 14 || bytes.toString('latin1', 0, 4) !== 'MThd') return { error: 'not an SMF' };
  const format = readU16(bytes, 8);
  const ntrks = readU16(bytes, 10);
  const rawDiv = readU16(bytes, 12);
  if ((rawDiv & 0x8000) !== 0) return { error: 'SMPTE division (unsupported)' };
  const out = { format, tpqn: rawDiv, tempoUS: 500000, timeSig: { num: 4, den: 2 }, notes: [], error: null };
  let off = 14;
  for (let t = 0; t < ntrks && off + 8 <= bytes.length; t++) {
    if (bytes.toString('latin1', off, off + 4) !== 'MTrk') break;
    const len = readU32(bytes, off + 4);
    const end = Math.min(off + 8 + len, bytes.length);
    out.notes.push(...parseTrack(bytes, off + 8, end, out));
    off = end;
  }
  if (out.notes.length === 0) out.error = 'no notes';
  return out;
}

// ── Quantization to a 16th-step grid, sliced per bar ──
function quantize(parsed) {
  const { tpqn, notes } = parsed;
  if (!tpqn || notes.length === 0) return null;
  const denVal = Math.pow(2, parsed.timeSig.den);
  const quartersPerBar = parsed.timeSig.num * (4 / denVal);
  const stepsPerBar = Math.round(quartersPerBar * 4);
  if (stepsPerBar < 8 || stepsPerBar > 64) return { skip: 'odd sig ' + parsed.timeSig.num + '/' + denVal };
  const beatOf = (tick) => tick / tpqn;
  const nb = notes.map((n) => ({ start: beatOf(n.tick), dur: Math.max(0.05, n.dur / tpqn), pitch: n.pitch, vel: n.vel }))
    .sort((a, b) => a.start - b.start);
  const totalBeats = nb.reduce((m, n) => Math.max(m, n.start + n.dur), 0);
  const nBars = Math.min(64, Math.max(1, Math.floor(totalBeats / quartersPerBar)));
  const bars = [];
  for (let i = 0; i < nBars; i++) {
    const barStart = i * quartersPerBar;
    const map = new Map();
    for (const n of nb) {
      if (n.start < barStart || n.start >= barStart + quartersPerBar) continue;
      const role = roleOf(n.pitch);
      if (!role) continue;
      let step = Math.round((n.start - barStart) * 4);
      if (step >= stepsPerBar) step = stepsPerBar - 1;
      let arr = map.get(role);
      if (!arr) { arr = []; map.set(role, arr); }
      arr.push({ step, vel: n.vel });
    }
    bars.push(map);
  }
  return {
    bpm: 60_000_000 / parsed.tempoUS,
    num: parsed.timeSig.num, den: denVal,
    quartersPerBar, stepsPerBar, totalBeats, nBars, bars,
    melodicNotes: nb.filter((n) => roleOf(n.pitch) === null).length,
    totalNotes: nb.length,
  };
}

function renderSteps(steps, n) {
  const s = new Array(n).fill('.');
  for (const st of steps) s[st] = 'X';
  return s.join('');
}

function analyzeFile(filePath) {
  const parsed = parseMidi(fs.readFileSync(filePath));
  const base = { file: path.basename(filePath), dir: path.basename(path.dirname(filePath)) };
  if (parsed.error) return { ...base, error: parsed.error };
  const q = quantize(parsed);
  if (!q) return { ...base, error: 'quantize failed' };
  if (q.skip) return { ...base, error: q.skip };
  const patMap = new Map();
  for (const bar of q.bars) {
    if (bar.size === 0) continue;
    const sig = {};
    for (const [role, arr] of bar) sig[role] = [...new Set(arr.map((h) => h.step))].sort((a, b) => a - b);
    const key = JSON.stringify(sig);
    const hit = patMap.get(key);
    if (hit) { hit.count++; continue; }
    const velMap = {};
    for (const [role, arr] of bar) {
      const byStep = new Map();
      for (const h of arr) byStep.set(h.step, (byStep.get(h.step) ?? 0) + h.vel);
      const per = {};
      for (const [s, sum] of byStep) per[s] = Math.round(sum / arr.filter((h) => h.step === s).length);
      velMap[role] = per;
    }
    patMap.set(key, { count: 1, sig, velMap });
  }
  const patterns = [...patMap.values()].sort((a, b) => b.count - a.count);
  const roleCounts = {};
  for (const bar of q.bars) for (const [role, arr] of bar) roleCounts[role] = (roleCounts[role] ?? 0) + arr.length;
  return {
    ...base, bpm: Math.round(q.bpm * 10) / 10,
    timeSig: q.num + '/' + q.den,
    totalBeats: Math.round(q.totalBeats * 100) / 100,
    nBars: q.nBars, stepsPerBar: q.stepsPerBar,
    melodicRatio: q.melodicNotes / Math.max(1, q.totalNotes),
    roleCounts, patterns,
  };
}

function pitchHistogram(filePath) {
  const parsed = parseMidi(fs.readFileSync(filePath));
  if (parsed.error) return parsed.error;
  const hist = {};
  for (const n of parsed.notes) hist[n.pitch] = (hist[n.pitch] ?? 0) + 1;
  return Object.entries(hist).sort((a, b) => b[1] - a[1]).map(([k, v]) => k + ':' + v).join(' ');
}

// ── CLI ──
const isMain = process.argv[1] && pathToFileURL(path.resolve(process.argv[1])).href === import.meta.url;
export const _internals = { parseMidi, roleOf, ROLE_PITCHES, pitchHistogram };
export { parseMidi, roleOf, ROLE_PITCHES, pitchHistogram };

if (isMain) {
  const args = process.argv.slice(2);
  const flagVal = (name) => {
    const i = args.indexOf(name);
    return i >= 0 ? args[i + 1] : null;
  };
  const has = (name) => args.includes(name);
  const jsonOut = flagVal('--json');
  const topN = flagVal('--top') ? parseInt(flagVal('--top'), 10) : 3;
  const showPitches = has('--pitches');
  const dirs = args.filter((a, i) => a === '--json' || a === '--top' || a === '--pitches'
    || args[i - 1] === '--json' || args[i - 1] === '--top' ? false : a);
  const scanDirs = dirs.length ? dirs : ['E:\\midi\\[1] Drum MIDIs'];

  const results = [];
  for (const dir of scanDirs) {
    if (!fs.existsSync(dir)) { console.log('SKIP (no such dir): ' + dir); continue; }
    const files = [];
    const walk = (d) => {
      for (const e of fs.readdirSync(d, { withFileTypes: true })) {
        const fp = path.join(d, e.name);
        if (e.isDirectory()) walk(fp);
        else if (/\.mid$/i.test(e.name)) files.push(fp);
      }
    };
    walk(dir);
    files.sort();
    let ok = 0, err = 0;
    console.log('\n==== ' + dir + '  (' + files.length + ' .mid files) ====');
    for (const fp of files) {
      let r;
      try { r = analyzeFile(fp); } catch (e) { r = { file: path.basename(fp), error: 'exception: ' + e.message }; }
      if (r.error) { err++; console.log('  [err] ' + r.file + ' — ' + r.error); continue; }
      ok++;
      const roles = Object.keys(r.roleCounts);
      console.log('  ' + r.file
        + ' | ' + r.bpm + 'bpm ' + r.timeSig + ' | bars:' + r.nBars
        + ' | ' + roles.map((k) => k + ':' + r.roleCounts[k]).join(' ')
        + (r.melodicRatio > 0.5 ? ' | MELODIC ' + Math.round(r.melodicRatio * 100) + '%' : ''));
      if (showPitches) {
        console.log('      pitches: ' + pitchHistogram(fp));
      }
      for (const pat of r.patterns.slice(0, topN)) {
        const grids = Object.entries(pat.sig)
          .map(([role, steps]) => role + ':' + renderSteps(steps, r.stepsPerBar))
          .join('  ');
        console.log('      x' + pat.count + '  ' + grids);
      }
    }
    console.log('  → analyzed ' + ok + ', errors ' + err);
  }

  const totals = {};
  let melodicFiles = 0;
  for (const r of results) {
    if (r.error) continue;
    if (r.melodicRatio > 0.5) melodicFiles++;
    for (const [role, cn] of Object.entries(r.roleCounts)) totals[role] = (totals[role] ?? 0) + cn;
  }
  console.log('\n==== ROLE TOTALS (' + results.filter((r) => !r.error).length + ' files analyzed, '
    + melodicFiles + ' melodic-dominant) ====');
  for (const [role, cn] of Object.entries(totals).sort((a, b) => b[1] - a[1]))
    console.log('  ' + role.padEnd(10) + cn);

  if (jsonOut) {
    const dump = { generatedAt: new Date().toISOString(), scanDirs, roleTotals: totals, files: results };
    fs.writeFileSync(jsonOut, JSON.stringify(dump, null, 1));
    console.log('\nJSON corpus → ' + jsonOut + ' (' + results.length + ' files)');
  }
}
