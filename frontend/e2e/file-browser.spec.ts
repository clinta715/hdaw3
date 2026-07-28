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
    await expect(page.locator("aside.file-browser")).toBeVisible({ timeout: 5000 });
  });

  test("clicking toggle button again closes the file browser", async ({ page }) => {
    await page.locator(".browser-toggle-btn").click();
    await expect(page.locator("aside.file-browser")).toBeVisible({ timeout: 5000 });
    await page.locator(".browser-toggle-btn").click();
    await expect(page.locator("aside.file-browser")).not.toBeVisible({ timeout: 5000 });
  });

  test("browser toggle button has active class when open", async ({ page }) => {
    await page.locator(".browser-toggle-btn").click();
    await expect(page.locator(".browser-toggle-btn")).toHaveClass(/active/, { timeout: 5000 });
  });

  test("Ctrl+B toggles the file browser", async ({ page }) => {
    await page.keyboard.press("Control+b");
    await expect(page.locator("aside.file-browser")).toBeVisible({ timeout: 5000 });
    await page.keyboard.press("Control+b");
    await expect(page.locator("aside.file-browser")).not.toBeVisible({ timeout: 5000 });
  });

  test("browser has a search input", async ({ page }) => {
    await page.locator(".browser-toggle-btn").click();
    await expect(page.locator("aside.file-browser")).toBeVisible({ timeout: 5000 });
    const search = page.locator(".fb-search-input");
    await expect(search).toBeVisible({ timeout: 5000 });
  });
});
