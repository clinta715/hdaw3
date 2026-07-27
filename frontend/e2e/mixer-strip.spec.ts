import { test, expect } from "@playwright/test";
import { startApp, rpcCall } from "./helpers";

test.describe("Mixer strip (user journeys)", () => {
  test.beforeEach(async ({ page }) => {
    await startApp(page);
    // Switch to the Mixer tab
    await page.locator(".bt-tab", { hasText: "Mixer" }).click();
  });

  test("mixer panel renders with a strip for each track", async ({ page }) => {
    const strips = page.locator(".mixer-strip");
    await expect(strips.first()).toBeVisible({ timeout: 5000 });
    // Default project has 1 track + master = 2 strips minimum
    expect(await strips.count()).toBeGreaterThanOrEqual(2);
  });

  test("master strip has the master class", async ({ page }) => {
    const master = page.locator(".mixer-strip--master");
    await expect(master).toBeVisible({ timeout: 5000 });
    await expect(master.locator(".ms-name")).toContainText("Master");
  });

  test("track strip displays track name", async ({ page }) => {
    const strip = page.locator(".mixer-strip").not(".mixer-strip--master").first();
    await expect(strip.locator(".ms-name")).toContainText("Track");
  });

  test("mute button toggles mute state", async ({ page }) => {
    const strip = page.locator(".mixer-strip").not(".mixer-strip--master").first();
    const muteBtn = strip.locator(".ms-btn", { hasText: "M" });
    await expect(muteBtn).not.toHaveClass(/active/);
    await muteBtn.click();
    await expect(muteBtn).toHaveClass(/active/, { timeout: 3000 });
    await muteBtn.click();
    await expect(muteBtn).not.toHaveClass(/active/, { timeout: 3000 });
  });

  test("solo button toggles solo state", async ({ page }) => {
    const strip = page.locator(".mixer-strip").not(".mixer-strip--master").first();
    const soloBtn = strip.locator(".ms-btn", { hasText: "S" });
    await expect(soloBtn).not.toHaveClass(/active/);
    await soloBtn.click();
    await expect(soloBtn).toHaveClass(/active/, { timeout: 3000 });
    await soloBtn.click();
    await expect(soloBtn).not.toHaveClass(/active/, { timeout: 3000 });
  });

  test("arm button toggles arm state", async ({ page }) => {
    const strip = page.locator(".mixer-strip").not(".mixer-strip--master").first();
    const armBtn = strip.locator(".ms-btn", { hasText: "R" });
    await expect(armBtn).not.toHaveClass(/active/);
    await armBtn.click();
    await expect(armBtn).toHaveClass(/active/, { timeout: 3000 });
    await armBtn.click();
    await expect(armBtn).not.toHaveClass(/active/, { timeout: 3000 });
  });

  test("volume fader is present and has correct range", async ({ page }) => {
    const strip = page.locator(".mixer-strip").not(".mixer-strip--master").first();
    const fader = strip.locator(".ms-fader");
    await expect(fader).toBeVisible();
    await expect(fader).toHaveAttribute("min", "0");
    await expect(fader).toHaveAttribute("max", "1");
  });

  test("pan fader is present and centered", async ({ page }) => {
    const strip = page.locator(".mixer-strip").not(".mixer-strip--master").first();
    const panFader = strip.locator(".ms-pan-fader");
    await expect(panFader).toBeVisible();
    await expect(panFader).toHaveAttribute("min", "-1");
    await expect(panFader).toHaveAttribute("max", "1");
  });

  test("master strip does not have mute/solo/arm buttons", async ({ page }) => {
    const master = page.locator(".mixer-strip--master");
    await expect(master).toBeVisible({ timeout: 5000 });
    // Master strip should not have the .ms-buttons container
    const buttons = master.locator(".ms-buttons");
    await expect(buttons).not.toBeVisible();
  });

  test("color bar is visible on each strip", async ({ page }) => {
    const strip = page.locator(".mixer-strip").not(".mixer-strip--master").first();
    const colorBar = strip.locator(".ms-color");
    await expect(colorBar).toBeVisible();
  });

  test("VU meter bars are present", async ({ page }) => {
    const strip = page.locator(".mixer-strip").not(".mixer-strip--master").first();
    const vu = strip.locator(".ms-vu");
    await expect(vu).toBeVisible();
    const bars = vu.locator(".ms-meter");
    expect(await bars.count()).toBe(2);
  });
});
