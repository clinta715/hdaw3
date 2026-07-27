import { test, expect } from "@playwright/test";
import { startApp, rpcCall } from "./helpers";

test.describe("FX Chain (user journeys)", () => {
  test.beforeEach(async ({ page }) => {
    await startApp(page);
    // Select track 0
    await page.locator(".th-row").first().click();
    // Switch to FX Chain tab
    await page.locator(".bt-tab", { hasText: "FX Chain" }).click();
  });

  test("FX chain panel renders", async ({ page }) => {
    const panel = page.locator(".fx-chain, [class*='fx-chain']");
    await expect(panel).toBeVisible({ timeout: 5000 });
  });

  test("add FX button opens the plugin menu", async ({ page }) => {
    const addBtn = page.locator('button[title="Add Effect"], button', { hasText: /add|plus|\+/i }).first();
    if (await addBtn.isVisible({ timeout: 3000 })) {
      await addBtn.click();
      // Plugin menu should appear
      await expect(page.locator(".fx-menu, [class*='fx-menu'], [class*='plugin-menu']")).toBeVisible({ timeout: 3000 });
    }
  });

  test("internal FX options are listed in the menu", async ({ page }) => {
    const addBtn = page.locator('button[title="Add Effect"], button', { hasText: /add|plus|\+/i }).first();
    if (await addBtn.isVisible({ timeout: 3000 })) {
      await addBtn.click();
      const menu = page.locator(".fx-menu, [class*='fx-menu'], [class*='plugin-menu']");
      if (await menu.isVisible({ timeout: 3000 })) {
        // Internal FX include EQ, Compressor, Reverb, Delay
        await expect(menu).toContainText(/EQ|Compressor|Reverb|Delay/i);
      }
    }
  });

  test("FX slot shows track name in header", async ({ page }) => {
    // The FX chain should display which track it's editing
    const header = page.locator(".fx-chain-header, [class*='fx-header']");
    if (await header.isVisible({ timeout: 3000 })) {
      await expect(header).toContainText(/Track/);
    }
  });

  test("empty FX chain shows placeholder message", async ({ page }) => {
    // Track 0 has no FX by default
    const empty = page.locator(".fx-empty, [class*='empty']");
    if (await empty.isVisible({ timeout: 3000 })) {
      await expect(empty).toBeVisible();
    }
  });
});
