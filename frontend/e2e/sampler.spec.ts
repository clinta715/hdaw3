import { test, expect, Page } from "@playwright/test";
import * as fs from "fs";
import { startApp, rpcCall, writeSineWav, tempWavPath } from "./helpers";

/** Record RPC method calls so tests can assert a call was made. */
async function spyOnRpc(page: Page) {
  await page.evaluate(() => {
    (window as any).__rpcCalls = [];
    const rpcClient = (window as any).rpc;
    const orig = rpcClient.call.bind(rpcClient);
    rpcClient.call = (method: string, params?: unknown) => {
      (window as any).__rpcCalls.push({ method, params });
      return orig(method, params);
    };
  });
}

test.describe("Sampler", () => {
  test("sampler tab renders and shows controls", async ({ page }) => {
    await startApp(page);
    // Select track 0 so selectedTrackIndex is set (SamplerEditor reads it)
    await page.locator(".th-row .th-type-badge").first().click();

    await rpcCall(page, "project.addFxSlot", {
      trackIndex: 0,
      type: "sampler",
      position: -1,
    });

    const samplerTab = page.locator('button:has-text("Sampler")');
    if (await samplerTab.isVisible()) {
      await samplerTab.click();
    }

    await expect(async () => {
      const editor = page.locator(".sampler-editor");
      await expect(editor).toBeVisible();
    }).toPass({ timeout: 5000 });

    const modeSelect = page.locator(".sampler-editor__select");
    await expect(modeSelect).toBeVisible();

    // Glide + A/H/D/S/R envelope + Start + End = 8 sliders in the classic editor
    const sliders = page.locator(".sampler-editor__slider");
    await expect(sliders).toHaveCount(8);
  });

  test("slice mode: load sample → detect grid slices → click a slice fires sampler.triggerSlice", async ({ page }) => {
    // Grid detection is content-independent, so a sine WAV is a fine sample.
    const wavPath = tempWavPath("sampler-slice");
    writeSineWav(wavPath);
    try {
      await startApp(page);

      // Select track 0 — the editor needs selectedTrackIndex set before mounting.
      await page.locator(".th-row").first().click();

      // Add the sampler FX slot.
      await rpcCall(page, "project.addFxSlot", { trackIndex: 0, fxType: "sampler" });

      // Resolve the actual slot index (robust to any pre-existing slots).
      const slots = await rpcCall<{ slotIndex: number; fxType: string }[]>(
        page,
        "read.getFxSlots",
        { trackIndex: 0 },
      );
      const samplerSlot = slots.find((s) => s.fxType === "sampler");
      if (!samplerSlot) throw new Error("sampler slot not found after addFxSlot");
      const slotIndex = samplerSlot.slotIndex;

      // Load a sample into the slot.
      await rpcCall(page, "sampler.setSample", {
        trackIndex: 0,
        slotIndex,
        filePath: wavPath,
      });

      // Open the Sampler tab — mounts SamplerEditor, which fetches slot + state.
      await page.locator(".bt-tab", { hasText: "Sampler" }).click();

      // The non-empty editor renders its mode select only once the sampler slot
      // and its state are present.
      const modeSelect = page.locator(".sampler-editor__controls .sampler-editor__select");
      await expect(modeSelect).toBeVisible({ timeout: 5000 });

      // Switch the sample mode to "slice" — fires sampler.setMode, then the
      // editor re-fetches state and renders the slice strip.
      await modeSelect.selectOption("slice");
      const strip = page.locator(".sampler-editor__slice");
      await expect(strip).toBeVisible({ timeout: 5000 });

      // Inside the strip, switch slice mode to "grid" — fires sampler.setSliceMode.
      // The strip's only select is the slice-mode select.
      const sliceModeSelect = strip.locator(".sampler-editor__select");
      await expect(sliceModeSelect).toBeVisible();
      await sliceModeSelect.selectOption("grid");

      // The grid slider only renders once grid mode is adopted by the engine
      // (sensitivity slider has min=0; the grid slider has min=0.0625).
      const gridInput = strip.locator('.sampler-editor__slider[min="0.0625"]');
      await expect(gridInput).toBeVisible({ timeout: 5000 });

      // Set the grid interval to 0.5 beats. Use the native value setter so
      // React's controlled range input fires onChange → sampler.setSliceMode.
      await gridInput.evaluate((el) => {
        const input = el as HTMLInputElement;
        const setter = Object.getOwnPropertyDescriptor(
          window.HTMLInputElement.prototype,
          "value",
        )!.set!;
        setter.call(input, "0.5");
        input.dispatchEvent(new Event("input", { bubbles: true }));
        input.dispatchEvent(new Event("change", { bubbles: true }));
      });

      // Wait for the editor to adopt the value (readout shows the grid %).
      await expect(strip.locator(".sampler-editor__value")).toHaveText("50%", { timeout: 5000 });

      // Click Detect — fires sampler.detectSlices (sliceMode:"grid", sliceGrid:0.5).
      await strip.getByRole("button", { name: "Detect" }).click();

      // Grid 0.5 @ 120 bpm on a 1 s file yields 4 slices. Assert >= 2 to stay
      // robust to tempo. The list renders after the engine stores the points and
      // the editor re-fetches state, so poll.
      await expect(async () => {
        const count = await page.locator(".sampler-slice-list .sampler-slice-btn").count();
        expect(count).toBeGreaterThanOrEqual(2);
      }).toPass({ timeout: 10000 });

      // Install the spy before clicking a slice so we capture the audition call.
      await spyOnRpc(page);

      // Click the first slice button — fires sampler.triggerSlice with index 0.
      const firstSlice = page.locator(".sampler-slice-list .sampler-slice-btn").first();
      await expect(firstSlice).toBeVisible();
      await firstSlice.click();

      // Assert the audition RPC fired with the right params. Do NOT assert the
      // backend result — ok can be false until the engine has adopted the sound.
      await expect(async () => {
        const calls = await page.evaluate(() => (window as any).__rpcCalls ?? []);
        expect(
          calls.some(
            (c: { method: string; params?: Record<string, unknown> }) =>
              c.method === "sampler.triggerSlice" &&
              c.params?.trackIndex === 0 &&
              c.params?.slotIndex === slotIndex &&
              c.params?.sliceIndex === 0,
          ),
        ).toBe(true);
      }).toPass({ timeout: 5000 });
    } finally {
      try { fs.unlinkSync(wavPath); } catch { /* already gone */ }
    }
  });
});