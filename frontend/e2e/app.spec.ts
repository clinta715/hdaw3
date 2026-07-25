import { test, expect } from "@playwright/test";
import { startApp } from "./helpers";

test.describe("HDAW Application", () => {
  // The StartupDialog gates the App; startApp clicks "New Project" to reach the
  // main UI with a fresh default project.
  test.beforeEach(async ({ page }) => {
    await startApp(page);
  });

  test("loads the application", async ({ page }) => {
    await expect(page).toHaveTitle(/HDAW/);
  });

  test("renders transport bar", async ({ page }) => {
    await expect(page.locator("header.transport-bar")).toBeVisible();
  });

  test("renders timeline", async ({ page }) => {
    await expect(page.locator(".timeline-minimal")).toBeVisible();
  });

  test("renders track headers", async ({ page }) => {
    await expect(page.locator("aside.track-headers")).toBeVisible();
  });

  test("renders status bar", async ({ page }) => {
    await expect(page.locator(".status-bar")).toBeVisible();
  });

  test("displays BPM in transport bar", async ({ page }) => {
    await expect(page.locator("header.transport-bar")).toContainText(/BPM/);
  });

  test("displays sample rate in status bar", async ({ page }) => {
    await expect(page.locator(".status-bar")).toContainText(/Hz/);
  });

  test("shows default tracks", async ({ page }) => {
    await expect(page.locator(".th-row").first()).toBeVisible({ timeout: 10000 });
  });

  test("play button is visible", async ({ page }) => {
    await expect(page.locator("header.transport-bar .tb-play")).toBeVisible();
  });

  test("stop button is visible", async ({ page }) => {
    await expect(page.locator('header.transport-bar button[title="Stop"]')).toBeVisible();
  });
});
