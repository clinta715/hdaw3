import { test, expect } from "@playwright/test";
import { startApp } from "./helpers";

// MidiFxChain is the "MIDI FX" bottom tab. It operates on the selected track's
// MIDI FX chain (independent of clips). Slots are added via a controlled
// <select> (value resets to "" after each add), bypassed/removed via per-slot
// Byp/Del buttons. Track 0 ("Track 1") starts with no MIDI FX.
test.describe("MIDI FX chain (user journeys)", () => {
  test.beforeEach(async ({ page }) => {
    await startApp(page);
    // Select track 0 (click the type badge to avoid the .th-color color picker).
    await page.locator(".th-row").first().locator(".th-type-badge").click();
    // Switch to the MIDI FX tab.
    await page.locator(".bt-tab", { hasText: "MIDI FX" }).click();
    await expect(page.locator(".midi-fx-chain")).toBeVisible({ timeout: 5000 });
  });

  test("header shows the selected track index", async ({ page }) => {
    await expect(page.locator(".mfx-title")).toContainText("MIDI FX — Track 0");
  });

  test("empty state is shown when the track has no MIDI FX", async ({ page }) => {
    await expect(page.locator(".mfx-empty")).toContainText(/No MIDI FX/i);
    await expect(page.locator(".mfx-slot")).toHaveCount(0);
  });

  test("add dropdown lists every MIDI FX type", async ({ page }) => {
    // Source of truth: MIDI_FX_TYPES in frontend/src/components/MidiFxChain.tsx.
    // Match options by their exact value (hasText "Velocity" would also match
    // "Velocity Curve"), and cover the full current type list.
    const expected: [value: string, label: string][] = [
      ["arpeggiator", "Arpeggiator"],
      ["velocity", "Velocity"],
      ["chord", "Chord"],
      ["scale", "Scale Quantize"],
      ["notelength", "Note Length"],
      ["transpose", "Transpose"],
      ["keyfilter", "Key Filter"],
      ["multinote", "Multi-Note"],
      ["velocitycurve", "Velocity Curve"],
      ["notechance", "Note Chance"],
      ["mididelay", "MIDI Delay"],
      ["humanize", "Humanize"],
      ["strum", "Strum"],
    ];
    const addSelect = page.locator(".mfx-add-select");
    // 13 type options + the "+ Add MIDI FX…" placeholder (value="").
    await expect(addSelect.locator("option")).toHaveCount(expected.length + 1);
    for (const [value, label] of expected) {
      await expect(addSelect.locator(`option[value="${value}"]`)).toHaveText(label);
    }
  });

  test("adding a slot renders a slot card with the type label", async ({ page }) => {
    await page.locator(".mfx-add-select").selectOption("arpeggiator");
    const slot = page.locator(".mfx-slot").first();
    await expect(slot).toBeVisible({ timeout: 5000 });
    await expect(slot.locator(".mfx-slot-type")).toHaveText("Arpeggiator");
    await expect(page.locator(".mfx-empty")).toHaveCount(0);
  });

  test("a freshly added slot is not bypassed", async ({ page }) => {
    await page.locator(".mfx-add-select").selectOption("velocity");
    const slot = page.locator(".mfx-slot").first();
    await expect(slot).toBeVisible({ timeout: 5000 });
    await expect(slot).not.toHaveClass(/mfx-slot--bypassed/);
  });

  test("added slot shows its index", async ({ page }) => {
    await page.locator(".mfx-add-select").selectOption("chord");
    const slot = page.locator(".mfx-slot").first();
    await expect(slot).toBeVisible({ timeout: 5000 });
    await expect(slot.locator(".mfx-slot-index")).toHaveText("0");
  });

  test("adding multiple slots renders one card each in order", async ({ page }) => {
    await page.locator(".mfx-add-select").selectOption("arpeggiator");
    await expect(page.locator(".mfx-slot")).toHaveCount(1, { timeout: 5000 });
    await page.locator(".mfx-add-select").selectOption("velocity");
    await expect(page.locator(".mfx-slot")).toHaveCount(2, { timeout: 5000 });

    const types = page.locator(".mfx-slot .mfx-slot-type");
    await expect(types.nth(0)).toHaveText("Arpeggiator");
    await expect(types.nth(1)).toHaveText("Velocity");
    // Indices are sequential.
    const indices = page.locator(".mfx-slot .mfx-slot-index");
    await expect(indices.nth(0)).toHaveText("0");
    await expect(indices.nth(1)).toHaveText("1");
  });

  test("bypass button toggles the bypassed class on", async ({ page }) => {
    await page.locator(".mfx-add-select").selectOption("arpeggiator");
    const slot = page.locator(".mfx-slot").first();
    await expect(slot).toBeVisible({ timeout: 5000 });
    await slot.locator(".mfx-btn", { hasText: "Byp" }).click();
    await expect(slot).toHaveClass(/mfx-slot--bypassed/, { timeout: 5000 });
  });

  test("bypass button toggles the bypassed class back off", async ({ page }) => {
    await page.locator(".mfx-add-select").selectOption("arpeggiator");
    const slot = page.locator(".mfx-slot").first();
    await expect(slot).toBeVisible({ timeout: 5000 });
    const byp = slot.locator(".mfx-btn", { hasText: "Byp" });
    await byp.click();
    await expect(slot).toHaveClass(/mfx-slot--bypassed/, { timeout: 5000 });
    await byp.click();
    await expect(slot).not.toHaveClass(/mfx-slot--bypassed/, { timeout: 5000 });
  });

  test("remove button deletes the slot card", async ({ page }) => {
    await page.locator(".mfx-add-select").selectOption("chord");
    await expect(page.locator(".mfx-slot")).toHaveCount(1, { timeout: 5000 });
    await page.locator(".mfx-slot").first().locator(".mfx-btn", { hasText: "Del" }).click();
    await expect(page.locator(".mfx-slot")).toHaveCount(0, { timeout: 5000 });
  });

  test("removing the only slot returns to the empty state", async ({ page }) => {
    await page.locator(".mfx-add-select").selectOption("notelength");
    await expect(page.locator(".mfx-slot")).toHaveCount(1, { timeout: 5000 });
    await page.locator(".mfx-slot").first().locator(".mfx-btn", { hasText: "Del" }).click();
    await expect(page.locator(".mfx-slot")).toHaveCount(0, { timeout: 5000 });
    await expect(page.locator(".mfx-empty")).toContainText(/No MIDI FX/i, { timeout: 5000 });
  });

  test("removing one of two slots keeps the other", async ({ page }) => {
    await page.locator(".mfx-add-select").selectOption("arpeggiator");
    await expect(page.locator(".mfx-slot")).toHaveCount(1, { timeout: 5000 });
    await page.locator(".mfx-add-select").selectOption("velocity");
    await expect(page.locator(".mfx-slot")).toHaveCount(2, { timeout: 5000 });
    // Remove the first slot; the second remains (re-indexed to 0).
    await page.locator(".mfx-slot").first().locator(".mfx-btn", { hasText: "Del" }).click();
    await expect(page.locator(".mfx-slot")).toHaveCount(1, { timeout: 5000 });
    await expect(page.locator(".mfx-slot .mfx-slot-type").first()).toHaveText("Velocity");
  });
});
