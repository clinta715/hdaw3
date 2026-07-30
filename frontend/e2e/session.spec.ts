import { test, expect } from "@playwright/test";
import { startApp } from "./helpers";

test.describe("Session View", () => {
  test("can switch to session view", async ({ page }) => {
    await startApp(page);

    // Switch to session view
    const toggle = page.locator('button:has-text("Arr")');
    await toggle.click();

    // Session view should be visible
    await expect(page.locator(".sv-root")).toBeVisible();
    await expect(page.locator(".sv-scene-btn").first()).toBeVisible();
  });

  test("can create a clip in session slot", async ({ page }) => {
    await startApp(page);

    // Switch to session view
    await page.locator('button:has-text("Arr")').click();
    await expect(page.locator(".sv-root")).toBeVisible();

    // Click an empty slot to create a clip
    const slot = page.locator(".sv-slot").first();
    await slot.click();

    // Slot should now be filled
    await expect(slot).toHaveClass(/sv-slot--filled/);
  });

  test("scene button launches scene", async ({ page }) => {
    await startApp(page);

    // Switch to session view
    await page.locator('button:has-text("Arr")').click();

    // Create a clip first
    await page.locator(".sv-slot").first().click();
    await expect(page.locator(".sv-slot--filled").first()).toBeVisible();

    // Launch scene 1
    await page.locator(".sv-scene-btn").first().click();

    // Scene button should become active
    await expect(page.locator(".sv-scene-btn--active").first()).toBeVisible({ timeout: 5000 });
  });
});
