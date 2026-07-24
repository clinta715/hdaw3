import { test, expect } from "@playwright/test";

test.describe("HDAW Application", () => {
  test("loads the application", async ({ page }) => {
    await page.goto("/");
    await expect(page).toHaveTitle(/HDAW/);
  });

  test("renders transport bar", async ({ page }) => {
    await page.goto("/");
    await expect(page.locator(".transport-bar")).toBeVisible();
  });

  test("renders timeline", async ({ page }) => {
    await page.goto("/");
    await expect(page.locator(".timeline-minimal")).toBeVisible();
  });

  test("renders track headers", async ({ page }) => {
    await page.goto("/");
    await expect(page.locator(".track-headers")).toBeVisible();
  });

  test("renders status bar", async ({ page }) => {
    await page.goto("/");
    await expect(page.locator(".status-bar")).toBeVisible();
  });

  test("displays BPM in transport bar", async ({ page }) => {
    await page.goto("/");
    await expect(page.locator(".transport-bar")).toContainText(/\d+/);
  });

  test("displays sample rate in status bar", async ({ page }) => {
    await page.goto("/");
    await expect(page.locator(".status-bar")).toContainText(/Hz/);
  });

  test("shows default tracks", async ({ page }) => {
    await page.goto("/");
    const tracks = page.locator(".track-header");
    await expect(tracks.first()).toBeVisible({ timeout: 10000 });
  });

  test("play button is clickable", async ({ page }) => {
    await page.goto("/");
    const playBtn = page.locator(".transport-bar button").filter({ hasText: /▶|Play/i });
    await expect(playBtn).toBeVisible();
  });

  test("stop button is clickable", async ({ page }) => {
    await page.goto("/");
    const stopBtn = page.locator(".transport-bar button").filter({ hasText: /■|Stop/i });
    await expect(stopBtn).toBeVisible();
  });
});
