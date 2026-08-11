import { test, expect } from "@playwright/test";
import { startApp } from "./helpers";

// PhraseGeneratorDialog opens from the TransportBar 🎵 button. Generation
// creates a MIDI clip on the target track and auto-closes the dialog ~400 ms
// after a successful generate. Metadata (scales/chords/patterns/styles) is
// fetched from the composition.* RPCs on mount.
test.describe("Phrase Generator dialog (user journeys)", () => {
  test.beforeEach(async ({ page }) => {
    await startApp(page);
  });

  async function openDialog(page: import("@playwright/test").Page) {
    await page.locator("header.transport-bar [title^='Phrase Generator']").click();
    await expect(page.locator(".pgd-dialog")).toBeVisible({ timeout: 5000 });
  }

  test("opens from the transport bar button", async ({ page }) => {
    await openDialog(page);
    await expect(page.locator(".pgd-header h3")).toContainText("Phrase Generator");
  });

  test("mode selector defaults to Phrase with five options", async ({ page }) => {
    await openDialog(page);
    const mode = page.locator(".pgd-mode-select");
    await expect(mode).toHaveValue("0");
    const options = mode.locator("option");
    await expect(options).toHaveCount(5);
    await expect(options.nth(0)).toHaveText("Phrase");
    await expect(options.nth(1)).toHaveText("Single Chord");
    await expect(options.nth(2)).toHaveText("Chord Progression");
    await expect(options.nth(3)).toHaveText("Arrangement");
    await expect(options.nth(4)).toHaveText("Rhythm");
  });

  test("phrase page shows style, length and density controls", async ({ page }) => {
    await openDialog(page);
    const pg = page.locator(".pgd-page");
    await expect(pg.locator(".pgd-label", { hasText: "Style" })).toBeVisible();
    await expect(pg.locator(".pgd-label", { hasText: "Length" })).toBeVisible();
    await expect(pg.locator(".pgd-label", { hasText: "Density" })).toBeVisible();
  });

  test("metadata dropdowns are populated from the engine", async ({ page }) => {
    await openDialog(page);
    // Root note select always has 12 options (the chromatic notes).
    await expect(page.locator(".pgd-note-select").first().locator("option")).toHaveCount(12);
    // Style select is populated from composition.getStyleNames (async on mount).
    // <option> inside a collapsed <select> is never "visible", so poll the count.
    const styleOptions = page.locator(".pgd-page select").first().locator("option");
    await expect(async () => {
      expect(await styleOptions.count()).toBeGreaterThan(0);
    }).toPass({ timeout: 5000 });
  });

  test("switching to Single Chord shows chord controls", async ({ page }) => {
    await openDialog(page);
    await page.locator(".pgd-mode-select").selectOption("1");
    const pg = page.locator(".pgd-page");
    await expect(pg.locator(".pgd-label", { hasText: "Root Pitch" })).toBeVisible();
    await expect(pg.locator(".pgd-label", { hasText: "Chord Type" })).toBeVisible();
    await expect(pg.locator(".pgd-label", { hasText: "Voicing" })).toBeVisible();
    await expect(pg.locator(".pgd-label", { hasText: "Inversion" })).toBeVisible();
    // Phrase-only controls are gone.
    await expect(pg.locator(".pgd-label", { hasText: "Density" })).toHaveCount(0);
  });

  test("switching to Chord Progression shows progression controls", async ({ page }) => {
    await openDialog(page);
    await page.locator(".pgd-mode-select").selectOption("2");
    const pg = page.locator(".pgd-page");
    await expect(pg.locator(".pgd-label", { hasText: "Pattern" })).toBeVisible();
    await expect(pg.locator(".pgd-label", { hasText: "Chord Override" })).toBeVisible();
    await expect(pg.locator(".pgd-label", { hasText: "Beats/Chord" })).toBeVisible();
  });

  test("arpeggiate checkbox reveals the rate control", async ({ page }) => {
    await openDialog(page);
    await page.locator(".pgd-mode-select").selectOption("1");
    const pg = page.locator(".pgd-page");
    const arp = pg.locator("input[type='checkbox']").first();
    await expect(arp).not.toBeChecked();
    // Rate input is hidden until arpeggiate is on.
    await expect(pg.locator(".pgd-label-sm", { hasText: "Rate" })).toHaveCount(0);
    await arp.check();
    await expect(pg.locator(".pgd-label-sm", { hasText: "Rate" })).toBeVisible();
  });

  test("selecting Arpeggio style applies smart defaults", async ({ page }) => {
    await openDialog(page);
    const pg = page.locator(".pgd-page");
    // Default style is Standard (length 4, density 8).
    const lengthInput = pg.locator(".pgd-input").nth(0);
    const densityInput = pg.locator(".pgd-input").nth(1);
    await expect(lengthInput).toHaveValue("4");
    await expect(densityInput).toHaveValue("8");
    // Arpeggio smart defaults: length 4, density 16.
    await pg.locator("select").first().selectOption({ label: "Arpeggio" });
    await expect(densityInput).toHaveValue("16");
  });

  test("velocity slider updates its readout", async ({ page }) => {
    await openDialog(page);
    const row = page.locator(".pgd-velocity-row");
    await expect(row.locator(".pgd-value")).toHaveText("90"); // default
    await row.locator(".pgd-slider").fill("110");
    await expect(row.locator(".pgd-value")).toHaveText("110");
  });

  test("cancel closes the dialog without creating a clip", async ({ page }) => {
    await openDialog(page);
    const before = await page.locator(".tl-clip").count();
    await page.locator(".pgd-btn-cancel").click();
    await expect(page.locator(".pgd-dialog")).toBeHidden({ timeout: 5000 });
    expect(await page.locator(".tl-clip").count()).toBe(before);
  });

  test("close (×) button closes the dialog", async ({ page }) => {
    await openDialog(page);
    await page.locator(".pgd-close").click();
    await expect(page.locator(".pgd-dialog")).toBeHidden({ timeout: 5000 });
  });

  test("generate (phrase mode) creates a clip and closes the dialog", async ({ page }) => {
    await openDialog(page);
    const before = await page.locator(".tl-clip").count();
    await page.locator(".pgd-btn-generate").click();
    // Dialog auto-closes ~400 ms after a successful generate.
    await expect(page.locator(".pgd-dialog")).toBeHidden({ timeout: 10000 });
    await expect(page.locator(".tl-clip")).toHaveCount(before + 1, { timeout: 10000 });
  });

  test("generate (single chord mode) creates a clip", async ({ page }) => {
    await openDialog(page);
    await page.locator(".pgd-mode-select").selectOption("1");
    const before = await page.locator(".tl-clip").count();
    await page.locator(".pgd-btn-generate").click();
    await expect(page.locator(".pgd-dialog")).toBeHidden({ timeout: 10000 });
    await expect(page.locator(".tl-clip")).toHaveCount(before + 1, { timeout: 10000 });
  });

  test("generate (progression mode) creates a clip", async ({ page }) => {
    await openDialog(page);
    await page.locator(".pgd-mode-select").selectOption("2");
    const before = await page.locator(".tl-clip").count();
    await page.locator(".pgd-btn-generate").click();
    await expect(page.locator(".pgd-dialog")).toBeHidden({ timeout: 10000 });
    await expect(page.locator(".tl-clip")).toHaveCount(before + 1, { timeout: 10000 });
  });

  test("switching to Arrangement shows arrangement controls", async ({ page }) => {
    await openDialog(page);
    await page.locator(".pgd-mode-select").selectOption("3");
    const pg = page.locator(".pgd-page");
    await expect(pg.locator(".pgd-label", { hasText: "Bars" })).toBeVisible();
    await expect(pg.locator(".pgd-label", { hasText: "Complexity" })).toBeVisible();
    await expect(pg.locator(".pgd-label", { hasText: "Swing" })).toBeVisible();
    await expect(pg.locator(".pgd-label", { hasText: "Tracks" })).toBeVisible();
    // Phrase-only controls are gone.
    await expect(pg.locator(".pgd-label", { hasText: "Density" })).toHaveCount(0);
  });

  test("generate (arrangement mode) creates a track + clip per part", async ({ page }) => {
    await openDialog(page);
    await page.locator(".pgd-mode-select").selectOption("3");
    // Fixed seed so the part set is deterministic (kick, closed/open hat, clap, bass).
    await page.locator(".pgd-row", { hasText: "Seed" }).locator(".pgd-input").fill("12345");
    const tracksBefore = await page.locator(".th-row").count();
    const clipsBefore = await page.locator(".tl-clip").count();
    await page.locator(".pgd-btn-generate").click();
    await expect(page.locator(".pgd-dialog")).toBeHidden({ timeout: 10000 });
    // Default enables kick, closed+open hat, clap, bass -> 5 new tracks, each with a clip.
    await expect(page.locator(".th-row")).toHaveCount(tracksBefore + 5, { timeout: 10000 });
    await expect(page.locator(".tl-clip")).toHaveCount(clipsBefore + 5, { timeout: 10000 });
  });
});
