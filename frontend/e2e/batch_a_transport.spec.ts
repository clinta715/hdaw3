import { test, expect } from "@playwright/test";
import { startApp, rpcCall } from "./helpers";

test.describe("Transport bar batch A (key/scale, count-in removal, follow)", () => {
  test.beforeEach(async ({ page }) => {
    await startApp(page);
  });

  test("key/scale popover changes project root and mode", async ({ page }) => {
    const trigger = page.locator('header.transport-bar button[title="Project key and scale"]');
    await expect(trigger).toBeVisible();
    await expect(trigger).toContainText("C");

    const modes = await rpcCall<{ index: number; name: string }[]>(page, "composition.getScaleModes");
    expect(modes.length).toBeGreaterThan(1);
    const targetMode = modes.find((m) => m.index !== 0) ?? modes[0];

    await trigger.click();
    const rootSel = page.locator('.tb-key-scale-popover select[aria-label="Key root"]');
    const modeSel = page.locator('.tb-key-scale-popover select[aria-label="Scale mode"]');
    await expect(rootSel).toBeVisible();
    await expect(modeSel).toBeVisible();

    await rootSel.selectOption("7");
    await modeSel.selectOption(String(targetMode.index));

    await expect(async () => {
      expect(await rpcCall<number>(page, "read.getScaleRoot")).toBe(7);
    }).toPass({ timeout: 10000 });
    await expect(async () => {
      expect(await rpcCall<number>(page, "read.getScaleMode")).toBe(targetMode.index);
    }).toPass({ timeout: 10000 });

    await expect(trigger).toContainText("G", { timeout: 10000 });

    await page.keyboard.press("Escape");
    await expect(rootSel).toHaveCount(0);
  });

  test("count-in (1Bar) button no longer exists", async ({ page }) => {
    await expect(page.locator('header.transport-bar button[title="Count-in (1 bar)"]')).toHaveCount(0);
    await expect(page.locator("header.transport-bar button", { hasText: "1Bar" })).toHaveCount(0);
    await expect(page.locator('header.transport-bar button[title="Metronome"]')).toBeVisible();
    await expect(page.locator('header.transport-bar button[title="Follow Playhead"]')).toBeVisible();
  });

  test("follow button toggles pressed state", async ({ page }) => {
    const btn = page.locator('header.transport-bar button[title="Follow Playhead"]');
    await expect(btn).toHaveAttribute("aria-pressed", "false");
    await expect(btn).not.toHaveClass(/active/);
    await btn.click();
    await expect(btn).toHaveAttribute("aria-pressed", "true");
    await expect(btn).toHaveClass(/active/);
    await btn.click();
    await expect(btn).toHaveAttribute("aria-pressed", "false");
    await expect(btn).not.toHaveClass(/active/);
  });
});
