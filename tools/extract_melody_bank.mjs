#!/usr/bin/env node
// extract_melody_bank.mjs — melodic phrase extraction for MelodyPatternBank.h.
// Parses .mid (reuses analyze_drum_midis.mjs SMF parser), detects scale+root,
// quantizes to a 16th grid, detects the phrase period (1/2/4/8 bars), reduces
// each bar to a monophonic lead line (highest note per onset), converts notes
// to {step, degree, octave} key-relative contours, dedupes across files, and
// applies curation gates. Emits the MelodyPatternBank.h data arrays + count.
//
// Usage: node tools/extract_melody_bank.mjs [--json out.json] [--role lead] <dir...>
//   --role overrides the role for all dirs (lead|arp|bass|chord|pluck|pad).
//   Default: role auto-classified from dir name (Bass->bass, Chord->chord, else lead).

import fs from 'node:fs';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
import { parseMidi } from './analyze_drum_midis.mjs';

// ── Scale library (shared with scan_melodies.mjs) ──
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

// pcCounts: Map<pitchClass, noteCount>. Picks the (scale,root) with best
// fit, breaking ties by tonic support (how often the root pitch class is
// actually played) so relative-major/minor ambiguity resolves to the real
// tonic (e.g. an A-emphasized melody -> A minor, not C major).
function detectKey(pcCounts) {
  const total = [...pcCounts.values()].reduce((a, b) => a + b, 0);
  if (total === 0) return null;
  let best = null;
  for (let si = 0; si < SCALES.length; si++) {
    const intervals = SCALES[si][1];
    for (let r = 0; r < 12; r++) {
      const set = new Set(intervals.map((d) => (r + d) % 12));
      let m = 0, tonic = 0;
      for (const [pc, c] of pcCounts) { if (set.has(pc)) m += c; if (pc === r) tonic += c; }
      const fit = m / total;
      if (!best || fit > best.fit ||
          (fit === best.fit && tonic > best.tonic) ||
          (fit === best.fit && tonic === best.tonic && m > best.match)) {
        best = { si, root: r, fit, match: m, tonic };
      }
    }
  }
  return best;
}

// Convert a source MIDI pitch to {degree, octave} key-relative, such that
// 12*octave + root + intervals[degree] === pitch (exact reconstruction).
function toDegreeOctave(pitch, root, intervals) {
  const pc = ((pitch % 12) + 12) % 12;
  let deg = -1;
  for (let i = 0; i < intervals.length; i++) if (((root + intervals[i]) % 12 + 12) % 12 === pc) { deg = i; break; }
  if (deg < 0) return null;
  const oct = Math.floor((pitch - (root + intervals[deg])) / 12);
  return { degree: deg, octave: oct };
}

function quantizeMelodic(parsed) {
  const { tpqn, notes } = parsed;
  if (!tpqn || notes.length < 6) return null;
  const denVal = Math.pow(2, parsed.timeSig.den);
  const qpb = parsed.timeSig.num * (4 / denVal);
  const spb = Math.round(qpb * 4);
  if (spb < 8 || spb > 64) return null;
  const beat = (t) => t / tpqn;
  const nb = notes.map((n) => ({ start: beat(n.tick), dur: Math.max(0.05, n.dur / tpqn), pitch: n.pitch, vel: n.vel }))
    .sort((a, b) => a.start - b.start);
  const total = nb.reduce((m, n) => Math.max(m, n.start + n.dur), 0);
  const nBars = Math.max(1, Math.ceil(total / qpb));
  // per-bar: map step -> highest pitch note (monophonic lead) with its dur
  const bars = [];
  for (let b = 0; b < nBars; b++) {
    const s0 = b * qpb, s1 = s0 + qpb;
    const byStep = new Map(); // step -> { pitch, dur }
    for (const n of nb) {
      if (n.start < s0 || n.start >= s1) continue;
      let st = Math.round((n.start - s0) * 4);
      if (st >= spb) st = spb - 1;
      const cur = byStep.get(st);
      if (!cur || n.pitch > cur.pitch) byStep.set(st, { pitch: n.pitch, dur: n.dur * 4 }); // dur in 16ths
    }
    bars.push(byStep);
  }
  const pcs = new Map();
  for (const n of nb) pcs.set(n.pitch % 12, (pcs.get(n.pitch % 12) ?? 0) + 1);
  return { spb, nBars, bars, bpm: 60_000_000 / parsed.tempoUS, pcs };
}

