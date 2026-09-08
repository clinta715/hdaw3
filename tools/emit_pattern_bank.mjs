#!/usr/bin/env node
// emit_pattern_bank.mjs — convert a phrase-bank JSON into RhythmPatternGenerator
// DSL strings + a C++-ready static data snippet for RhythmPatternBank.h.
//
// Usage: node tools/emit_pattern_bank.mjs <bank.json> [--cpp out.h] [--preview]

import fs from 'node:fs';
import path from 'node:path';

const ROLE_PITCH = { kick: 36, snare: 38, clap: 39, hats: 42, openHat: 46, perc: 60, tom: 47, crash: 49, ride: 51 };

const [bankPath, cppOut] = (() => {
  const args = process.argv.slice(2);
  const pi = (n) => { const i = args.indexOf(n); return i >= 0 ? args[i + 1] : null; };
  return [args.find((a) => a.endsWith('.json') && !a.startsWith('--')), pi('--cpp')];
})();
if (!bankPath) { console.log('usage: node tools/emit_pattern_bank.mjs <bank.json> [--cpp out.h]'); process.exit(1); }
const bank = JSON.parse(fs.readFileSync(bankPath, 'utf8'));

// DSL string: each phrase.grid row is an array of step INDICES (0..15).
// Expand each row to a 16-char bar ('x' hit, '-' rest), then join bars.
const PER = 16; // steps per bar (4/4 sixteenths) for this corpus
const dslOf = (ph) => ph.grid.map((row) => {
  const set = new Set(row);
  let s = '';
  for (let i = 0; i < PER; i++) s += set.has(i) ? 'x' : '-';
  return s;
}).join('');

const rows = bank.phrases.map((ph) => {
  const dsl = dslOf(ph);
  const dense = dsl.replace(/-/g, '');              // count actual hits
  const tag = dense.replace(/x/g, 'h').slice(0, 14) || 'empty';
  return {
    id: ph.role + '_' + ph.bars + 'bar_' + tag,
    role: ph.role, bars: ph.bars, grid: PER, dsl,
    pitch: ROLE_PITCH[ph.role] ?? 60,
    bpm: Math.round(ph.bpm[0] ?? 120), source: (ph.sources[0] ?? '').replace(/x\d+$/, ''), weight: ph.total,
  };
});

// preview
console.log('=== PREVIEW (' + rows.length + ' phrases) ===');
for (const r of rows) {
  console.log('\n[' + r.id + '] ' + r.role + ' ' + r.bars + 'bar @' + r.bpm + 'bpm (pitch ' + r.pitch + ')  src:' + r.source);
  const per = 16;
  for (let b = 0; b < r.bars; b++)
    console.log('  b' + b + ' ' + r.dsl.slice(b * per, (b + 1) * per).replace(/x/g, 'X'));
  console.log('  DSL: ' + r.dsl);
}

if (cppOut) {
  const lines = [];
  lines.push('// Auto-generated from ' + path.basename(bankPath) + ' — corpus-derived drum phrases.');
  lines.push('// Each phrase = a RhythmPatternGenerator DSL string (bars x 16 steps) + role metadata.');
  lines.push('// struct RhythmicPhrase { const char* id; const char* role; int bars; int grid;');
  lines.push('//                   const char* dsl; int pitch; int bpm; const char* source; };');
  lines.push('static const RhythmicPhrase kCorpusPhrases[] = {');
  for (const r of rows)
    lines.push('  { "' + r.id + '", "' + r.role + '", ' + r.bars + ', ' + r.grid + ', "' + r.dsl + '", ' + r.pitch + ', ' + r.bpm + ', "' + r.source + '" },');
  lines.push('};');
  lines.push('static const int kCorpusPhraseCount = ' + rows.length + ';');
  fs.writeFileSync(cppOut, lines.join('\n'));
  console.log('\nC++ snippet → ' + cppOut);
}
