import { test, expect } from "@playwright/test";
import { startApp } from "./helpers";

// ModulationPanel is driven by the selected track. Track 0 ("Track 1") in the
// default project has no LFOs, so it's a clean starting state. We select a
// track by clicking its type badge (avoids the .th-color stopPropagation that
// opens the native color picker).
test.describe("Modulation panel (user journeys)", () => {
  test.beforeEach(async ({ page }) => {
    await startApp(page);
    // Select track 0 so the panel has a target track.
    await page.locator(".th-row").first().locator(".th-type-badge").click();
    // Switch to the Modulation tab.
    await page.locator(".bt-tab", { hasText: "Modulation" }).click();
    await expect(page.locator(".modulation-panel")).toBeVisible({ timeout: 5000 });
  });

  test("panel shows the selected track name in the header", async ({ page }) => {
    await expect(page.locator(".mod-title")).toContainText("Track 1");
  });

  test("empty state is shown when the track has no LFOs", async ({ page }) => {
    await expect(page.locator(".mod-empty")).toBeVisible({ timeout: 5000 });
    await expect(page.locator(".mod-empty")).toContainText(/No LFOs/i);
    await expect(page.locator(".mod-lfo-card")).toHaveCount(0);
  });

  test("add LFO button creates an LFO card", async ({ page }) => {
    await page.locator(".mod-add-btn").click();
    await expect(page.locator(".mod-lfo-card")).toHaveCount(1, { timeout: 5000 });
    await expect(page.locator(".mod-empty")).toHaveCount(0);
  });

  test("new LFO has default name and waveform", async ({ page }) => {
    await page.locator(".mod-add-btn").click();
    const card = page.locator(".mod-lfo-card").first();
    await expect(card).toBeVisible({ timeout: 5000 });
    await expect(card.locator(".mod-lfo-name")).toContainText("LFO 1");
    // Default waveform is Sine (index 0).
    await expect(card.locator("select")).toHaveValue("0");
  });

  test("adding multiple LFOs renders one card each", async ({ page }) => {
    await page.locator(".mod-add-btn").click();
    await expect(page.locator(".mod-lfo-card")).toHaveCount(1, { timeout: 5000 });
    await page.locator(".mod-add-btn").click();
    await expect(page.locator(".mod-lfo-card")).toHaveCount(2, { timeout: 5000 });
  });

  test("remove button deletes the LFO card", async ({ page }) => {
    await page.locator(".mod-add-btn").click();
    await expect(page.locator(".mod-lfo-card")).toHaveCount(1, { timeout: 5000 });
    await page.locator(".mod-lfo-card").first().locator(".mod-remove-btn").click();
    await expect(page.locator(".mod-lfo-card")).toHaveCount(0, { timeout: 5000 });
    await expect(page.locator(".mod-empty")).toBeVisible({ timeout: 5000 });
  });

  test("changing waveform select updates optimistically", async ({ page }) => {
    await page.locator(".mod-add-btn").click();
    const select = page.locator(".mod-lfo-card").first().locator("select");
    await expect(select).toBeVisible({ timeout: 5000 });
    await select.selectOption("2"); // Saw
    await expect(select).toHaveValue("2");
  });

  test("toggling bipolar checkbox updates optimistically", async ({ page }) => {
    await page.locator(".mod-add-btn").click();
    const card = page.locator(".mod-lfo-card").first();
    await expect(card).toBeVisible({ timeout: 5000 });
    // Bipolar is the first checkbox; default (new LFO) is unchecked.
    const bipolar = card.locator(".mod-checkbox input[type='checkbox']").first();
    await expect(bipolar).not.toBeChecked();
    await bipolar.check();
    await expect(bipolar).toBeChecked();
  });

  test("enabled checkbox defaults to checked and can be toggled off", async ({ page }) => {
    await page.locator(".mod-add-btn").click();
    const card = page.locator(".mod-lfo-card").first();
    await expect(card).toBeVisible({ timeout: 5000 });
    // Enabled is the second checkbox; default (new LFO) is checked.
    const enabled = card.locator(".mod-checkbox input[type='checkbox']").nth(1);
    await expect(enabled).toBeChecked();
    await enabled.uncheck();
    await expect(enabled).not.toBeChecked();
  });

  test("rate slider reflects its value in the Hz readout", async ({ page }) => {
    await page.locator(".mod-add-btn").click();
    const card = page.locator(".mod-lfo-card").first();
    await expect(card).toBeVisible({ timeout: 5000 });
    // Default rate is 1.0 Hz.
    await expect(card.locator("label", { hasText: "Rate" })).toContainText("1.0 Hz");
  });

  test("depth slider reflects its value as a percentage", async ({ page }) => {
    await page.locator(".mod-add-btn").click();
    const card = page.locator(".mod-lfo-card").first();
    await expect(card).toBeVisible({ timeout: 5000 });
    // Default depth is 0.3 -> 30%.
    await expect(card.locator("label", { hasText: "Depth" })).toContainText("30%");
  });
});
