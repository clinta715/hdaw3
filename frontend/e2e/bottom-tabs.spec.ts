import { test, expect } from "@playwright/test";
import { startApp } from "./helpers";

test.describe("Bottom tabs (user journeys)", () => {
  test.beforeEach(async ({ page }) => {
    await startApp(page);
  });

  test("bottom panel renders with tab bar", async ({ page }) => {
    await expect(page.locator(".bottom-tabs")).toBeVisible({ timeout: 5000 });
    await expect(page.locator(".bt-tab-bar")).toBeVisible();
  });

  test("default tab is Mixer", async ({ page }) => {
    const mixerTab = page.locator(".bt-tab", { hasText: "Mixer" });
    await expect(mixerTab).toHaveClass(/bt-tab--active/, { timeout: 5000 });
  });

  test("clicking a tab switches the visible content", async ({ page }) => {
    const pianoTab = page.locator(".bt-tab", { hasText: "Piano Roll" });
    await pianoTab.click();
    await expect(pianoTab).toHaveClass(/bt-tab--active/, { timeout: 5000 });
    const mixerTab = page.locator(".bt-tab", { hasText: "Mixer" });
    await expect(mixerTab).not.toHaveClass(/bt-tab--active/, { timeout: 5000 });
  });

  test("all expected tabs are present", async ({ page }) => {
    const expected = ["Mixer", "Piano Roll", "Automation", "FX Chain", "MIDI FX", "Audio Editor", "Modulation", "Step Seq"];
    for (const label of expected) {
      await expect(page.locator(".bt-tab", { hasText: label })).toBeVisible();
    }
  });

  test("switching tabs updates the content panel", async ({ page }) => {
    await page.locator(".bt-tab", { hasText: "FX Chain" }).click();
    await expect(page.locator(".bt-content")).toBeVisible({ timeout: 5000 });
  });

  test("switching back to Mixer tab shows mixer content", async ({ page }) => {
    // Go to Piano Roll first
    await page.locator(".bt-tab", { hasText: "Piano Roll" }).click();
    await expect(page.locator(".bt-tab", { hasText: "Piano Roll" })).toHaveClass(/bt-tab--active/, { timeout: 5000 });

    // Switch back to Mixer
    await page.locator(".bt-tab", { hasText: "Mixer" }).click();
    await expect(page.locator(".bt-tab", { hasText: "Mixer" })).toHaveClass(/bt-tab--active/, { timeout: 5000 });
    await expect(page.locator(".mixer-strips").first()).toBeVisible({ timeout: 5000 });
  });

  test("tab bar remains visible when scrolling the timeline", async ({ page }) => {
    const timeline = page.locator(".tl-tracks");
    if (await timeline.isVisible()) {
      await timeline.evaluate((el) => { el.scrollTop = 200; });
    }
    await expect(page.locator(".bt-tab-bar")).toBeVisible();
  });
});
