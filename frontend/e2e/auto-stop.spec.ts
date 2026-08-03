import { test, expect } from "@playwright/test";
import { startApp, rpcCall, addMidiClip } from "./helpers";

test.describe("Auto-stop (transport stops at end of clips)", () => {
  test.beforeEach(async ({ page }) => {
    await startApp(page);
  });

  test("transport auto-stops when playhead reaches end of all clips", async ({ page }) => {
    // The default project ships empty; add a MIDI clip so auto-stop has a
    // target. duration is in beats; at 120 BPM, 8 beats = 4 seconds.
    await addMidiClip(page, { trackIndex: 0, start: 0, duration: 8, name: "AutoStop" });

    const playBtn = page.locator("header.transport-bar .tb-play");
    await expect(playBtn).toContainText("▶");

    await playBtn.click();
    await expect(playBtn).toContainText("⏸", { timeout: 3000 });

    // Clip ends at ~4 seconds; wait generously for auto-stop.
    await page.waitForTimeout(7000);

    // Transport should have auto-stopped
    await expect(playBtn).toContainText("▶", { timeout: 3000 });
  });
}, { timeout: 20000 });
