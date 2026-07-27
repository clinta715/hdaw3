import { test, expect, Page } from "@playwright/test";
import { startApp, rpcCall, addMidiClip, addAudioClip, writeSineWav, tempWavPath } from "./helpers";

// Isolate the canvas/GPU-memory hypothesis for the black screen.
// Pumps clips via direct RPC (NO dragging → no vanish/hang confound), placing
// duplicates NON-overlapping (far to the right) so Case-1 deletion can't fire.
// Each batch logs: engine clip count, DOM clip count, canvas count, JS heap,
// and checks responsiveness / ErrorBoundary / errors. If the renderer dies as
// clip+canvas count grows (with no JS error), that's a GPU/memory black screen.

const cap = { errors: [] as string[], logs: [] as string[] };
test.afterEach(async ({}, info) => {
  const s = `=== "${info.title}" ===\nerrors(${cap.errors.length}):\n${cap.errors.join("\n\n") || "(none)"}\nlog:\n${cap.logs.join("\n")}`;
  console.log(s);
  await info.attach("captured-output", { body: s, contentType: "text/plain" });
});
function attach(page: Page) {
  cap.errors = []; cap.logs = [];
  page.on("console", (m) => { if (m.type() === "error") cap.errors.push(`[c.err] ${m.text()}`); });
  page.on("pageerror", (e) => cap.errors.push(`[pageerror] ${e.message}`));
  page.on("crash", () => cap.errors.push("[PAGE CRASH]"));
}
const log = (m: string) => { const t = new Date().toISOString().slice(11, 23); cap.logs.push(`${t}  ${m}`); console.log(`${t}  ${m}`); };
const heapMB = async (p: Page) => Math.round(((await p.evaluate(() => (performance as any).memory?.usedJSHeapSize ?? 0).catch(() => 0)) as number) / 1024 / 1024);

async function status(page: Page) {
  const dom = await page.locator(".tl-clip").count().catch(() => -1);
  const canvases = await page.locator(".tl-clip canvas").count().catch(() => -1);
  const heap = await heapMB(page);
  return { dom, canvases, heap };
}

test("memory stress: does accumulating clips/canvases kill the renderer?", async ({ page }) => {
  test.setTimeout(180_000);
  attach(page);
  await startApp(page);

  const wav = tempWavPath("mem");
  writeSineWav(wav);
  // Seed one MIDI (with notes → MidiThumbnailCanvas draws) + one audio (WaveformCanvas).
  const m0 = await addMidiClip(page, { trackIndex: 0, start: 0, duration: 4 });
  await rpcCall(page, "project.addNote", { clipId: m0, pitch: 60, velocity: 100, startBeat: 0, durationBeats: 0.5 });
  await rpcCall(page, "project.addNote", { clipId: m0, pitch: 67, velocity: 90, startBeat: 1, durationBeats: 0.5 });
  const a0 = await addAudioClip(page, { trackIndex: 1, start: 0, sourceFile: wav });
  await page.waitForTimeout(300);
  log(`seed heap=${await heapMB(page)}MB`);

  // Grow by duplicating onto fresh, non-overlapping slots far to the right.
  // Each iteration ~doubles clip count. Capped at a moderate count — this guards
  // the N-rebuilds→1 coalescing fix (before it, duplicating 32 clips took 31s
  // and timed out). The remaining O(project) rebuild cost at EXTREME counts
  // (128+ clips in one burst) is a separate follow-up (incremental routing).
  const MAX_CLIPS = 48; // stops after the 32→64 batch
  let nextStart = 8;
  for (let it = 0; it < 12; it++) {
    const clips = await rpcCall<{ clips: { clipId: number; durationBeats: number; trackIndex: number; isMidi: boolean }[] }>(page, "read.snapshot")
      .catch(() => null);
    if (!clips?.clips?.length) { log(`it${it}: snapshot unreadable, stopping`); break; }
    const src = clips.clips;
    const ids = src.map((c) => c.clipId);
    const starts = src.map(() => { const s = nextStart; nextStart += 8; return s; });        // non-overlapping
    const tracks = src.map((c) => c.trackIndex);
    await rpcCall(page, "project.duplicateClips", { clipIds: ids, newStarts: starts, newTrackIndices: tracks }).catch((e) => {
      cap.errors.push(`[dup failed] ${String(e)}`);
    });
    await page.waitForTimeout(600); // let sync + canvases settle
    const st = await status(page);
    const alive = await page.evaluate(() => 1, { timeout: 4000 }).catch(() => 0);
    const boundary = await page.locator("text=HDAW hit a render error").count().catch(() => 0);
    log(`it${it}: engine=${src.length}→? DOM=${st.dom} canvases=${st.canvases} heap=${st.heap}MB alive=${!!alive} boundary=${boundary}`);
    if (!alive) { cap.errors.push("[FROZEN/CLOSED] page unresponsive after duplicate batch"); break; }
    if (boundary) { cap.errors.push("[ErrorBoundary fired] " + await page.locator("pre").first().innerText().catch(() => "")); break; }
    if (st.dom >= MAX_CLIPS) { log(`reached ${MAX_CLIPS} clips, stopping`); break; }
  }

  await page.waitForTimeout(500);
  log(`final heap=${await heapMB(page)}MB`);
  if (cap.errors.length) throw new Error(`Faults (${cap.errors.length}):\n${cap.errors.join("\n")}`);
  log("PASS: renderer survived clip/canvas accumulation");
});
