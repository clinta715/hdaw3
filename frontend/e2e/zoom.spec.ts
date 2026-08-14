import { test, expect } from "@playwright/test";
import { startApp, addMidiClip, clipLocator } from "./helpers";

// Read the live pps from the toolbar label ("{pps} px/beat").
async function readPps(page: import("@playwright/test").Page): Promise<number> {
  const text = await page.locator(".tl-tb-label").textContent();
  const n = text ? parseFloat(text) : NaN;
  if (!Number.isFinite(n)) throw new Error(`could not parse pps from "${text}"`);
  return n;
}

// Tracks-viewport geometry: left edge (screen) + width + current scrollLeft.
async function tracksGeometry(page: import("@playwright/test").Page) {
  return page.evaluate(() => {
    const el = document.querySelector(".tl-tracks") as HTMLElement;
    const r = el.getBoundingClientRect();
    return { left: r.left, width: r.width, scrollLeft: el.scrollLeft };
  });
}

test.describe("Timeline zoom (wheel, pointer-centered, marquee)", () => {
  test("plain wheel over the ruler zooms (no modifier required)", async ({ page }) => {
    await startApp(page);
    const c1 = await addMidiClip(page, { trackIndex: 0, start: 0, duration: 4, name: "RulerWheel" });

    const rulerBox = await page.locator(".tl-ruler").boundingBox();
    if (!rulerBox) throw new Error("ruler has no bounding box");

    const widthBefore = await clipLocator(page, c1).evaluate((el) => el.getBoundingClientRect().width);

    // Hover the ruler and wheel up (deltaY < 0 = zoom in).
    await page.mouse.move(rulerBox.x + rulerBox.width / 2, rulerBox.y + rulerBox.height / 2);
    await page.mouse.wheel(0, -100);

    // Zoom-in grows the clip width.
    await expect
      .poll(async () => clipLocator(page, c1).evaluate((el) => el.getBoundingClientRect().width), { timeout: 5000 })
      .toBeGreaterThan(widthBefore);
  });

  test("Ctrl+wheel over the tracks zooms centered on the cursor", async ({ page }) => {
    await startApp(page);
    // Clip at start 0: its left edge sits at beat 0. Pinning the cursor on that
    // edge means the clip's screen-x must not move after zoom (the beat under
    // the cursor = clip.startBeat is preserved, and the clip's left edge lives
    // at exactly that beat).
    const c1 = await addMidiClip(page, { trackIndex: 0, start: 0, duration: 4, name: "Centered" });
    const clipBoxBefore = await clipLocator(page, c1).boundingBox();
    if (!clipBoxBefore) throw new Error("clip has no bounding box");

    // Park the cursor on the clip's left edge, vertically centered.
    const cursorX = clipBoxBefore.x + 1;
    const cursorY = clipBoxBefore.y + clipBoxBefore.height / 2;
    await page.mouse.move(cursorX, cursorY);

    // Beat under the cursor before zoom (in tracks-content coords).
    const geoBefore = await tracksGeometry(page);
    const cursorOffsetBefore = cursorX - geoBefore.left;
    const beatBefore = (cursorOffsetBefore + geoBefore.scrollLeft) / (await readPps(page));

    await page.keyboard.down("Control");
    await page.mouse.wheel(0, -100);
    await page.keyboard.up("Control");

    // Let the layout effect + re-render settle.
    await page.waitForTimeout(120);

    const ppsAfter = await readPps(page);
    const geoAfter = await tracksGeometry(page);
    const beatAfter = (cursorOffsetBefore + geoAfter.scrollLeft) / ppsAfter;

    // G2 contract: the beat under the cursor is preserved.
    expect(Math.abs(beatAfter - beatBefore)).toBeLessThan(0.05);

    // And therefore the clip's left edge (at beat 0) stays anchored at the cursor.
    const clipBoxAfter = await clipLocator(page, c1).boundingBox();
    if (!clipBoxAfter) throw new Error("clip has no bounding box after zoom");
    expect(Math.abs(clipBoxAfter.x - clipBoxBefore.x)).toBeLessThan(3);
  });

  test("Ctrl+Alt+drag on the ruler marquee-zooms to the dragged beat range", async ({ page }) => {
    await startApp(page);
    await addMidiClip(page, { trackIndex: 0, start: 0, duration: 4, name: "MarqueeAnchor" });

    const rulerBox = await page.locator(".tl-ruler").boundingBox();
    if (!rulerBox) throw new Error("ruler has no bounding box");

    const ppsBefore = await readPps(page);
    const geo = await tracksGeometry(page);
    const viewportWidth = geo.width;

    // Drag exactly 10 beats across the ruler (pixels = 10 * ppsBefore).
    const dragBeats = 10;
    const dragPixels = dragBeats * ppsBefore;
    const startX = rulerBox.x + 20;
    const endX = startX + dragPixels;
    const y = rulerBox.y + rulerBox.height / 2;

    await page.keyboard.down("Control");
    await page.keyboard.down("Alt");
    await page.mouse.move(startX, y);
    await page.mouse.down();
    await page.mouse.move(endX, y, { steps: 10 });
    await page.mouse.up();
    await page.keyboard.up("Alt");
    await page.keyboard.up("Control");

    // Expected: viewport / dragBeats, clamped to [MIN_PPS, MAX_PPS].
    const expected = Math.min(200, Math.max(10, viewportWidth / dragBeats));
    const ppsAfter = await readPps(page);
    // Allow small float drift + the wheel/clamp rounding.
    expect(Math.abs(ppsAfter - expected)).toBeLessThan(2);
  });

  test("plain wheel over the tracks does NOT zoom (native scroll passthrough)", async ({ page }) => {
    await startApp(page);
    await addMidiClip(page, { trackIndex: 0, start: 0, duration: 4, name: "NoZoom" });

    const tracksBox = await page.locator(".tl-tracks").boundingBox();
    if (!tracksBox) throw new Error("tracks has no bounding box");

    const ppsBefore = await readPps(page);
    await page.mouse.move(tracksBox.x + tracksBox.width / 2, tracksBox.y + 50);
    await page.mouse.wheel(0, -100);
    await page.waitForTimeout(120);
    const ppsAfter = await readPps(page);
    expect(ppsAfter).toBe(ppsBefore);
  });

  test("Shift+wheel over the tracks does NOT zoom (horizontal scroll passthrough)", async ({ page }) => {
    await startApp(page);
    await addMidiClip(page, { trackIndex: 0, start: 0, duration: 4, name: "ShiftNoZoom" });

    const tracksBox = await page.locator(".tl-tracks").boundingBox();
    if (!tracksBox) throw new Error("tracks has no bounding box");

    const ppsBefore = await readPps(page);
    await page.keyboard.down("Shift");
    await page.mouse.move(tracksBox.x + tracksBox.width / 2, tracksBox.y + 50);
    await page.mouse.wheel(0, -100);
    await page.keyboard.up("Shift");
    await page.waitForTimeout(120);
    const ppsAfter = await readPps(page);
    expect(ppsAfter).toBe(ppsBefore);
  });
});
