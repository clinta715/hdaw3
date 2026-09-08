import fs from 'node:fs';
import path from 'node:path';
import { parseMidi } from './analyze_drum_midis.mjs';
const args = process.argv.slice(2);
const files = [];
for (const a of args) {
  if (fs.statSync(a).isDirectory()) {
    const walk = (d) => { for (const e of fs.readdirSync(d, { withFileTypes: true })) { const p = path.join(d, e.name); if (e.isDirectory()) walk(p); else if (/\.mid$/i.test(e.name)) files.push(p); } };
    walk(a);
  } else files.push(a);
}
for (const f of files) {
  const p = parseMidi(fs.readFileSync(f));
  const tpqn = p.tpqn;
  const denVal = Math.pow(2, p.timeSig.den);
  const qpb = p.timeSig.num * (4 / denVal);
  const notes = p.notes.map((n) => ({ start: n.tick / tpqn, vel: n.vel, pitch: n.pitch })).sort((a, b) => a.start - b.start);
  const total = notes.reduce((m, n) => Math.max(m, n.start + 0.05), 0);
  const nBars = Math.ceil(total / qpb);
  const stepChars = ['0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f'];
  console.log('\n== ' + path.basename(f) + '  (' + nBars + ' bars x 16, ' + notes.length + ' notes, tpqn=' + tpqn + ')');
  for (let b = 0; b < nBars; b++) {
    const s = new Array(16).fill('.');
    const acc = new Array(16).fill(0);
    for (const n of notes) {
      const pos = n.start - b * qpb;
      if (pos < 0 || pos >= qpb) continue;
      let st = Math.round(pos * 4);
      if (st >= 16) continue;
      acc[st]++; s[st] = acc[st] > 9 ? '*' : stepChars[acc[st]];
    }
    console.log('  bar' + String(b).padStart(2) + ': ' + s.join(''));
  }
}
