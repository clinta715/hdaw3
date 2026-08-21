import { Page, test, expect } from "@playwright/test";
import { startApp, rpcCall } from "./helpers";

const PREFS_KEY = "hdaw.exportPrefs";

async function openExportDialog(page: Page) {
  await page.keyboard.press("Control+e");
  const dialog = page.locator(".ed-dialog");
  await expect(dialog).toBeVisible({ timeout: 5000 });
  return dialog;
}

function labeledRow(page: Page, label: string) {
  return page.locator(".ed-dialog .ed-row", {
    has: page.locator(".ed-label", { hasText: label }),
  });
}

test.describe("Export dialog — sample rate, range, prefs persistence", () => {
  test.beforeEach(async ({ page }) => {
    await startApp(page);
  });

  test("shows sample rate and range controls; changes persist to localStorage and restore on open", async ({ page }) => {
    await openExportDialog(page);

    const formatSelect = labeledRow(page, "Format").locator("select");
    const bitDepthSelect = labeledRow(page, "Bit Depth").locator("select");
    const sampleRateSelect = labeledRow(page, "Sample Rate").locator("select");
    const rangeSelect = labeledRow(page, "Range").locator("select");

    await expect(sampleRateSelect).toBeVisible();
    await expect(rangeSelect).toBeVisible();
    await expect(sampleRateSelect).toHaveValue("48000");
    await expect(rangeSelect).toHaveValue("full");

    await sampleRateSelect.selectOption("96000");
    await bitDepthSelect.selectOption("16");

    await expect
      .poll(() => page.evaluate((k) => localStorage.getItem(k), PREFS_KEY))
      .toContain('"sampleRate":96000');
    await expect
      .poll(() => page.evaluate((k) => localStorage.getItem(k), PREFS_KEY))
      .toContain('"bitDepth":16');

    await page.locator(".ed-dialog button.ed-btn-cancel").click();
    await expect(page.locator(".ed-dialog")).not.toBeVisible({ timeout: 3000 });

    await page.evaluate(
      ([k, v]) => localStorage.setItem(k, v),
      [PREFS_KEY, JSON.stringify({ format: "flac", bitDepth: 16, sampleRate: 44100, range: "full" })] as [string, string],
    );

    await openExportDialog(page);
    await expect(labeledRow(page, "Format").locator("select")).toHaveValue("flac");
    await expect(labeledRow(page, "Bit Depth").locator("select")).toHaveValue("16");
    await expect(labeledRow(page, "Sample Rate").locator("select")).toHaveValue("44100");
  });

  test("loop region option is disabled until a loop range is set, and disabled again at zero width", async ({ page }) => {
    await openExportDialog(page);

    const rangeSelect = labeledRow(page, "Range").locator("select");
    const loopOption = rangeSelect.locator("option", { hasText: "Loop Region" });

    await expect(loopOption).toBeDisabled();

    await rpcCall(page, "project.setLooping", { looping: true });
    await rpcCall(page, "project.setLoopStart", { beat: 2 });
    await rpcCall(page, "project.setLoopEnd", { beat: 6 });
    await expect(loopOption).toBeEnabled({ timeout: 10000 });

    await rangeSelect.selectOption("loop");
    await expect(rangeSelect).toHaveValue("loop");

    await rpcCall(page, "project.setLoopEnd", { beat: 2 });
    await expect(loopOption).toBeDisabled({ timeout: 10000 });
  });
});

test.describe("Preferences — MIDI None and engine connection", () => {
  test.beforeEach(async ({ page }) => {
    await startApp(page);
  });

  test("selecting MIDI None issues midi.closeDevice and keeps the selection consistent", async ({ page }) => {
    await page.locator('button[title="Preferences"]').click();
    const dialog = page.locator(".preferences-dialog");
    await expect(dialog).toBeVisible({ timeout: 5000 });

    await page.evaluate(() => {
      const rpcClient = (window as any).rpc;
      const calls: { method: string }[] = [];
      (window as any).__rpcSpy = calls;
      const orig = rpcClient.call.bind(rpcClient);
      rpcClient.call = (method: string, params?: unknown) => {
        calls.push({ method });
        return orig(method, params);
      };
    });

    const midiSelect = dialog
      .locator(".pref-section", { has: page.locator("h3", { hasText: "MIDI" }) })
      .locator("select");
    await midiSelect.evaluate((el) => {
      (el as HTMLSelectElement).selectedIndex = -1;
    });
    await midiSelect.selectOption({ label: "None" });

    await expect
      .poll(() =>
        page.evaluate(() =>
          ((window as any).__rpcSpy as { method: string }[]).some((c) => c.method === "midi.closeDevice"),
        ),
      )
      .toBe(true);

    await expect(midiSelect).toHaveValue("");
    await expect(page.locator(".toast--error")).toHaveCount(0);
  });

  test("engine connection block shows the WebSocket endpoint read-only", async ({ page }) => {
    await page.locator('button[title="Preferences"]').click();
    const dialog = page.locator(".preferences-dialog");
    await expect(dialog).toBeVisible({ timeout: 5000 });

    const section = dialog.locator(".pref-section", {
      has: page.locator("h3", { hasText: "Engine Connection" }),
    });
    await expect(section).toBeVisible();
    await expect(section.locator(".pref-conn-value")).toHaveText(/ws:\/\/127\.0\.0\.1:\d+/);
    await expect(section).toContainText(/command-line flags/i);
    await expect(section.locator("select, input, button")).toHaveCount(0);
  });
});
