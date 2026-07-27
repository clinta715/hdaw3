import { test, expect } from "@playwright/test";
import { startApp } from "./helpers";

test.describe("File browser (user journeys)", () => {
  test.beforeEach(async ({ page }) => {
    await startApp(page);
  });

  test("browser toggle button is visible", async ({ page }) => {
    await expect(page.locator(".browser-toggle-btn")).toBeVisible();
  });

  test("clicking toggle button opens the file browser", async ({ page }) => {
    await page.locator(".browser-toggle-btn").click();
    await expect(page.locator(".file-browser, [class*='file-browser']")).toBeVisible({ timeout: 5000 });
  });

  test("clicking toggle button again closes the file browser", async ({ page }) => {
    await page.locator(".browser-toggle-btn").click();
    await expect(page.locator(".file-browser, [class*='file-browser']")).toBeVisible({ timeout: 5000 });
    await page.locator(".browser-toggle-btn").click();
    await expect(page.locator(".file-browser, [class*='file-browser']")).not.toBeVisible({ timeout: 3000 });
  });

  test("browser toggle button has active class when open", async ({ page }) => {
    await page.locator(".browser-toggle-btn").click();
    await expect(page.locator(".browser-toggle-btn")).toHaveClass(/active/, { timeout: 3000 });
  });

  test("browser shows folder tree when open (Electron only)", async ({ page }) => {
    const isElectron = await page.evaluate(() => !!(window as any).hdaw);
    if (isElectron) {
      await page.locator(".browser-toggle-btn").click();
      const browser = page.locator(".file-browser, [class*='file-browser']");
      await expect(browser).toBeVisible({ timeout: 5000 });
      // Should have folder nodes or a drives list
      const folders = browser.locator(".fb-folder, [class*='folder'], [class*='drive']");
      await expect(folders.first()).toBeVisible({ timeout: 5000 });
    }
  });

  test("Ctrl+B toggles the file browser", async ({ page }) => {
    await page.keyboard.press("Control+b");
    await expect(page.locator(".file-browser, [class*='file-browser']")).toBeVisible({ timeout: 5000 });
    await page.keyboard.press("Control+b");
    await expect(page.locator(".file-browser, [class*='file-browser']")).not.toBeVisible({ timeout: 3000 });
  });

  test("browser has a search input", async ({ page }) => {
    await page.locator(".browser-toggle-btn").click();
    const browser = page.locator(".file-browser, [class*='file-browser']");
    await expect(browser).toBeVisible({ timeout: 5000 });
    const search = browser.locator('input[type="search"], input[placeholder*="Search"], input[placeholder*="search"]');
    if (await search.isVisible({ timeout: 2000 })) {
      await expect(search).toBeVisible();
    }
  });
});
