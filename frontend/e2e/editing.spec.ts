import { test, expect } from "@playwright/test";
import * as fs from "fs";
import {
  startApp,
  rpcCall,
  clipLocator,
  clipLeft,
  addMidiClip,
  addAudioClip,
  dragClip,
  writeSineWav,
  tempWavPath,
} from "./helpers";

// User-journey regression tests. These drive the real app (engine + React +
// WebSocket) with simulated input and assert on the resulting state — the layer
// that unit tests miss and where the recurring interaction bugs live.
test.describe("Clip editing (user journeys)", () => {
  test("clicking an audio clip opens the editor and renders its waveform", async ({ page }) => {
    await startApp(page);
    const wavPath = tempWavPath("editor");
    writeSineWav(wavPath, { seconds: 1, freq: 220, amplitude: 0.8 });
    try {
      const clipId = await addAudioClip(page, {
        trackIndex: 0,
        start: 0,
        duration: 4,
        sourceFile: wavPath,
      });

      await clipLocator(page, clipId).click();
      await expect(page.locator(".audio-clip-editor")).toBeVisible({ timeout: 10000 });

      // Wait until the waveform canvas has actually drawn (peaks fetch is async).
      await expect
        .poll(
          async () =>
            page.evaluate(() => {
              const canvas = document.querySelector(".ace-waveform canvas") as HTMLCanvasElement | null;
              if (!canvas || canvas.width === 0) return false;
              const ctx = canvas.getContext("2d");
              if (!ctx) return false;
              const d = ctx.getImageData(0, 0, canvas.width, canvas.height).data;
              for (let i = 3; i < d.length; i += 4) if (d[i] > 0) return true;
              return false;
            }),
          { timeout: 15000 },
        )
        .toBe(true);

      // A rendered waveform is a polygon: transparent above/below it, opaque
      // inside. The flat-line fallback fills the WHOLE canvas with a gradient
      // (no transparent pixels) and a blank canvas has no opaque pixels — so
      // requiring both distinguishes a real waveform from either failure.
      const stats = await page.evaluate(() => {
        const canvas = document.querySelector(".ace-waveform canvas") as HTMLCanvasElement;
        const ctx = canvas.getContext("2d")!;
        const d = ctx.getImageData(0, 0, canvas.width, canvas.height).data;
        let hasTransparent = false;
        let hasOpaque = false;
        for (let i = 3; i < d.length; i += 4) {
          if (d[i] === 0) hasTransparent = true;
          else hasOpaque = true;
          if (hasTransparent && hasOpaque) break;
        }
        return { hasTransparent, hasOpaque };
      });
      expect(stats.hasOpaque).toBe(true);
      expect(stats.hasTransparent).toBe(true);
    } finally {
      if (fs.existsSync(wavPath)) fs.unlinkSync(wavPath);
    }
  });

  test("dragging a clip moves it and the position holds after reconciliation", async ({ page }) => {
    await startApp(page);
    const clipId = await addMidiClip(page, { trackIndex: 0, start: 0, duration: 2 });
    const before = await clipLeft(page, clipId);

    await dragClip(page, clipId, 120, 0);

    // Optimistic placement moves it immediately.
    await expect.poll(() => clipLeft(page, clipId), { timeout: 5000 }).toBeGreaterThan(before + 40);
    const moved = await clipLeft(page, clipId);

    // Let the moveClips RPC + delta reconcile, then confirm it didn't jump back
    // (the recurring stale-closure regression reverted drags on snapshot sync).
    await page.waitForTimeout(400);
    const settled = await clipLeft(page, clipId);
    expect(settled).toBeGreaterThan(before + 40);
    expect(Math.abs(settled - moved)).toBeLessThan(8);
  });

  test("ctrl-drag duplicates the clip and places the copy at the drop point", async ({ page }) => {
    await startApp(page);
    const clipId = await addMidiClip(page, { trackIndex: 0, start: 0, duration: 2 });
    const countBefore = await page.locator(".tl-clip").count();

    await dragClip(page, clipId, 160, 0, { modifiers: ["Control"] });

    // Wait for the duplicate to reconcile into the authoritative snapshot: track 0
    // should hold the original plus the copy.
    await expect
      .poll(async () => {
        const snap = await rpcCall<{ clips: { trackIndex: number }[] }>(page, "read.snapshot");
        return snap.clips.filter((c) => c.trackIndex === 0).length;
      }, { timeout: 5000 })
      .toBe(2);

    // The copy sits to the right of the original (placed at the drop point).
    const snap = await rpcCall<{ clips: { trackIndex: number; startBeat: number }[] }>(page, "read.snapshot");
    const starts = snap.clips
      .filter((c) => c.trackIndex === 0)
      .map((c) => c.startBeat)
      .sort((a, b) => a - b);
    expect(starts[1]).toBeGreaterThan(starts[0]);

    // The original clip is still on the timeline and the total count grew by one.
    await expect(clipLocator(page, clipId)).toBeVisible();
    await expect(page.locator(".tl-clip")).toHaveCount(countBefore + 1);
  });

  test("rubber-band selection selects clips that overlap the drag rectangle", async ({ page }) => {
    await startApp(page);
    const c1 = await addMidiClip(page, { trackIndex: 0, start: 0, duration: 2, name: "RB1" });
    const c2 = await addMidiClip(page, { trackIndex: 0, start: 10, duration: 2, name: "RB2" });

    const b1 = await clipLocator(page, c1).boundingBox();
    const b2 = await clipLocator(page, c2).boundingBox();
    if (!b1 || !b2) throw new Error("clips have no bounding box");

    // The track row is 40px tall; clips sit at y=4..36 inside it. Clicking
    // at y=2 hits the empty track-row background above the clip, so the
    // mousedown reaches the rubber-band handler on .tl-tracks-inner.
    const trackRow = page.locator(".tl-track-row").first();
    const rowBox = await trackRow.boundingBox();
    if (!rowBox) throw new Error("track row has no bounding box");

    const startX = rowBox.x + 5;
    const startY = rowBox.y + 2;
    // Drag far right to cover both clips regardless of pps.
    const dragDist = Math.max(b2.x + b2.width - startX + 150, 1200);

    await page.mouse.move(startX, startY);
    await page.mouse.down();
    await page.mouse.move(startX + dragDist, startY, { steps: 10 });
    await page.mouse.up();

    // Both clips should now be selected.
    await expect(clipLocator(page, c1)).toHaveClass(/tl-clip--selected/, { timeout: 5000 });
    await expect(clipLocator(page, c2)).toHaveClass(/tl-clip--selected/);
  });

  test("vertical drag on empty track background zooms the timeline", async ({ page }) => {
    await startApp(page);
    // Add clips so we can detect zoom changes via their rendered width
    const c1 = await addMidiClip(page, { trackIndex: 0, start: 0, duration: 4, name: "ZoomClip" });

    // Get the timeline tracks container
    const trackRow = page.locator(".tl-track-row").first();
    const rowBox = await trackRow.boundingBox();
    if (!rowBox) throw new Error("track row has no bounding box");

    const startX = rowBox.x + 5;
    const startY = rowBox.y + 2;

    // Record initial clip width
    const widthBefore = await clipLocator(page, c1).evaluate((el) => el.getBoundingClientRect().width);

    // Vertical drag downward (predominantly vertical: dy > dx*2) → zoom out
    await page.mouse.move(startX, startY);
    await page.mouse.down();
    await page.mouse.move(startX + 5, startY + 200, { steps: 15 });
    await page.mouse.up();

    // After zooming out, the clip should be narrower
    const widthAfter = await clipLocator(page, c1).evaluate((el) => el.getBoundingClientRect().width);
    expect(widthAfter).toBeLessThan(widthBefore);
  });

  test("undo and redo restore and re-apply clip changes", async ({ page }) => {
    await startApp(page);
    await expect
      .poll(() => page.locator(".tl-clip").count(), { timeout: 10000 })
      .toBe(2);
    const countBefore = 2;

    // Add a clip, then undo it — the clip should disappear.
    const clipId = await addMidiClip(page, { trackIndex: 0, start: 0, duration: 2, name: "Undo" });
    await expect.poll(() => page.locator(".tl-clip").count(), { timeout: 5000 }).toBe(countBefore + 1);

    await page.locator('header.transport-bar button[title="Undo (Ctrl+Z)"]').click();
    await expect.poll(() => page.locator(".tl-clip").count(), { timeout: 5000 }).toBe(countBefore);

    // Redo — the clip reappears.
    await page.locator('header.transport-bar button[title="Redo (Ctrl+Shift+Z)"]').click();
    await expect.poll(() => page.locator(".tl-clip").count(), { timeout: 5000 }).toBe(countBefore + 1);
    await expect(clipLocator(page, clipId)).toBeVisible();
  });

  test("multi-select drag moves all selected clips together", async ({ page }) => {
    await startApp(page);
    const c1 = await addMidiClip(page, { trackIndex: 0, start: 0, duration: 2, name: "MS1" });
    const c2 = await addMidiClip(page, { trackIndex: 0, start: 6, duration: 2, name: "MS2" });

    // Select both clips: click the first, then ctrl-click the second.
    await clipLocator(page, c1).click();
    await clipLocator(page, c2).click({ modifiers: ["Control"] });

    const left1Before = await clipLeft(page, c1);
    const left2Before = await clipLeft(page, c2);

    // Drag clip c1; both should move because they're selected.
    await dragClip(page, c1, 150, 0);

    // Wait for the batch move to reconcile, then confirm both moved right.
    await expect.poll(() => clipLeft(page, c1), { timeout: 5000 }).toBeGreaterThan(left1Before + 40);
    await page.waitForTimeout(400);

    const left1After = await clipLeft(page, c1);
    const left2After = await clipLeft(page, c2);
    expect(left1After).toBeGreaterThan(left1Before + 40);
    expect(left2After).toBeGreaterThan(left2Before + 40);

    // The relative distance between the two clips is preserved (both moved the
    // same amount), within pixel-rounding tolerance.
    const gapBefore = left2Before - left1Before;
    const gapAfter = left2After - left1After;
    expect(Math.abs(gapAfter - gapBefore)).toBeLessThan(10);
  });

  test("Delete key removes the selected clip", async ({ page }) => {
    await startApp(page);
    await expect
      .poll(() => page.locator(".tl-clip").count(), { timeout: 10000 })
      .toBe(2);
    const countBefore = 2;

    const clipId = await addMidiClip(page, { trackIndex: 0, start: 0, duration: 2, name: "Del" });
    await expect.poll(() => page.locator(".tl-clip").count(), { timeout: 5000 }).toBe(countBefore + 1);

    // Select the clip, then press Delete.
    await clipLocator(page, clipId).click();
    await page.keyboard.press("Delete");

    await expect.poll(() => page.locator(".tl-clip").count(), { timeout: 5000 }).toBe(countBefore);
    await expect(clipLocator(page, clipId)).toHaveCount(0);
  });

  test("Ctrl+D duplicates the selected clip via keyboard", async ({ page }) => {
    await startApp(page);
    await expect
      .poll(() => page.locator(".tl-clip").count(), { timeout: 10000 })
      .toBe(2);
    const countBefore = 2;

    const clipId = await addMidiClip(page, { trackIndex: 0, start: 0, duration: 2, name: "Dup" });
    await expect.poll(() => page.locator(".tl-clip").count(), { timeout: 5000 }).toBe(countBefore + 1);

    await clipLocator(page, clipId).click();
    await page.keyboard.press("Control+d");

    // The duplicate appears via placeholder → real clip reconciliation.
    await expect.poll(() => page.locator(".tl-clip").count(), { timeout: 5000 }).toBe(countBefore + 2);

    // The duplicate sits to the right of the original.
    const snap = await rpcCall<{ clips: { trackIndex: number; startBeat: number }[] }>(page, "read.snapshot");
    const starts = snap.clips
      .filter((c) => c.trackIndex === 0)
      .map((c) => c.startBeat)
      .sort((a, b) => a - b);
    expect(starts[starts.length - 1]).toBeGreaterThan(starts[0]);
  });

  test("Ctrl+C / Ctrl+V copy-paste creates a new clip", async ({ page }) => {
    await startApp(page);
    await expect
      .poll(() => page.locator(".tl-clip").count(), { timeout: 10000 })
      .toBe(2);
    const countBefore = 2;

    const clipId = await addMidiClip(page, { trackIndex: 0, start: 0, duration: 2, name: "Copy" });
    await expect.poll(() => page.locator(".tl-clip").count(), { timeout: 5000 }).toBe(countBefore + 1);

    await clipLocator(page, clipId).click();
    await page.keyboard.press("Control+c");

    // Deselect by clicking empty track background.
    const trackRow = page.locator(".tl-track-row").first();
    const rowBox = await trackRow.boundingBox();
    if (!rowBox) throw new Error("track row has no bounding box");
    await page.mouse.click(rowBox.x + rowBox.width - 10, rowBox.y + 20);

    await page.keyboard.press("Control+v");
    await expect.poll(() => page.locator(".tl-clip").count(), { timeout: 5000 }).toBe(countBefore + 2);
  });

  test("dragging a clip to another track changes its trackIndex", async ({ page }) => {
    await startApp(page);
    const clipId = await addMidiClip(page, { trackIndex: 0, start: 2, duration: 2, name: "Move" });

    // Verify it starts on track 0.
    const snapBefore = await rpcCall<{ clips: { clipId: number; trackIndex: number }[] }>(page, "read.snapshot");
    expect(snapBefore.clips.find((c) => c.clipId === clipId)?.trackIndex).toBe(0);

    // Drag the clip down past the track boundary (TRACK_HEIGHT = 40px).
    // A 60px downward drag moves it firmly into track 1.
    await dragClip(page, clipId, 0, 60);

    await expect
      .poll(async () => {
        const snap = await rpcCall<{ clips: { clipId: number; trackIndex: number }[] }>(page, "read.snapshot");
        return snap.clips.find((c) => c.clipId === clipId)?.trackIndex;
      }, { timeout: 5000 })
      .toBe(1);
  });

  test("clicking empty track background clears the selection", async ({ page }) => {
    await startApp(page);
    const clipId = await addMidiClip(page, { trackIndex: 0, start: 0, duration: 2, name: "Sel" });

    // Select the clip.
    await clipLocator(page, clipId).click();
    await expect(clipLocator(page, clipId)).toHaveClass(/tl-clip--selected/);

    // Click empty track background (right edge of the track row, well past
    // the clip which ends at beat 2).
    const trackRow = page.locator(".tl-track-row").first();
    const rowBox = await trackRow.boundingBox();
    if (!rowBox) throw new Error("track row has no bounding box");
    await page.mouse.click(rowBox.x + rowBox.width - 10, rowBox.y + 20);

    // Selection should be cleared.
    await expect(page.locator(".tl-clip--selected")).toHaveCount(0, { timeout: 3000 });
  });

  test("Ctrl+M merges selected MIDI clips into one", async ({ page }) => {
    await startApp(page);
    await expect
      .poll(() => page.locator(".tl-clip").count(), { timeout: 10000 })
      .toBe(2);
    const countBefore = 2;

    // Create two MIDI clips on the same track.
    const c1 = await addMidiClip(page, { trackIndex: 0, start: 0, duration: 2, name: "M1" });
    const c2 = await addMidiClip(page, { trackIndex: 0, start: 4, duration: 2, name: "M2" });
    await expect.poll(() => page.locator(".tl-clip").count(), { timeout: 5000 }).toBe(countBefore + 2);

    // Select both clips.
    await clipLocator(page, c1).click();
    await clipLocator(page, c2).click({ modifiers: ["Control"] });

    // Ensure focus is on body so the timeline keyboard handler fires
    // (the NoteGrid [tabindex] guard would swallow the shortcut otherwise).
    await page.evaluate(() => document.body.focus());

    // Merge via Ctrl+M.
    await page.keyboard.press("Control+m");

    // After merge: 2 original clips replaced by 1 merged clip → count drops by 1.
    await expect.poll(() => page.locator(".tl-clip").count(), { timeout: 5000 }).toBe(countBefore + 1);

    // The merged clip spans beats 0..6 (clip 1 at 0-2, clip 2 at 4-6).
    const snap = await rpcCall<{ clips: { trackIndex: number; startBeat: number; durationBeats: number }[] }>(
      page, "read.snapshot",
    );
    const track0 = snap.clips.filter((c) => c.trackIndex === 0);
    expect(track0.length).toBe(1);
    expect(track0[0].startBeat).toBe(0);
    expect(track0[0].durationBeats).toBe(6);

    // Undo restores the originals.
    await page.locator('header.transport-bar button[title="Undo (Ctrl+Z)"]').click();
    await expect.poll(() => page.locator(".tl-clip").count(), { timeout: 5000 }).toBe(countBefore + 2);
  });
});
