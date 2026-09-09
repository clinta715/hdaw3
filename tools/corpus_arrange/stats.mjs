import { sampleArrangement } from './generate.mjs';

const N = 20;
const seeds = Array.from({ length: N }, (_, i) => String(i + 1));
const plans = seeds.map(s => sampleArrangement(s));

// determinism
const d1 = JSON.stringify(sampleArrangement('7'));
const d2 = JSON.stringify(sampleArrangement('7'));
const deterministic = d1 === d2;

// structural invariants
const anomalies = [];
for (const p of plans) {
  let cur = 0;
  for (const s of p.sections) {
    if (s.barStart !== cur) anomalies.push(String(p.seed) + ': gap at ' + s.name + ' want ' + cur + ' got ' + s.barStart);
    if (s.bars < 2) anomalies.push(String(p.seed) + ': tiny section ' + s.name + ' ' + s.bars + 'B');
    cur = s.barStart + s.bars;
  }
  if (cur !== p.length.bars) anomalies.push(String(p.seed) + ': end ' + cur + ' != ' + p.length.bars);
  const intro = p.sections[0];
  if (!intro || intro.name !== 'intro' || intro.bars > 24) anomalies.push(String(p.seed) + ': bad intro');
  const kick = p.layers['kick'];
  if (kick && kick.firstBar > 24) anomalies.push(String(p.seed) + ': kick entry ' + kick.firstBar + 'B > cap 24');
}

const cnt = (fn) => plans.filter(fn).length;
const bars = plans.map(p => p.length.bars).sort((a, b) => a - b);
const med = (a) => a[Math.floor(a.length / 2)];
const modeCounts = plans.reduce((m, p) => { m[p.length.mode] = (m[p.length.mode] ?? 0) + 1; return m; }, {});
const kickMode = plans.reduce((m, p) => { m[p.flags.kickIntro] = (m[p.flags.kickIntro] ?? 0) + 1; return m; }, {});
const noveltyRoles = plans.filter(p => p.flags.lateNovelty).map(p => p.flags.noveltyRole);
const secCount = plans.reduce((m, p) => m + p.sections.length, 0) / N;
const roleUse = {};
for (const p of plans) for (const r of Object.keys(p.layers)) roleUse[r] = (roleUse[r] ?? 0) + 1;

console.log(JSON.stringify({
  n: N, deterministic,
  bars: { min: bars[0], p25: bars[Math.floor(N * 0.25)], median: med(bars), p75: bars[Math.floor(N * 0.75)], max: bars[N - 1] },
  modeCounts,
  flagRates: {
    kickIntro: kickMode,
    constBass: +(cnt(p => p.flags.constBass) / N).toFixed(2),
    lateNovelty: +(cnt(p => p.flags.lateNovelty) / N).toFixed(2),
    breakdown: +(cnt(p => p.flags.breakdown) / N).toFixed(2),
    kicklessBreakdown: +(cnt(p => p.flags.kicklessBreakdown) / N).toFixed(2)
  },
  noveltyRolesUsed: noveltyRoles,
  avgSections: +secCount.toFixed(1),
  roleUseByTrack: roleUse,
  anomalies,
  priors: { constBass: 0.37, lateNovelty: 0.20, breakdown: 0.05, lengthModes: '0.50/0.25/0.25' }
}, null, 2));
