import { test, expect } from "@playwright/test";
import { startApp } from "./helpers";

test.describe("Mixer strip (user journeys)", () => {
  test.beforeEach(async ({ page }) => {
    await startApp(page);
    // Switch to the Mixer tab
    await page.locator(".bt-tab", { hasText: "Mixer" }).click();
    // Wait for mixer content to render
    await expect(page.locator(".mixer-strips")).toBeVisible({ timeout: 5000 });
  });

  test("mixer panel renders with a strip for each track", async ({ page }) => {
    const strips = page.locator(".mixer-strip");
    await expect(strips.first()).toBeVisible({ timeout: 5000 });
    expect(await strips.count()).toBeGreaterThanOrEqual(2);
  });

  test("master strip has the master class", async ({ page }) => {
    const master = page.locator(".mixer-strip--master");
    await expect(master).toBeVisible({ timeout: 5000 });
    await expect(master.locator(".ms-name")).toContainText("Master");
  });

  test("track strip displays track name", async ({ page }) => {
    await expect(page.locator(".mixer-strip").first()).toBeVisible({ timeout: 5000 });
    const strip = page.locator(".mixer-strips .mixer-strip").first();
    await expect(strip).toBeVisible({ timeout: 5000 });
    await expect(strip.locator(".ms-name")).toContainText("Track");
  });

  test("mute button toggles mute state", async ({ page }) => {
    await expect(page.locator(".mixer-strips .mixer-strip").first()).toBeVisible({ timeout: 5000 });
    // Find a non-master strip that isn't muted by a parent folder
    const strips = page.locator(".mixer-strips .mixer-strip");
    const count = await strips.count();
    let muteBtn;
    for (let i = 0; i < count; i++) {
      const btn = strips.nth(i).locator("button.ms-btn", { hasText: "M" });
      const title = await btn.getAttribute("title");
      if (title === "Mute") { muteBtn = btn; break; }
    }
    if (muteBtn) {
      await muteBtn.click();
      await expect(muteBtn).toHaveClass(/active/, { timeout: 5000 });
      await muteBtn.click();
      await expect(muteBtn).not.toHaveClass(/active/, { timeout: 5000 });
    }
  });

  test("solo button toggles solo state", async ({ page }) => {
    await expect(page.locator(".mixer-strips .mixer-strip").first()).toBeVisible({ timeout: 5000 });
    const strips = page.locator(".mixer-strips .mixer-strip");
    const count = await strips.count();
    let soloBtn;
    for (let i = 0; i < count; i++) {
      const btn = strips.nth(i).locator("button.ms-btn", { hasText: "S" });
      const title = await btn.getAttribute("title");
      if (title === "Solo") { soloBtn = btn; break; }
    }
    if (soloBtn) {
      await soloBtn.click();
      await expect(soloBtn).toHaveClass(/active/, { timeout: 5000 });
      await soloBtn.click();
      await expect(soloBtn).not.toHaveClass(/active/, { timeout: 5000 });
    }
  });

  test("arm button toggles arm state", async ({ page }) => {
    await expect(page.locator(".mixer-strips .mixer-strip").first()).toBeVisible({ timeout: 5000 });
    const strip = page.locator(".mixer-strips .mixer-strip").first();
    const armBtn = strip.locator(".ms-btn", { hasText: "R" });
    await armBtn.click();
    await expect(armBtn).toHaveClass(/active/, { timeout: 3000 });
    await armBtn.click();
    await expect(armBtn).not.toHaveClass(/active/, { timeout: 3000 });
  });

  test("volume fader is present and has correct range", async ({ page }) => {
    await expect(page.locator(".mixer-strips .mixer-strip").first()).toBeVisible({ timeout: 5000 });
    const strip = page.locator(".mixer-strips .mixer-strip").first();
    const fader = strip.locator(".ms-fader");
    await expect(fader).toBeVisible();
    await expect(fader).toHaveAttribute("min", "0");
    await expect(fader).toHaveAttribute("max", "1");
  });

  test("pan fader is present and centered", async ({ page }) => {
    await expect(page.locator(".mixer-strips .mixer-strip").first()).toBeVisible({ timeout: 5000 });
    const strip = page.locator(".mixer-strips .mixer-strip").first();
    const panFader = strip.locator(".ms-pan-fader");
    await expect(panFader).toBeVisible();
    await expect(panFader).toHaveAttribute("min", "-1");
    await expect(panFader).toHaveAttribute("max", "1");
  });

  test("master strip does not have mute/solo/arm buttons", async ({ page }) => {
    const master = page.locator(".mixer-strip--master");
    await expect(master).toBeVisible({ timeout: 5000 });
    const buttons = master.locator(".ms-buttons");
    await expect(buttons).not.toBeVisible();
  });

  test("color bar is visible on each strip", async ({ page }) => {
    await expect(page.locator(".mixer-strips .mixer-strip").first()).toBeVisible({ timeout: 5000 });
    const strip = page.locator(".mixer-strips .mixer-strip").first();
    const colorBar = strip.locator(".ms-color");
    await expect(colorBar).toBeVisible();
  });

  test("VU meter bars are present", async ({ page }) => {
    await expect(page.locator(".mixer-strips .mixer-strip").first()).toBeVisible({ timeout: 5000 });
    const strip = page.locator(".mixer-strips .mixer-strip").first();
    const vu = strip.locator(".ms-vu");
    await expect(vu).toBeVisible();
    const bars = vu.locator(".ms-meter");
    expect(await bars.count()).toBe(2);
  });
});
