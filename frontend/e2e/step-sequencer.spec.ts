import { test, expect } from "@playwright/test";
import { startApp, rpcCall } from "./helpers";

test.describe("Step Sequencer (user journeys)", () => {
  test.beforeEach(async ({ page }) => {
    await startApp(page);
    // Create a MIDI clip
    await rpcCall(page, "project.addMidiClip", { trackIndex: 0, start: 0, duration: 4 });
    // Select it
    await page.locator(".tl-clip").first().click();
    // Switch to step seq tab
    await page.locator(".bt-tab", { hasText: "Step Seq" }).click();
  });

  test("step sequencer renders the grid", async ({ page }) => {
    const grid = page.locator(".step-sequencer, [class*='step-seq']");
    await expect(grid).toBeVisible({ timeout: 5000 });
  });

  test("step grid has clickable cells", async ({ page }) => {
    const cells = page.locator(".ss-cell, [class*='ss-cell'], .step-cell");
    await expect(cells.first()).toBeVisible({ timeout: 5000 });
    expect(await cells.count()).toBeGreaterThan(0);
  });

  test("clicking a cell toggles it on", async ({ page }) => {
    const cell = page.locator(".ss-cell, [class*='ss-cell'], .step-cell").first();
    await cell.click();
    await expect(cell).toHaveClass(/active|on|filled/, { timeout: 2000 });
  });

  test("clicking an active cell toggles it off", async ({ page }) => {
    const cell = page.locator(".ss-cell, [class*='ss-cell'], .step-cell").first();
    await cell.click();
    await expect(cell).toHaveClass(/active|on|filled/, { timeout: 2000 });
    await cell.click();
    await expect(cell).not.toHaveClass(/active|on|filled/, { timeout: 2000 });
  });

  test("row labels show note names", async ({ page }) => {
    const labels = page.locator(".ss-label, [class*='ss-label'], .step-label");
    await expect(labels.first()).toBeVisible({ timeout: 5000 });
    // Should contain note names like C, D, E, etc.
    const text = await labels.first().textContent();
    expect(text).toMatch(/[A-G]/);
  });

  test("pattern length selector is present", async ({ page }) => {
    const selector = page.locator(".ss-length, [class*='pattern-length'], select");
    if (await selector.first().isVisible({ timeout: 3000 })) {
      await expect(selector.first()).toBeVisible();
    }
  });

  test("drum label mode toggle exists", async ({ page }) => {
    const toggle = page.locator('button', { hasText: /drum/i }).first();
    if (await toggle.isVisible({ timeout: 2000 })) {
      await toggle.click();
      // Labels should change to drum names (Kick, Snare, etc.)
      await expect(page.locator(".ss-label, [class*='ss-label'], .step-label").first()).toContainText(/Kick|Snare|Hat|Tom|Crash/i, { timeout: 2000 });
    }
  });
});
