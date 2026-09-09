
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
const __dirname = path.dirname(fileURLToPath(import.meta.url));
const DIST = JSON.parse(fs.readFileSync(path.join(__dirname,'distributions.json'),'utf8'));

function hashSeed(s){let h=2166136261>>>0;for(let i=0;i<s.length;i++){h^=s.charCodeAt(i);h=Math.imul(h,16777619);}return h>>>0;}
function mulberry32(a){return function(){a|=0;a=(a+0x6D2B79F5)|0;let t=Math.imul(a^(a>>>15),1|a);t=(t+Math.imul(t^(t>>>7),61|t))^t;return((t^(t>>>14))>>>0)/4294967296;};}
const pick=(rng,arr)=>arr[Math.floor(rng()*arr.length)];
const pickP=(rng,items)=>{const r=rng();let acc=0;for(const it of items){acc+=it.p;if(r<acc)return it;}return items[items.length-1];};
const clamp=(v,a,b)=>Math.max(a,Math.min(b,v));

export function sampleArrangement(seed, opts={}){
  const rng=mulberry32(hashSeed(String(seed)));
  const lenMode = opts.lengthMode ?? pickP(rng,DIST.length.modes);
  const bars = opts.bars ?? Math.round(lenMode.min + rng()*(lenMode.max-lenMode.min));
  const kickIntro = opts.kickIntro ?? pickP(rng,DIST.kick.introModes);
  const kickEntry = opts.kickEntry ?? clamp(kickIntro.bars,0,DIST.caps.introMaxBars);
  const constBass = opts.constBass ?? (rng()<DIST.constBass);
  const lateNovelty = opts.lateNovelty ?? (rng()<DIST.lateNovelty);
  const breakdown = opts.breakdown ?? (rng()<DIST.breakdown);
  const noveltyRole = opts.noveltyRole ?? pick(rng,DIST.noveltyPool);
  const noveltyAt = Math.round(bars*(0.45+rng()*0.25)); // bars
  const breakLen = Math.round(DIST.caps.breakdownLen[0]+rng()*(DIST.caps.breakdownLen[1]-DIST.caps.breakdownLen[0]));

  const layers={};
  const setL=(r,a,b,flags={})=>{if(!layers[r])layers[r]={firstBar:a,lastBar:b,...flags}; else layers[r].lastBar=Math.max(layers[r].lastBar,b);};
  const sections=[];
  let cursor=0;
  const push=(name,len,roles,density)=>{ if(len<2) return null; const s={name,barStart:cursor,bars:len,roles,density}; sections.push(s); cursor+=len; return s; };

  // 1) intro (minimum 4 bars of atmosphere unless fourOnFloor-with-tight-build)
  const introLen = Math.max(kickEntry, 4);
  push('intro', introLen, ['pad','fx'], 0.30);
  setL('pad', 0, bars);
  setL('fx', 0, bars);

  // 2) build1 -> dropA
  const dropATarget = Math.round(bars*(0.22+rng()*0.12));
  let dropA = Math.max(cursor+8, dropATarget);
  // reserve room: dropB + outro need ~ (bars*0.45)
  dropA = clamp(dropA, cursor+4, Math.round(bars*0.5));
  if (dropA > cursor+3) push('build1', dropA-cursor, ['pad','fx',...(kickEntry>0?['kick']:[]),...(constBass?['bass']:[]),'hats'], 0.55);
  const dropAStart = cursor;
  setL('kick', kickEntry, Math.round(bars*0.85));
  if (constBass) setL('bass',0,bars); else setL('bass',Math.max(dropAStart-8,0),bars);
  setL('hats', Math.max(kickEntry, Math.round(bars*0.10)), Math.round(bars*0.90));

  // 3) dropA (16-32 bars)
  let dropALen = Math.min(DIST.caps.dropLen[1], Math.round(bars*0.20));
  const midPoint = Math.round(bars/2);
  dropALen = clamp(dropALen, 12, Math.max(12, midPoint-cursor-4));
  push('dropA', dropALen, ['kick','bass','hats','snare','clap','arp','stabs','fx'], 0.85);
  setL('snare', dropAStart, Math.round(bars*0.82));
  setL('clap', dropAStart, Math.round(bars*0.82));
  setL('arp', dropAStart, Math.round(bars*0.90), {const:true});
  setL('stabs', dropAStart, Math.round(bars*0.80));

  // 4) minibreak (optional, 45-70%)
  let breakStart = null;
  if (breakdown && bars >= 48){
    const target = Math.round(bars*(0.45+rng()*0.25));
    breakStart = clamp(target, dropAStart + dropALen + 4, bars - 24);
    if (breakStart > cursor+2){ push('minibreak', breakLen, ['bass','pad'], 0.45); } // kickless breakdown (psy convention)
  }

  // 5) build2
  const build2Start = cursor;
  const build2Len = Math.max(4, Math.round(bars*0.06));
  push('build2', Math.min(build2Len, bars-12-cursor), ['kick','bass','hats','arp','fx','riser'], 0.70);

  // 6) dropB with optional novelty layer
  const dropBRoles=['kick','bass','hats','snare','clap','arp','stabs','fx'];
  if (lateNovelty){ dropBRoles.push(noveltyRole); setL(noveltyRole, Math.max(cursor, noveltyAt), Math.min(bars, noveltyAt+Math.round(bars*0.25)), {novelty:true}); }
  // 6b) budget the tail so dropB + outro always exactly fill the song
  const remaining = bars - cursor;
  const outroBudget = Math.max(2, Math.min(4, Math.floor(remaining * 0.2)));
  const dropBLen = Math.max(2, Math.min(Math.round(bars * 0.22), remaining - outroBudget));
  push('dropB', dropBLen, dropBRoles, lateNovelty ? 0.95 : 0.85);

  // 7) outro (always fills the remainder exactly)
  push('outro', bars - cursor, ['bass','pad','fx'], 0.35);

  return { method:'corpusArranger', version:1, seed:String(seed),
    length:{bars, mode:lenMode.name},
    flags:{ kickIntro:kickIntro.name, constBass, lateNovelty, breakdown, noveltyRole: lateNovelty?noveltyRole:null, kicklessBreakdown: !!breakStart },
    layers,
    sections: sections.map(s=>({...s, barEnd: s.barStart+s.bars})) };
}

const seeds=process.argv.slice(2);
if(!seeds.length) seeds.push('demo');
for(const s of seeds) console.log(JSON.stringify(sampleArrangement(s)));
