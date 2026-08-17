import { test, expect, Page } from "@playwright/test";
import * as fs from "fs";
import * as os from "os";
import * as path from "path";
import { startApp, rpcCall } from "./helpers";

/** Create a minimal valid DX7 single-voice SysEx file (163 bytes). */
function writeTestSysex(filePath: string): void {
  const voice = Buffer.alloc(155, 0);
  // Set algorithm = 5, feedback = 3 (indices 134, 135 in VCED)
  voice[134] = 5;
  voice[135] = 3;
  // Set voice name "E.PIANO  " at indices 145-154
  const name = "E.PIANO  ";
  for (let i = 0; i < name.length && 145 + i < 155; i++) {
    voice[145 + i] = name.charCodeAt(i);
  }

  const syx = Buffer.alloc(163);
  syx[0] = 0xf0; // SysEx start
  syx[1] = 0x43; // Yamaha
  syx[2] = 0x00; // sub-status 0
  syx[3] = 0x00; // format 0 (single voice)
  syx[4] = 0x00; // byte count MSB
  syx[5] = 0x9b; // byte count LSB (155)
  voice.copy(syx, 6);
  // Compute checksum over voice data (bytes 6..160)
  let sum = 0;
  for (let i = 6; i <= 160; i++) sum += syx[i];
  syx[161] = (~sum + 1) & 0x7f;
  syx[162] = 0xf7; // SysEx end

  fs.writeFileSync(filePath, syx);
}

/**
 * Set up an FM synth FX slot on track 0 and open the FX Chain panel so the
 * slot is visible. Returns the slotIndex and a locator for the rendered slot.
 *
 * Order matters: FXChain only mounts when its bottom tab is active, and it
 * fetches slots on mount using the currently selected track. So we select the
 * track first, add the slot via RPC, then open the tab.
 */
async function setupFmSynthSlot(page: Page) {
  // Select track 0 (sets selectedTrackIndex in the ui store)
  await page.locator(".th-row").first().click();

  // Add the FM synth slot via RPC
  await rpcCall(page, "project.addFxSlot", { trackIndex: 0, fxType: "fm_synth" });

  // Find the actual slot index (robust to any pre-existing slots)
  const slots = await rpcCall<{ slotIndex: number; fxType: string }[]>(
    page,
    "read.getFxSlots",
    { trackIndex: 0 },
  );
  const fmSlot = slots.find((s) => s.fxType === "fm_synth");
  if (!fmSlot) throw new Error("FM synth slot not found after addFxSlot");
  const slotIndex = fmSlot.slotIndex;

  // Open the FX Chain tab — mounts FXChain, which fetches slots for track 0
  await page.locator(".bt-tab", { hasText: "FX Chain" }).click();

  const slot = page.locator(
    `.fx-slot[data-track-index='0'][data-slot-index='${slotIndex}']`,
  );
  await expect(slot).toBeVisible({ timeout: 5000 });
  return { slotIndex, slot };
}

/** Dispatch a real HTML5 drag-and-drop of an internal file-browser file. */
async function dropInternalFile(page: Page, selector: string, filePath: string, fileName: string) {
  await page.evaluate(
    ([sel, p, n]) => {
      const el = document.querySelector(sel);
      if (!el) throw new Error(`drop target not found: ${sel}`);
      const dt = new DataTransfer();
      dt.setData("application/hdaw-file", JSON.stringify({ path: p, name: n }));
      el.dispatchEvent(new DragEvent("dragover", { bubbles: true, cancelable: true, dataTransfer: dt }));
      el.dispatchEvent(new DragEvent("drop", { bubbles: true, cancelable: true, dataTransfer: dt }));
    },
    [selector, filePath, fileName] as const,
  );
}

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

test.describe("FM Synth SysEx import (user journeys)", () => {
  const syxPath = path.join(os.tmpdir(), `hdaw-e2e-syx-${Date.now()}.syx`);

  test.beforeAll(() => {
    writeTestSysex(syxPath);
  });

  test.afterAll(() => {
    try { fs.unlinkSync(syxPath); } catch {}
  });

  test.beforeEach(async ({ page }) => {
    await startApp(page);
  });

  test("RPC fm_synthImportSysex imports a patch into an FM synth slot", async ({ page }) => {
    await rpcCall(page, "project.addFxSlot", { trackIndex: 0, fxType: "fm_synth" });

    const fxSlots = await rpcCall<{ slotIndex: number; fxType: string }[]>(page, "read.getFxSlots", { trackIndex: 0 });
    const fmSlot = fxSlots.find((s) => s.fxType === "fm_synth");
    expect(fmSlot).toBeDefined();
    const slotIndex = fmSlot!.slotIndex;

    const result = await rpcCall<{ ok: boolean; voiceName: string; algorithm: number }>(
      page,
      "audio.fm_synthImportSysex",
      { trackIndex: 0, slotIndex, filePath: syxPath },
    );

    expect(result.ok).toBe(true);
    expect(result.voiceName).toContain("E.PIANO");
    expect(result.algorithm).toBe(5);
  });

  test("drag-drop .syx onto FM synth slot triggers import", async ({ page }) => {
    const { slot } = await setupFmSynthSlot(page);

    await spyOnRpc(page);
    await dropInternalFile(page, ".fx-slot[data-track-index='0'][data-slot-index='0']", syxPath, "test_voice.syx");

    // The import is async — wait for the RPC to complete
    await expect(async () => {
      const calls = await page.evaluate(() => (window as any).__rpcCalls ?? []);
      expect(calls.some((c: { method: string }) => c.method === "audio.fm_synthImportSysex")).toBe(true);
    }).toPass({ timeout: 5000 });

    const calls = await page.evaluate(() => (window as any).__rpcCalls ?? []);
    const importCall = calls.find((c: { method: string }) => c.method === "audio.fm_synthImportSysex");
    expect(importCall.params).toMatchObject({ trackIndex: 0, slotIndex: 0, filePath: syxPath });

    // Slot still visible, no crash
    await expect(slot).toBeVisible();
  });

  test("dropping non-.syx file on FM synth slot is ignored", async ({ page }) => {
    const { slot } = await setupFmSynthSlot(page);

    await spyOnRpc(page);
    await dropInternalFile(page, ".fx-slot[data-track-index='0'][data-slot-index='0']", "/tmp/test.wav", "test.wav");
    await page.waitForTimeout(500);

    const calls = await page.evaluate(() => (window as any).__rpcCalls ?? []);
    expect(calls.some((c: { method: string }) => c.method === "audio.fm_synthImportSysex")).toBe(false);

    await expect(slot).toBeVisible();
  });

  test("dropping .syx on non-FM-synth slot is ignored", async ({ page }) => {
    // Select track 0, add an EQ slot, open the FX Chain tab
    await page.locator(".th-row").first().click();
    await rpcCall(page, "project.addFxSlot", { trackIndex: 0, fxType: "eq" });
    await page.locator(".bt-tab", { hasText: "FX Chain" }).click();
    const slot = page.locator(".fx-slot[data-track-index='0'][data-slot-index='0']");
    await expect(slot).toBeVisible({ timeout: 5000 });

    await spyOnRpc(page);
    await dropInternalFile(page, ".fx-slot[data-track-index='0'][data-slot-index='0']", syxPath, "test_voice.syx");
    await page.waitForTimeout(500);

    const calls = await page.evaluate(() => (window as any).__rpcCalls ?? []);
    expect(calls.some((c: { method: string }) => c.method === "audio.fm_synthImportSysex")).toBe(false);

    await expect(slot).toBeVisible();
    await expect(slot.locator(".fx-slot-type")).toHaveText("eq");
  });
});