function detectPeriod(rows) {
  const n = rows.length;
  const eq = (a, b) => JSON.stringify(a) === JSON.stringify(b);
  for (const P of [1, 2, 4, 8]) { if (P >= n) continue; let ok = true; for (let i = 0; i + P < n; i++) if (!eq(rows[i], rows[i + P])) { ok = false; break; } if (ok) return P; }
  return n;
}

// signature for dedupe: scale + step:degree sequence + rhythm
function sigOf(phrase) {
  const notes = phrase.notes.map((n) => n.step + ':' + n.degree + ':' + n.durSteps).join(',');
  return phrase.scaleMode + '|' + phrase.bars + '|' + notes;
}

function roleFromDir(dir) {
  const n = path.basename(dir).toLowerCase();
  if (/(bass|808|phonk)/.test(n)) return 'bass';
  if (/(chord)/.test(n)) return 'chord';
  if (/(arp)/.test(n)) return 'arp';
  if (/(pluck)/.test(n)) return 'pluck';
  if (/(pad)/.test(n)) return 'pad';
  return 'lead';
}

const isMain = process.argv[1] && pathToFileURL(path.resolve(process.argv[1])).href === import.meta.url;

if (isMain) {
  const args = process.argv.slice(2);
  const val = (nm) => { const i = args.indexOf(nm); return i >= 0 ? args[i + 1] : null; };
  const jsonOut = val('--json');
  const roleOverride = val('--role');
  const dirs = args.filter((a, i) => !['--json', '--role'].includes(a) && !['--json', '--role'].includes(args[i - 1]));

  const phrases = new Map(); // sig -> phrase
  const stats = { files: 0, usable: 0, skipped: 0, deduped: 0 };

  for (const dir of dirs) {
    if (!fs.existsSync(dir)) { console.log('SKIP dir: ' + dir); continue; }
    const role = roleOverride ?? roleFromDir(dir);
    const files = [];
    const walk = (d) => { for (const e of fs.readdirSync(d, { withFileTypes: true })) { const p = path.join(d, e.name); if (e.isDirectory()) walk(p); else if (/\.mid$/i.test(e.name)) files.push(p); } };
    walk(dir); files.sort();
    let packOk = 0;
    for (const f of files) {
      stats.files++;
      const name = path.basename(f);
      let parsed; try { parsed = parseMidi(fs.readFileSync(f)); } catch { stats.skipped++; continue; }
      if (parsed.error) { stats.skipped++; continue; }
      const q = quantizeMelodic(parsed);
      if (!q) { stats.skipped++; continue; }
      const key = detectKey(q.pcs);
      if (!key || key.fit < 0.7) { stats.skipped++; continue; }
      const intervals = SCALES[key.si][1];
      // build per-bar step->(degree,octave,dur) using detected key
      const rows = q.bars.map((byStep) => {
        const out = {};
        for (const [st, v] of byStep) {
          const d = toDegreeOctave(v.pitch, key.root, intervals);
          if (!d) continue;
          out[st] = { ...d, durSteps: Math.max(1, Math.round(v.dur)) };
        }
        return out;
      });
      const P = detectPeriod(rows);
      const phraseRows = rows.slice(0, P);
      while (phraseRows.length > 1 && Object.keys(phraseRows[phraseRows.length - 1]).length === 0) phraseRows.pop();
      // flatten to note list
      const notes = [];
      let distinctDeg = new Set();
      for (let b = 0; b < phraseRows.length; b++) {
        for (const [st, d] of Object.entries(phraseRows[b])) {
          notes.push({ step: b * q.spb + +st, degree: d.degree, octave: d.octave, durSteps: d.durSteps });
          distinctDeg.add(d.degree);
        }
      }
      // curation gates: multi-bar, >=5 notes, >=3 distinct degrees, not a single drone
      if (phraseRows.length < 2 || notes.length < 5 || distinctDeg.size < 3) { stats.skipped++; continue; }
      const phrase = { role, bars: phraseRows.length, grid: q.spb, rootPc: key.root, scaleMode: key.si, scaleName: SCALES[key.si][0], bpm: Math.round(q.bpm), source: name, sourcePack: path.basename(dir), notes };
      const sig = sigOf(phrase);
      if (phrases.has(sig)) { stats.deduped++; continue; }
      phrases.set(sig, phrase);
      packOk++;
      stats.usable++;
    }
    console.log('  ' + dir + ' [' + role + '] -> ' + packOk + ' phrases');
  }

  const list = [...phrases.values()];
  console.log('\nTOTAL distinct phrases: ' + list.length + ' (files=' + stats.files + ', usable=' + stats.usable + ', skipped=' + stats.skipped + ', deduped=' + stats.deduped + ')');

  // group by role, cap per role, DIVERSIFY by round-robining across packs
  // so one pack doesn't dominate the cap (insertion order would take all 60
  // from the first pack scanned).
  const byRole = new Map();
  for (const p of list) { if (!byRole.has(p.role)) byRole.set(p.role, []); byRole.get(p.role).push(p); }
  const caps = { lead: 60, arp: 35, bass: 30, chord: 20, pluck: 20, pad: 10 };
  const sel = [];
  for (const [role, arr] of byRole) {
    const cap = caps[role] ?? 20;
    // bucket by source pack (dir of file)
    const byPack = new Map();
    for (const p of arr) {
      const pk = p.sourcePack || '?'; if (!byPack.has(pk)) byPack.set(pk, []); byPack.get(pk).push(p);
    }
    const keys = [...byPack.keys()];
    const capped = [];
    const idx = {}; keys.forEach((k) => (idx[k] = 0));
    let any = true;
    while (any && capped.length < cap) {
      any = false;
      for (const k of keys) {
        if (idx[k] < byPack.get(k).length) { capped.push(byPack.get(k)[idx[k]++]); any = true; if (capped.length >= cap) break; }
      }
    }
    console.log('  role ' + role + ': ' + arr.length + ' distinct across ' + keys.length + ' packs, keeping ' + capped.length);
    sel.push(...capped);
  }

  if (jsonOut) {
    fs.writeFileSync(jsonOut, JSON.stringify({ count: sel.length, phrases: sel }, null, 1));
    console.log('\nJSON -> ' + jsonOut + ' (' + sel.length + ' phrases)');
  }

  // Emit C++ arrays (MelodyPatternBank.h body)
  const noteLines = [];
  const phLines = [];
  let off = 0;
  sel.forEach((p, idx) => {
    p.notes.forEach((n) => noteLines.push('{ ' + n.step + ', ' + n.degree + ', ' + n.octave + ', ' + n.durSteps + ' }'));
    phLines.push('{ "' + p.role + '_m' + (idx + 1) + '", "' + p.role + '", ' + p.bars + ', ' + p.grid + ', ' + p.rootPc + ', ' + p.scaleMode + ', ' + p.bpm + ', "' + p.source.replace(/\\/g, '/') + '", ' + off + ', ' + p.notes.length + ' }');
    off += p.notes.length;
  });
  const body =
    'inline const MelodyNote* melodyNotes() {\n' +
    '    static const MelodyNote n[] = {\n' + noteLines.map((l) => '        ' + l + ',').join('\n') + '\n    };\n' +
    '    return n;\n}\n\n' +
    'inline const MelodicPhrase* melodyPhrases()\n{\n' +
    '    static const MelodicPhrase p[] = {\n' + phLines.map((l) => '        ' + l + ',').join('\n') + '\n    };\n' +
    '    return p;\n}\n\n' +
    'inline int melodyPhraseCount() { return ' + sel.length + '; }\n' +
    'inline int melodyNoteCount() { return ' + off + '; }\n';
  fs.writeFileSync('melody_bank_gen.txt', body);
  console.log('\nEmitted C++ -> melody_bank_gen.txt (phrases=' + sel.length + ', notes=' + off + ')');
}