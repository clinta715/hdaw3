import { test, expect, Page } from "@playwright/test";
import { startApp, rpcCall, addMidiClip, addAudioClip, dragClip, writeSineWav, tempWavPath } from "./helpers";

// Crash-focused realistic stress (post-fix validation). Exercises the exact
// trigger (drag / move / duplicate) at a realistic project size and asserts the
// RENDERER SURVIVES: no page close, no ErrorBoundary, main thread responsive
// throughout. Resilient to the separate Case-1 clip-vanish data bug (re-fetches
// live clip ids each round, skips vanished clips) so it isolates the crash fix.

const cap = { errors: [] as string[], logs: [] as string[] };
test.afterEach(async ({}, info) => {
  const s = `=== "${info.title}" ===\nerrors(${cap.errors.length}):\n${cap.errors.join("\n\n") || "(none)"}\nlog:\n${cap.logs.join("\n")}`;
  console.log(s); await info.attach("out", { body: s, contentType: "text/plain" });
});
function attach(page: Page) {
  cap.errors = []; cap.logs = [];
  page.on("console", (m) => { if (m.type() === "error") cap.errors.push(`[c.err] ${m.text()}`); });
  page.on("pageerror", (e) => cap.errors.push(`[pageerror] ${e.message}`));
  page.on("crash", () => cap.errors.push("[PAGE CRASH]"));
}
const log = (m: string) => { const t = new Date().toISOString().slice(11, 23); cap.logs.push(`${t}  ${m}`); console.log(`${t}  ${m}`); };
const heapMB = async (p: Page) => Math.round(((await p.evaluate(() => (performance as any).memory?.usedJSHeapSize ?? 0).catch(() => 0)) as number) / 1024 / 1024);

async function liveIds(page: Page): Promise<number[]> {
  const s = await rpcCall<{ clips: { clipId: number }[] } | null>(page, "read.snapshot").catch(() => null);
  return (s?.clips ?? []).map((c) => c.clipId);
}
// Drag only if the clip still exists; boundingBox with a short timeout so a
// vanished clip skips instead of hanging.
async function dragIfThere(page: Page, id: number, dx: number, dy: number, mods: ("Control")[] = []) {
  const exists = (await page.locator(`.tl-clip[data-clip-id="${id}"]`).count().catch(() => 0)) > 0;
  if (!exists) return false;
  try {
    await Promise.race([
      dragClip(page, id, dx, dy, mods.length ? { modifiers: mods } : {}),
      new Promise((_, rej) => setTimeout(() => rej(new Error("drag-hang")), 6000)),
    ]);
    return true;
  } catch { return false; }
}

test("realistic arrange: renderer survives repeated move/duplicate", async ({ page }) => {
  test.setTimeout(180_000);
  attach(page);
  await startApp(page);

  const wav = tempWavPath("real");
  writeSineWav(wav);
  for (let i = 0; i < 4; i++) await addMidiClip(page, { trackIndex: i % 3, start: i * 5, duration: 4, name: `M${i}` });
  for (let i = 0; i < 2; i++) await addAudioClip(page, { trackIndex: 0, start: 30 + i * 5, sourceFile: wav, name: `A${i}` });
  for (const id of await liveIds(page)) {
    await rpcCall(page, "project.addNote", { clipId: id, pitch: 60, velocity: 100, startBeat: 0, durationBeats: 0.5 });
  }
  await page.waitForTimeout(400);
  log(`seeded heap=${await heapMB(page)}MB`);

  for (let round = 0; round < 10; round++) {
    let ids = await liveIds(page);
    // Move each clip a little (skip vanished).
    for (const id of ids) { await dragIfThere(page, id, 30, 0); await page.waitForTimeout(40); }
    // Duplicate a couple.
    const midi = ids.slice(0, 2);
    for (const id of midi) { await dragIfThere(page, id, 25, 0, ["Control"]); await page.waitForTimeout(120); }

    // Health check: responsive? boundary? page alive?
    const alive = await page.evaluate(() => 1, { timeout: 4000 }).catch(() => 0);
    const boundary = await page.locator("text=HDAW hit a render error").count().catch(() => 0);
    ids = await liveIds(page);
    log(`r${round}: clips=${ids.length} heap=${await heapMB(page)}MB alive=${!!alive} boundary=${boundary}`);
    if (!alive) { cap.errors.push("[FROZEN/CLOSED] renderer died"); break; }
    if (boundary) { cap.errors.push("[ErrorBoundary] " + await page.locator("pre").first().innerText().catch(() => "")); break; }
  }

  await page.waitForTimeout(500);
  log(`final heap=${await heapMB(page)}MB`);
  if (cap.errors.length) throw new Error(`Renderer fault:\n${cap.errors.join("\n")}`);
  log("PASS: renderer survived realistic arrange stress");
});
