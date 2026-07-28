import { test, expect } from "@playwright/test";
import { startApp } from "./helpers";

test.describe("Preferences dialog (user journeys)", () => {
  test.beforeEach(async ({ page }) => {
    await startApp(page);
  });

  test("clicking preferences button opens the dialog", async ({ page }) => {
    await page.locator('button[title="Preferences"]').click();
    await expect(page.locator(".preferences-dialog")).toBeVisible({ timeout: 5000 });
  });

  test("dialog shows audio device settings", async ({ page }) => {
    await page.locator('button[title="Preferences"]').click();
    const dialog = page.locator(".preferences-dialog");
    await expect(dialog).toBeVisible({ timeout: 5000 });
    await expect(dialog).toContainText(/Sample Rate|Buffer|Device|Audio/i);
  });

  test("dialog shows MIDI device section", async ({ page }) => {
    await page.locator('button[title="Preferences"]').click();
    const dialog = page.locator(".preferences-dialog");
    await expect(dialog).toBeVisible({ timeout: 5000 });
    await expect(dialog).toContainText(/MIDI/i);
  });

  test("dialog has a close button", async ({ page }) => {
    await page.locator('button[title="Preferences"]').click();
    const dialog = page.locator(".preferences-dialog");
    await expect(dialog).toBeVisible({ timeout: 5000 });
    const closeBtn = dialog.locator("button.pref-close");
    await expect(closeBtn).toBeVisible({ timeout: 2000 });
    await closeBtn.click();
    await expect(dialog).not.toBeVisible({ timeout: 3000 });
  });

  test("clicking overlay closes the dialog", async ({ page }) => {
    await page.locator('button[title="Preferences"]').click();
    await expect(page.locator(".preferences-dialog")).toBeVisible({ timeout: 5000 });
    await page.locator(".modal-overlay").click({ position: { x: 5, y: 5 } });
    await expect(page.locator(".preferences-dialog")).not.toBeVisible({ timeout: 3000 });
  });
});

test.describe("Export dialog (user journeys)", () => {
  test.beforeEach(async ({ page }) => {
    await startApp(page);
  });

  test("Ctrl+E opens the export dialog", async ({ page }) => {
    await page.keyboard.press("Control+e");
    await expect(page.locator(".ed-dialog")).toBeVisible({ timeout: 5000 });
  });

  test("export dialog shows format selector", async ({ page }) => {
    await page.keyboard.press("Control+e");
    const dialog = page.locator(".ed-dialog");
    await expect(dialog).toBeVisible({ timeout: 5000 });
    await expect(dialog).toContainText(/WAV|AIFF|FLAC|Format/i);
  });

  test("export dialog shows bit depth selector", async ({ page }) => {
    await page.keyboard.press("Control+e");
    const dialog = page.locator(".ed-dialog");
    await expect(dialog).toBeVisible({ timeout: 5000 });
    await expect(dialog).toContainText(/16|24|32|Bit/i);
  });

  test("export dialog shows output path input", async ({ page }) => {
    await page.keyboard.press("Control+e");
    const dialog = page.locator(".ed-dialog");
    await expect(dialog).toBeVisible({ timeout: 5000 });
    const input = dialog.locator('input[type="text"], input');
    await expect(input.first()).toBeVisible();
  });

  test("export dialog has Export button", async ({ page }) => {
    await page.keyboard.press("Control+e");
    const dialog = page.locator(".ed-dialog");
    await expect(dialog).toBeVisible({ timeout: 5000 });
    await expect(dialog.locator("button", { hasText: /Export/i })).toBeVisible();
  });

  test("clicking overlay closes the export dialog", async ({ page }) => {
    await page.keyboard.press("Control+e");
    await expect(page.locator(".ed-dialog")).toBeVisible({ timeout: 5000 });
    await page.locator(".ed-overlay").click({ position: { x: 5, y: 5 } });
    await expect(page.locator(".ed-dialog")).not.toBeVisible({ timeout: 3000 });
  });
});
