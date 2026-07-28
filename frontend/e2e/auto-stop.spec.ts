import { test, expect } from "@playwright/test";
import { startApp, rpcCall } from "./helpers";

test.describe("Auto-stop (transport stops at end of clips)", () => {
  test.beforeEach(async ({ page }) => {
    await startApp(page);
  });

  test("transport auto-stops when playhead reaches end of all clips", async ({ page }) => {
    // Default project has MIDI clips ("Melody" 0–4 beats, "Chords" 4–8 beats)
    // stored as startTime/duration in seconds. projectEndSample = 8 * sr.
    const playBtn = page.locator("header.transport-bar .tb-play");
    await expect(playBtn).toContainText("▶");

    await playBtn.click();
    await expect(playBtn).toContainText("⏸", { timeout: 3000 });

    // Clips end at "8 seconds" (beats stored as seconds). Wait generously.
    await page.waitForTimeout(10000);

    // Transport should have auto-stopped
    await expect(playBtn).toContainText("▶", { timeout: 3000 });
  });
}, { timeout: 20000 });
