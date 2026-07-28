import { test, expect } from "@playwright/test";
import { startApp } from "./helpers";

// ImportDialog is opened from the File menu ("Import Audio..." / "Import
// MIDI...") or the Ctrl+Shift+I / Ctrl+Shift+M shortcuts. The import *mode*
// (audio vs midi) is fixed by how the dialog is opened — there is no in-dialog
// toggle — so the mode tests open it each way and verify the title/placeholder
// and the switch between the two. File selection is via the text input or the
// Browse button (which uses window.prompt). Import creates a clip on the
// chosen track and closes the dialog.
test.describe("Import dialog (user journeys)", () => {
  test.beforeEach(async ({ page }) => {
    await startApp(page);
  });

  async function openImport(page: import("@playwright/test").Page, mode: "audio" | "midi") {
    await page.locator(".fm-trigger", { hasText: "File" }).click();
    const item = mode === "audio" ? "Import Audio..." : "Import MIDI...";
    await page.locator(".fm-dropdown button", { hasText: item }).click();
    await expect(page.locator(".id-dialog")).toBeVisible({ timeout: 5000 });
  }

  test("Import Audio menu item opens the dialog in audio mode", async ({ page }) => {
    await openImport(page, "audio");
    await expect(page.locator(".id-title")).toHaveText("Import Audio");
  });

  test("Import MIDI menu item opens the dialog in midi mode", async ({ page }) => {
    await openImport(page, "midi");
    await expect(page.locator(".id-title")).toHaveText("Import MIDI");
  });

  test("Ctrl+Shift+I opens the Import Audio dialog", async ({ page }) => {
    await page.keyboard.press("Control+Shift+I");
    await expect(page.locator(".id-dialog")).toBeVisible({ timeout: 5000 });
    await expect(page.locator(".id-title")).toHaveText("Import Audio");
  });

  test("Ctrl+Shift+M opens the Import MIDI dialog", async ({ page }) => {
    await page.keyboard.press("Control+Shift+M");
    await expect(page.locator(".id-dialog")).toBeVisible({ timeout: 5000 });
    await expect(page.locator(".id-title")).toHaveText("Import MIDI");
  });

  test("switching from audio to midi mode changes title and placeholder", async ({ page }) => {
    await openImport(page, "audio");
    await expect(page.locator(".id-title")).toHaveText("Import Audio");
    await expect(page.locator(".id-input")).toHaveAttribute("placeholder", "path/to/audio.wav");
    // Close, then reopen in MIDI mode.
    await page.locator(".id-btn-cancel").click();
    await expect(page.locator(".id-dialog")).toBeHidden({ timeout: 5000 });
    await openImport(page, "midi");
    await expect(page.locator(".id-title")).toHaveText("Import MIDI");
    await expect(page.locator(".id-input")).toHaveAttribute("placeholder", "path/to/file.mid");
  });

  test("file input accepts a typed path", async ({ page }) => {
    await openImport(page, "midi");
    await page.locator(".id-input").fill("C:/music/loop.mid");
    await expect(page.locator(".id-input")).toHaveValue("C:/music/loop.mid");
  });

  test("Browse button prompts for a path and fills the input", async ({ page }) => {
    await openImport(page, "audio");
    page.once("dialog", (d) => d.accept("/tmp/sample.wav"));
    await page.locator(".id-browse").click();
    await expect(page.locator(".id-input")).toHaveValue("/tmp/sample.wav", { timeout: 5000 });
  });

  test("track select lists New Track plus the existing tracks", async ({ page }) => {
    await openImport(page, "midi");
    const select = page.locator(".id-select");
    await expect(select.locator("option", { hasText: "New Track" })).toHaveCount(1);
    // Default project has 3 tracks -> New Track + 3 = at least 4 options.
    expect(await select.locator("option").count()).toBeGreaterThanOrEqual(4);
  });

  test("Cancel closes the dialog without importing", async ({ page }) => {
    await openImport(page, "midi");
    const before = await page.locator(".tl-clip").count();
    await page.locator(".id-input").fill("loop.mid");
    await page.locator(".id-btn-cancel").click();
    await expect(page.locator(".id-dialog")).toBeHidden({ timeout: 5000 });
    expect(await page.locator(".tl-clip").count()).toBe(before);
  });

  test("Import with an empty path does nothing", async ({ page }) => {
    await openImport(page, "midi");
    const before = await page.locator(".tl-clip").count();
    await page.locator(".id-btn-import").click();
    await page.waitForTimeout(300);
    // handleImport returns early on empty path: dialog stays, no clip added.
    await expect(page.locator(".id-dialog")).toBeVisible();
    expect(await page.locator(".tl-clip").count()).toBe(before);
  });

  test("Import MIDI with a path creates a clip and closes the dialog", async ({ page }) => {
    await openImport(page, "midi");
    const before = await page.locator(".tl-clip").count();
    await page.locator(".id-input").fill("generated.mid");
    await page.locator(".id-btn-import").click();
    await expect(page.locator(".id-dialog")).toBeHidden({ timeout: 5000 });
    await expect(page.locator(".tl-clip")).toHaveCount(before + 1, { timeout: 10000 });
  });
});
