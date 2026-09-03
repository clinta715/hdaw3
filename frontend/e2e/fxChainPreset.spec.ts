import { test, expect } from "@playwright/test";
import { startApp, rpcCall } from "./helpers";

interface FxSlot {
  slotIndex: number;
  fxType: string;
}

interface FxChainPreset {
  id: string;
  name: string;
  slotCount: number;
}

test("saves and reapplies an FX chain preset", async ({ page }) => {
  await startApp(page);
  await page.locator(".th-row").first().click();
  await rpcCall(page, "project.addFxSlot", { trackIndex: 0, fxType: "compressor" });
  await page.locator(".bt-tab", { hasText: "FX Chain" }).click();

  const presetBar = page.getByTestId("fx-chain-preset-bar");
  await expect(presetBar).toBeVisible({ timeout: 10000 });

  const presetName = `E2E Compressor ${Date.now()} ${Math.random().toString(36).slice(2)}`;
  let presetId = "";
  try {
    await presetBar.getByLabel("FX chain preset name").fill(presetName);
    await presetBar.getByRole("button", { name: "Save" }).click();

    await expect(async () => {
      const presets = await rpcCall<FxChainPreset[]>(page, "project.listFxChainPresets", {});
      presetId = presets.find((preset) => preset.name === presetName)?.id ?? "";
      expect(presetId).not.toBe("");
    }).toPass({ timeout: 10000 });

    await expect(presetBar.locator("select")).toHaveValue(presetId, { timeout: 10000 });

    const slots = await rpcCall<FxSlot[]>(page, "read.getFxSlots", { trackIndex: 0 });
    const compressor = slots.find((slot) => slot.fxType === "compressor");
    if (!compressor) throw new Error("Compressor slot not found after addFxSlot");
    await rpcCall(page, "project.removeFxSlot", {
      trackIndex: 0,
      slotIndex: compressor.slotIndex,
    });

    await expect(async () => {
      const current = await rpcCall<FxSlot[]>(page, "read.getFxSlots", { trackIndex: 0 });
      expect(current.some((slot) => slot.fxType === "compressor")).toBe(false);
    }).toPass({ timeout: 10000 });

    await page.locator(".bt-tab", { hasText: "Mixer" }).click();
    await page.locator(".bt-tab", { hasText: "FX Chain" }).click();
    await expect(async () => {
      expect(await page.locator(".fx-slot").count()).toBe(slots.length - 1);
    }).toPass({ timeout: 10000 });

    await presetBar.locator("select").selectOption(presetId);
    await presetBar.getByRole("button", { name: "Apply" }).click();

    await expect(async () => {
      const restored = await rpcCall<FxSlot[]>(page, "read.getFxSlots", { trackIndex: 0 });
      expect(restored.some((slot) => slot.fxType === "compressor")).toBe(true);
    }).toPass({ timeout: 10000 });

    await expect(async () => {
      expect(await page.locator(".fx-slot").count()).toBe(slots.length);
    }).toPass({ timeout: 10000 });
  } finally {
    if (!presetId) {
      const presets = await rpcCall<FxChainPreset[]>(page, "project.listFxChainPresets", {});
      presetId = presets.find((preset) => preset.name === presetName)?.id ?? "";
    }
    if (presetId) await rpcCall(page, "project.deleteFxChainPreset", { id: presetId });
  }
});
