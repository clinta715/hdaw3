import fs from 'node:fs';
import path from 'node:path';
import { _internals } from './analyze_drum_midis.mjs';
const { pitchHistogram } = _internals;
const root = 'E:\\midi';
const drumish = (n) => /drum|hi.?hat|hat|clap|snare|perc|808|break|trap.*drum|rock|kit/i.test(n) && !/chord|melod|lead/i.test(n);
const dirs = fs.readdirSync(root, { withFileTypes: true })
  .filter((d) => d.isDirectory() && drumish(d.name));
let total = 0;
const rows = [];
for (const d of dirs) {
  const files = [];
  const walk = (dd) => { for (const e of fs.readdirSync(dd, { withFileTypes: true })) { const p = path.join(dd, e.name); if (e.isDirectory()) walk(p); else if (/\.mid$/i.test(e.name)) files.push(p); } };
  walk(path.join(root, d.name));
  total += files.length;
  rows.push({ dir: d.name, n: files.length });
}
rows.sort((a, b) => b.n - a.n);
console.log('DRUM-ISH DIRS (' + rows.length + '), total files ' + total);
for (const r of rows) console.log('  ' + r.dir.padEnd(45) + r.n);
// sample pitch histograms from the biggest multi-instrument dirs
const sampleDirs = ['Drum MIDI Collection', 'Drum Midis', 'Mix Elite MIDI DRUM and Hat Loops', 'Drum Loop Midi', 'Toontrack Fundamental Rock MiDi-ARCADiA'].filter((n) => dirs.some((d) => d.name === n));
for (const dn of sampleDirs) {
  const files = [];
  const walk = (dd) => { for (const e of fs.readdirSync(dd, { withFileTypes: true })) { const p = path.join(dd, e.name); if (e.isDirectory()) walk(p); else if (/\.mid$/i.test(e.name)) files.push(p); } };
  walk(path.join(root, dn));
  console.log('\n== ' + dn + ' (' + files.length + ') — first 3 files' );
  for (const f of files.slice(0, 3)) {
    console.log('   ' + path.basename(f));
    console.log('      ' + pitchHistogram(f));
  }
}
