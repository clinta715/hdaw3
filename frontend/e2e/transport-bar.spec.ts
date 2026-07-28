import { test, expect } from "@playwright/test";
import { startApp, rpcCall } from "./helpers";

test.describe("Transport bar (user journeys)", () => {
  test.beforeEach(async ({ page }) => {
    await startApp(page);
  });

  test("play button starts playback and toggles to pause icon", async ({ page }) => {
    const playBtn = page.locator("header.transport-bar .tb-play");
    await expect(playBtn).toContainText("▶");

    await playBtn.click();
    await expect(playBtn).toContainText("⏸", { timeout: 3000 });
  });

  test("pause button stops playback and reverts to play icon", async ({ page }) => {
    const playBtn = page.locator("header.transport-bar .tb-play");
    await playBtn.click();
    await expect(playBtn).toContainText("⏸", { timeout: 3000 });

    await playBtn.click();
    await expect(playBtn).toContainText("▶", { timeout: 3000 });
  });

  test("stop button stops playback", async ({ page }) => {
    const playBtn = page.locator("header.transport-bar .tb-play");
    await playBtn.click();
    await expect(playBtn).toContainText("⏸", { timeout: 3000 });

    await page.locator('header.transport-bar button[title="Stop"]').click();
    await expect(playBtn).toContainText("▶", { timeout: 3000 });
  });

  test("rewind button resets playhead to zero", async ({ page }) => {
    await page.locator('header.transport-bar button[title="Rewind"]').click();
    const time = page.locator("header.transport-bar .tb-time");
    await expect(time).toContainText("0:00", { timeout: 3000 });
  });

  test("loop button toggles loop state", async ({ page }) => {
    const loopBtn = page.locator('header.transport-bar button[title="Toggle Loop"]');
    await expect(loopBtn).not.toHaveClass(/active/);
    await loopBtn.click();
    await expect(loopBtn).toHaveClass(/active/, { timeout: 3000 });
    await loopBtn.click();
    await expect(loopBtn).not.toHaveClass(/active/, { timeout: 3000 });
  });

  test("BPM display shows default 120.0 BPM", async ({ page }) => {
    await expect(page.locator("header.transport-bar .tb-bpm")).toContainText("120.0 BPM");
  });

  test("double-clicking BPM opens inline editor", async ({ page }) => {
    const bpm = page.locator("header.transport-bar .tb-bpm");
    await bpm.dblclick();
    const input = page.locator("header.transport-bar .tb-bpm-input");
    await expect(input).toBeVisible({ timeout: 3000 });
    await expect(input).toHaveValue("120.0");
  });

  test("setting BPM via inline editor updates the display", async ({ page }) => {
    const bpm = page.locator("header.transport-bar .tb-bpm");
    await bpm.dblclick();
    const input = page.locator("header.transport-bar .tb-bpm-input");
    await input.fill("140");
    await input.press("Enter");

    await expect(bpm).toContainText("140.0 BPM", { timeout: 3000 });
  });

  test("time signature displays 4/4 by default", async ({ page }) => {
    await expect(page.locator("header.transport-bar .tb-time-sig")).toContainText("4/4");
  });

  test("add track button adds a new track to the headers", async ({ page }) => {
    const before = await page.locator(".th-row").count();
    await page.locator('header.transport-bar button[title="Add Track"]').click();
    await expect(page.locator(".th-row")).toHaveCount(before + 1, { timeout: 5000 });
  });

  test("remove track button removes the selected track", async ({ page }) => {
    const before = await page.locator(".th-row").count();
    // Select the second track (index 1) — track 0 may be the master/output track
    // and can't be removed. Click the type-badge to avoid .th-color's stopPropagation.
    const secondTrack = page.locator(".th-row").nth(1);
    await secondTrack.locator(".th-type-badge").click();
    await page.locator('header.transport-bar button[title="Remove Track"]').click();
    await expect(page.locator(".th-row")).toHaveCount(before - 1, { timeout: 5000 });
  });

  test("snap toggle button toggles snap state", async ({ page }) => {
    const snapBtn = page.locator('header.transport-bar button[title="Toggle Snap"]');
    const wasActive = (await snapBtn.getAttribute("class"))?.includes("active") ?? false;
    await snapBtn.click();
    if (wasActive) {
      await expect(snapBtn).not.toHaveClass(/active/, { timeout: 5000 });
    } else {
      await expect(snapBtn).toHaveClass(/active/, { timeout: 5000 });
    }
  });

  test("metronome toggle button toggles metronome", async ({ page }) => {
    const metroBtn = page.locator('header.transport-bar button[title="Metronome"]');
    await metroBtn.click();
    await expect(metroBtn).toHaveClass(/active/, { timeout: 3000 });
    await metroBtn.click();
    await expect(metroBtn).not.toHaveClass(/active/, { timeout: 3000 });
  });

  test("record button shows recording state", async ({ page }) => {
    const recBtn = page.locator('header.transport-bar button[title="Record"]');
    await recBtn.click();
    await expect(recBtn).toHaveClass(/recording/, { timeout: 5000 });
    // Click Record again to toggle recording off (transport.stop doesn't stop recording)
    await recBtn.click();
    await expect(recBtn).not.toHaveClass(/recording/, { timeout: 5000 });
  });

  test("undo and redo buttons are visible", async ({ page }) => {
    await expect(page.locator('header.transport-bar button[title="Undo (Ctrl+Z)"]')).toBeVisible();
    await expect(page.locator('header.transport-bar button[title="Redo (Ctrl+Shift+Z)"]')).toBeVisible();
  });
});
