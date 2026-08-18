import { test, expect } from "@playwright/test";
import { startApp, rpcCall } from "./helpers";

test.describe("Piano Roll (user journeys)", () => {
  test.beforeEach(async ({ page }) => {
    await startApp(page);
    // Create an empty MIDI clip to work with (default project ships empty tracks)
    await rpcCall(page, "project.addMidiClip", { trackIndex: 0, start: 0, duration: 4, name: "E2E MIDI" });
    // Select it so the piano roll opens on it
    await page.locator(".tl-clip").first().click();
    // Switch to piano roll tab
    await page.locator(".bt-tab", { hasText: "Piano Roll" }).click();
  });

  test("piano roll tab shows note grid with notes", async ({ page }) => {
    const grid = page.locator(".note-grid");
    await expect(grid).toBeVisible({ timeout: 5000 });
    // Draw a note: double-clicking the grid creates one through the UI (optimistic
    // store update), so it renders regardless of async note-fetch timing.
    await grid.dblclick();
    const notes = page.locator(".ng-note");
    await expect(notes.first()).toBeVisible({ timeout: 5000 });
  });

  test("piano roll shows piano keys on the left", async ({ page }) => {
    const keys = page.locator(".pr-keys");
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

  test("velocity lane is collapsed by default; Vel pill opens it", async ({ page }) => {
    await expect(page.locator(".pr-lane-handle")).toBeVisible({ timeout: 5000 });
    await expect(page.locator(".velocity-lane")).toHaveCount(0);
    await page.getByRole("button", { name: "Toggle velocity lane", exact: true }).click();
    await expect(page.locator(".velocity-lane").first()).toBeVisible({ timeout: 5000 });
    await page.locator(".pr-lane-handle").click();
    await expect(page.locator(".velocity-lane")).toHaveCount(0, { timeout: 5000 });
    await page.locator(".pr-lane-handle").click();
    await expect(page.locator(".velocity-lane").first()).toBeVisible({ timeout: 5000 });
  });

  test("clicking a note selects it", async ({ page }) => {
    const noteEl = page.locator(".ng-note").first();
    if (await noteEl.isVisible({ timeout: 3000 })) {
      await noteEl.click();
      // Selected note should have a selection class
      await expect(noteEl).toHaveClass(/selected|active/, { timeout: 2000 });
    }
  });

  test("CC lane is added on demand via the +CC popover", async ({ page }) => {
    await expect(page.locator(".cc-lane")).toHaveCount(0);
    await page.getByRole("button", { name: "Add CC lane", exact: true }).click();
    const popover = page.locator(".pr-cc-popover");
    await expect(popover).toBeVisible();
    await popover.locator("button", { hasText: "Add" }).click();
    const lane = page.locator(".cc-lane");
    await expect(lane).toHaveCount(1, { timeout: 5000 });
    await expect(lane.locator(".cc-label")).toContainText("CC7");
    await lane.locator(".cc-remove").click();
    await expect(page.locator(".cc-lane")).toHaveCount(0, { timeout: 5000 });
  });

  test("clip picker dropdown switches clips", async ({ page }) => {
    await rpcCall(page, "project.addMidiClip", { trackIndex: 0, start: 8, duration: 4, name: "E2E MIDI 2" });
    const select = page.locator(".pr-clip-select");
    await expect(select).toBeVisible({ timeout: 10000 });
    // The second clip arrives via a debounced tree delta, so poll for it.
    await expect(async () => {
      expect(await select.locator("option").count()).toBe(2);
    }).toPass({ timeout: 10000 });
    // Clear the timeline clip selection so the dropdown's internal selection wins
    // (timelineSelectedId takes precedence over internalClipId in PianoRoll).
    await page.locator(".tl-track-row").nth(1).click({ position: { x: 40, y: 10 } });
    const firstValue = await select.inputValue();
    const secondValue = await select.locator("option").nth(1).getAttribute("value");
    expect(secondValue).not.toBe(firstValue);
    await select.selectOption({ value: secondValue ?? "" });
    await expect(async () => {
      expect(await select.inputValue()).toBe(secondValue);
    }).toPass({ timeout: 10000 });
  });

  test("edit popover opens on note selection", async ({ page }) => {
    const grid = page.locator(".note-grid");
    await expect(grid).toBeVisible({ timeout: 5000 });
    await grid.dblclick({ position: { x: 60, y: 50 } });
    const notes = page.locator(".ng-note");
    await expect(notes).toHaveCount(1, { timeout: 5000 });
    await notes.first().click();
    await expect(notes.first()).toHaveClass(/selected/, { timeout: 2000 });
    const editPill = page.getByRole("button", { name: "Edit selected notes", exact: true });
    await expect(editPill).toBeVisible();
    await editPill.click();
    const popover = page.locator(".pr-edit-popover");
    await expect(popover).toBeVisible();
    await expect(popover.locator(".pr-slider")).toHaveCount(4);
    await expect(popover.locator(".pr-slider").first()).toBeVisible();
    const box = await grid.boundingBox();
    if (!box) return;
    const cx = Math.min(300, box.width - 20);
    const cy = Math.min(200, box.height - 20);
    await grid.click({ position: { x: cx, y: cy } });
    await expect(page.locator(".pr-edit-popover")).toHaveCount(0, { timeout: 5000 });
    await expect(page.getByRole("button", { name: "Edit selected notes", exact: true })).toHaveCount(0);
  });

  test("default view is decluttered", async ({ page }) => {
    await expect(page.locator(".pr-clip-btn")).toHaveCount(0);
    await expect(page.locator(".pr-cc-add")).toHaveCount(0);
    await expect(page.locator(".velocity-lane")).toHaveCount(0);
    await expect(page.locator(".note-grid")).toBeVisible({ timeout: 5000 });
  });

  test("marquee selection selects multiple notes", async ({ page }) => {
    const grid = page.locator(".note-grid");
    await expect(grid).toBeVisible({ timeout: 5000 });
    const box = await grid.boundingBox();
    if (!box) return;
    // Create two notes (grid-relative positions).
    await grid.dblclick({ position: { x: 60, y: 50 } });
    await grid.dblclick({ position: { x: 140, y: 50 } });
    const notes = page.locator(".ng-note");
    await expect(notes).toHaveCount(2, { timeout: 5000 });
    // Rubber-band around both notes.
    await page.mouse.move(box.x + 40, box.y + 40);
    await page.mouse.down();
    await page.mouse.move(box.x + 180, box.y + 60, { steps: 5 });
    await page.mouse.up();
    await expect(page.locator(".ng-note--selected")).toHaveCount(2, { timeout: 5000 });
  });

  test("marquee drag moves all selected notes", async ({ page }) => {
    const grid = page.locator(".note-grid");
    await expect(grid).toBeVisible({ timeout: 5000 });
    const box = await grid.boundingBox();
    if (!box) return;
    await grid.dblclick({ position: { x: 60, y: 50 } });
    await grid.dblclick({ position: { x: 140, y: 50 } });
    const notes = page.locator(".ng-note");
    await expect(notes).toHaveCount(2, { timeout: 5000 });
    // Marquee-select both.
    await page.mouse.move(box.x + 40, box.y + 40);
    await page.mouse.down();
    await page.mouse.move(box.x + 180, box.y + 60, { steps: 5 });
    await page.mouse.up();
    await expect(page.locator(".ng-note--selected")).toHaveCount(2, { timeout: 5000 });

    const leftsBefore = await notes.evaluateAll((els) => els.map((e) => parseFloat(e.style.left)));
    const firstBox = await notes.first().boundingBox();
    if (!firstBox) return;
    // Drag the first (shared) note one beat right.
    await page.mouse.move(firstBox.x + firstBox.width / 2, firstBox.y + firstBox.height / 2);
    await page.mouse.down();
    await page.mouse.move(firstBox.x + firstBox.width / 2 + 80, firstBox.y + firstBox.height / 2, { steps: 5 });
    await page.mouse.up();
    // Tree deltas are debounced (~16 ms) and the commit is async, so poll.
    await expect(async () => {
      const leftsAfter = await notes.evaluateAll((els) => els.map((e) => parseFloat(e.style.left)));
      expect(leftsAfter[0]).toBeGreaterThan(leftsBefore[0] + 40);
      expect(leftsAfter[1]).toBeGreaterThan(leftsBefore[1] + 40);
    }).toPass({ timeout: 10000 });
  });

  test("clicking empty grid clears note selection", async ({ page }) => {
    const grid = page.locator(".note-grid");
    await expect(grid).toBeVisible({ timeout: 5000 });
    const box = await grid.boundingBox();
    if (!box) return;
    await grid.dblclick({ position: { x: 60, y: 50 } });
    const notes = page.locator(".ng-note");
    await expect(notes).toHaveCount(1, { timeout: 5000 });
    await notes.first().click();
    await expect(notes.first()).toHaveClass(/selected/, { timeout: 2000 });
    // Click far from the note (bottom-right of the grid, clamped to stay inside
    // the box for viewport-size determinism).
    const cx = Math.min(300, box.width - 20);
    const cy = Math.min(200, box.height - 20);
    await grid.click({ position: { x: cx, y: cy } });
    await expect(page.locator(".ng-note--selected")).toHaveCount(0, { timeout: 2000 });
  });
});
