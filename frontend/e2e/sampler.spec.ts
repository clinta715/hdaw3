import { test, expect } from "@playwright/test";
import { startApp, rpcCall } from "./helpers";

test.describe("Sampler", () => {
  test("sampler tab renders and shows controls", async ({ page }) => {
    await startApp(page);
    // Select track 0 so selectedTrackIndex is set (SamplerEditor reads it)
    await page.locator(".th-row .th-type-badge").first().click();

    await rpcCall(page, "project.addFxSlot", {
      trackIndex: 0,
      type: "sampler",
      position: -1,
    });

    const samplerTab = page.locator('button:has-text("Sampler")');
    if (await samplerTab.isVisible()) {
      await samplerTab.click();
    }

    await expect(async () => {
      const editor = page.locator(".sampler-editor");
      await expect(editor).toBeVisible();
    }).toPass({ timeout: 5000 });

    const modeSelect = page.locator(".sampler-editor__select");
    await expect(modeSelect).toBeVisible();

    const sliders = page.locator(".sampler-editor__slider");
    await expect(sliders).toHaveCount(4);
  });
});
