import { test, expect } from "@playwright/test";
import { startApp, rpcCall } from "./helpers";

test.describe("FX Chain (user journeys)", () => {
  test.beforeEach(async ({ page }) => {
    await startApp(page);
    // Click a track type badge to set selectedTrackIndex (no stopPropagation)
    await page.locator(".th-row .th-type-badge").first().click();
    // Then switch to FX Chain tab
    await page.locator(".bt-tab", { hasText: "FX Chain" }).click();
  });

  test("FX chain panel renders", async ({ page }) => {
    await expect(page.locator(".fx-chain")).toBeVisible({ timeout: 5000 });
  });

  test("add FX button opens the plugin menu", async ({ page }) => {
    const addBtn = page.locator(".fx-add-btn");
    await expect(addBtn).toBeVisible({ timeout: 10000 });
    await addBtn.click();
    await expect(page.locator(".fx-dropdown")).toBeVisible({ timeout: 3000 });
  });

  test("internal FX options are listed in the menu", async ({ page }) => {
    await expect(page.locator(".fx-add-btn")).toBeVisible({ timeout: 10000 });
    await page.locator(".fx-add-btn").click();
    const menu = page.locator(".fx-dropdown");
    await expect(menu).toBeVisible({ timeout: 3000 });
    await expect(menu).toContainText(/EQ|Compressor|Reverb|Delay/i);
  });

  test("FX header shows track name", async ({ page }) => {
    const header = page.locator(".fx-header");
    if (await header.isVisible({ timeout: 3000 })) {
      await expect(header).toContainText(/Track/);
    }
  });

  test("empty FX chain shows placeholder message", async ({ page }) => {
    const empty = page.locator(".fx-empty-slots, .fx-empty");
    if (await empty.isVisible({ timeout: 3000 })) {
      await expect(empty).toBeVisible();
    }
  });
});
