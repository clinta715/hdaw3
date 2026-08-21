import { test, expect } from "@playwright/test";
import { startApp, rpcCall, addMidiClip, dragClip, clipLocator } from "./helpers";

interface ClipJson {
  clipId: number;
  trackIndex: number;
  startBeat: number;
  isGhost: boolean;
  ghostSourceId: number;
}

interface MarkerJson {
  index: number;
  name: string;
  time: number;
  color: number;
}

test.describe("batch A — timeline interactions", () => {
  test("ctrl+shift+drag creates a real ghost clip (isGhost + ghostSourceId)", async ({ page }) => {
    await startApp(page);
    const clipId = await addMidiClip(page, { trackIndex: 0, start: 0, duration: 2 });

    await dragClip(page, clipId, 320, 0, { modifiers: ["Control", "Shift"] });

    await expect(async () => {
      const snap = await rpcCall<{ clips: ClipJson[] }>(page, "read.snapshot");
      const ghost = snap.clips.find((c) => c.isGhost);
      expect(ghost).toBeTruthy();
      expect(ghost!.ghostSourceId).toBe(clipId);
      expect(ghost!.trackIndex).toBe(0);
      expect(ghost!.startBeat).toBeGreaterThan(0);
      expect(snap.clips.some((c) => c.clipId === clipId && !c.isGhost)).toBe(true);
    }).toPass({ timeout: 10000 });

    await expect(page.locator(".tl-clip--ghost")).toHaveCount(1, { timeout: 10000 });
    await expect(clipLocator(page, clipId)).toBeVisible();
  });

  test("multi-clip ctrl+shift+drag ghosts every selected clip", async ({ page }) => {
    await startApp(page);
    const first = await addMidiClip(page, { trackIndex: 0, start: 0, duration: 2 });
    const second = await addMidiClip(page, { trackIndex: 0, start: 4, duration: 2 });

    await clipLocator(page, first).click({ modifiers: ["Control"] });
    await clipLocator(page, second).click({ modifiers: ["Control"] });

    await dragClip(page, first, 320, 0, { modifiers: ["Control", "Shift"] });

    await expect(async () => {
      const snap = await rpcCall<{ clips: ClipJson[] }>(page, "read.snapshot");
      const ghosts = snap.clips.filter((c) => c.isGhost);
      expect(ghosts).toHaveLength(2);
      const sources = ghosts.map((g) => g.ghostSourceId).sort((a, b) => a - b);
      expect(sources).toEqual([first, second].sort((a, b) => a - b));
    }).toPass({ timeout: 10000 });
  });

  test("dragging a ruler marker moves it (project.setMarkerTime)", async ({ page }) => {
    await startApp(page);
    await rpcCall(page, "project.addMarker", { name: "E2E", time: 2 });
    const pin = page.locator(".tl-marker-pin").first();
    await expect(pin).toBeVisible({ timeout: 10000 });

    const before = await rpcCall<MarkerJson[]>(page, "read.getMarkers");
    expect(before).toHaveLength(1);
    expect(before[0].time).toBe(2);
    const pinLeftBefore = (await pin.boundingBox())!.x;

    const box = await pin.boundingBox();
    if (!box) throw new Error("marker pin has no bounding box");
    const startX = box.x + box.width / 2;
    const startY = box.y + box.height / 2;
    await page.mouse.move(startX, startY);
    await page.mouse.down();
    await page.mouse.move(startX + 320, startY, { steps: 10 });
    await page.mouse.up();

    await expect(async () => {
      const markers = await rpcCall<MarkerJson[]>(page, "read.getMarkers");
      expect(markers).toHaveLength(1);
      expect(markers[0].time).toBeGreaterThan(2);
    }).toPass({ timeout: 10000 });

    await expect(async () => {
      const left = (await pin.boundingBox())!.x;
      expect(left).toBeGreaterThan(pinLeftBefore + 40);
    }).toPass({ timeout: 10000 });
  });

  test("plain marker click still seeks and setMarkerTime round-trips via RPC", async ({ page }) => {
    await startApp(page);
    await rpcCall(page, "project.addMarker", { name: "E2E", time: 1 });
    const pin = page.locator(".tl-marker-pin").first();
    await expect(pin).toBeVisible({ timeout: 10000 });

    await pin.click();
    await expect(async () => {
      const pos = await rpcCall<{ currentTimeSeconds: number }>(page, "read.getTransport");
      expect(pos.currentTimeSeconds).toBeGreaterThan(0);
    }).toPass({ timeout: 10000 });

    await rpcCall(page, "project.setMarkerTime", { index: 0, time: 6 });
    await expect(async () => {
      const markers = await rpcCall<MarkerJson[]>(page, "read.getMarkers");
      expect(markers[0].time).toBe(6);
    }).toPass({ timeout: 10000 });
  });
});
