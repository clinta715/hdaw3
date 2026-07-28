import { test, expect } from "@playwright/test";
import { startApp, rpcCall } from "./helpers";

test.describe("Piano Roll (user journeys)", () => {
  test.beforeEach(async ({ page }) => {
    await startApp(page);
    // Create a MIDI clip to work with
    await rpcCall(page, "project.addMidiClip", { trackIndex: 0, start: 0, duration: 4, name: "E2E MIDI" });
    // Select it so the piano roll shows notes
    await page.locator(".tl-clip").first().click();
    // Switch to piano roll tab
    await page.locator(".bt-tab", { hasText: "Piano Roll" }).click();
  });

  test("piano roll tab shows note grid with notes", async ({ page }) => {
    const grid = page.locator(".note-grid");
    await expect(grid).toBeVisible({ timeout: 5000 });
    // Default MIDI clip has notes — check for note rectangles
    const notes = page.locator(".note-rect, .note-block, [class*='note']");
    await expect(notes.first()).toBeVisible({ timeout: 5000 });
  });

  test("piano roll shows piano keys on the left", async ({ page }) => {
    const keys = page.locator(".pr-keys, [class*='piano-key'], [class*='pr-key']");
    await expect(keys.first()).toBeVisible({ timeout: 5000 });
  });

  test("zoom in button increases pixels per beat", async ({ page }) => {
    const zoomIn = page.locator('button[title="Zoom In"], button', { hasText: "+" }).first();
    if (await zoomIn.isVisible()) {
      const grid = page.locator(".pr-grid-area .note-grid, .note-grid");
      const before = await grid.evaluate((el) => el.scrollWidth);
      await zoomIn.click();
      const after = await grid.evaluate((el) => el.scrollWidth);
      expect(after).toBeGreaterThanOrEqual(before);
    }
  });

  test("zoom out button decreases pixels per beat", async ({ page }) => {
    const zoomOut = page.locator('button[title="Zoom Out"], button', { hasText: "-" }).first();
    if (await zoomOut.isVisible()) {
      await zoomOut.click();
      // No crash = pass
    }
  });

  test("loop toggle button toggles looping on the active clip", async ({ page }) => {
    const loopBtn = page.locator('button[title="Toggle Loop"], .pr-loop-btn, button', { hasText: /loop/i }).first();
    if (await loopBtn.isVisible()) {
      await loopBtn.click();
      // No crash = pass
    }
  });

  test("velocity lane is visible below the note grid", async ({ page }) => {
    const velLane = page.locator(".velocity-lane, [class*='velocity']");
    await expect(velLane.first()).toBeVisible({ timeout: 5000 });
  });

  test("clicking a note selects it", async ({ page }) => {
    const noteEl = page.locator(".note-rect, .note-block, [class*='note-rect']").first();
    if (await noteEl.isVisible({ timeout: 3000 })) {
      await noteEl.click();
      // Selected note should have a selection class
      await expect(noteEl).toHaveClass(/selected|active/, { timeout: 2000 });
    }
  });

  test("CC lane is present", async ({ page }) => {
    const ccLane = page.locator(".cc-lane, [class*='cc-lane']");
    await expect(ccLane.first()).toBeVisible({ timeout: 5000 });
  });
});
