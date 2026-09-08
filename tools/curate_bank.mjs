#!/usr/bin/env node
// curate_bank.mjs — curate a phrase-bank JSON into bank-worthy entries and
// emit C++ RhythmPatternBank array lines. Selection: multi-bar (bars>=2) OR
// cross-file-verified (total>=2), and non-trivial (>=2 hits). Sorted by
// frequency then bars, capped per role.
// Usage: node tools/curate_bank.mjs <bank.json> [--cap role:n,...]
import fs from 'node:fs';
const PER = 16;
const ROLE_PITCH = { kick:36, snare:38, clap:39, hats:42, openHat:46, perc:60, tom:47, crash:49, ride:51 };
const ORDER = ['kick','snare','clap','hats','openHat','perc','tom','crash','ride'];
const bank = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'));
const dslOf = (ph) => ph.grid.map(row => { const s=new Set(row); let o=''; for(let i=0;i<PER;i++) o+=s.has(i)?'x':'-'; return o; }).join('');
const hitCount = (d) => (d.match(/x/g)||[]).length;
const phrases = bank.phrases.map(ph => {
  const dsl = dslOf(ph);
  return { role: ph.role, bars: ph.bars, grid: PER, dsl, hits: hitCount(dsl),
           total: ph.total||0, nFiles: ph.nFiles||1, bpm: (ph.bpm && ph.bpm[0])||0, sources: ph.sources||[] };
});
const minBars = (() => { const i = process.argv.indexOf('--min-bars'); return i>=0?parseInt(process.argv[i+1],10):1; })();
const minHits = (() => { const i = process.argv.indexOf('--min-hits'); return i>=0?parseInt(process.argv[i+1],10):2; })();
const selected = phrases.filter(p => p.bars>=minBars && p.hits>=minHits && (p.total>=2 || p.bars>=2));
const byRole = {};
for (const p of selected) (byRole[p.role] ??= []).push(p);
const cap = {};
const ci = process.argv.indexOf('--cap');
if (ci >= 0) for (const kv of process.argv[ci+1].split(',')) { const [k,v]=kv.split(':'); cap[k]=+v; }
const defaults = { hats:12, kick:8, snare:8, clap:6, perc:8, tom:3, crash:3, ride:3 };
const picks = [];
for (const role of ORDER) {
  const list = byRole[role] || [];
  list.sort((a,b)=> b.total - a.total || b.bars - a.bars);
  const n = cap[role] ?? defaults[role] ?? 5;
  list.slice(0, n).forEach((p,i) => picks.push({ ...p, id: role + '_' + p.bars + 'bar_' + (i+1), pitch: ROLE_PITCH[role]??60 }));
}
let count=0;
for (const p of picks) {
  count++;
  const src = (p.sources[0]||'').replace(/x\d+$/, '').replace(/\.mid$/i, '');
  console.log('  { "' + p.id + '", "' + p.role + '", ' + p.bars + ', ' + p.grid + ', "' + p.dsl + '", ' + p.pitch + ', ' + Math.round(p.bpm) + ', "' + src + '" },');
}
console.log('// entries=' + count + '  (selected ' + selected.length + ' of ' + phrases.length + ')');
