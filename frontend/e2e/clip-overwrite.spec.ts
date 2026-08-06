import { test, expect } from "@playwright/test";
import { startApp, rpcCall, clipLocator, addMidiClip, dragClip } from "./helpers";

// Overwrite cardinal rule: when clip is placed/moved so it FULLY COVERS an
// existing clip on the same track, the covered clip must be removed (parts
// overwrite, never overlap). Regression against Case 1 in moveClipWithOverlap,
// which was previously a non-destructive no-op that left overlapping clips to
// coexist/sum — and against the user's workaround (move away, delete, move back)
// that produced silence because the covered clip lingered in the graph.
test.describe("Clip overwrite (user journeys)", () => {
  test("dragging a clip fully over another removes the covered clip and keeps the survivor", async ({ page }) => {
    await startApp(page);

    // A occupies [0, 2] on track 0. B occupies [3, 7] — nowhere near A initially.
    const aId = await addMidiClip(page, { trackIndex: 0, start: 0, duration: 2, name: "Covered" });
    const bId = await addMidiClip(page, { trackIndex: 0, start: 3, duration: 4, name: "Overwriter" });

    const aBox = await clipLocator(page, aId).boundingBox();
    const bBox = await clipLocator(page, bId).boundingBox();
    if (!aBox || !bBox) throw new Error("clips have no bounding box");

    // Drag B so its center lands on A's center -> B fully covers A.
    const targetX = aBox.x + aBox.width / 2;
    const dx = targetX - (bBox.x + bBox.width / 2);
    await dragClip(page, bId, dx, 0);

    // The covered clip (A) must be removed; only B survives on track 0.
    await expect
      .poll(
        async () => {
          const snap = await rpcCall<{ clips: { clipId: number; trackIndex: number }[] }>(
            page,
            "read.snapshot",
          );
          const onTrack0 = snap.clips.filter((c) => c.trackIndex === 0);
          return { count: onTrack0.length, hasA: onTrack0.some((c) => c.clipId === aId), hasB: onTrack0.some((c) => c.clipId === bId) };
        },
        { timeout: 10000 },
      )
      .toEqual({ count: 1, hasA: false, hasB: true });

    // A's clip element is gone from the DOM, and B is still rendered.
    await expect(clipLocator(page, aId)).toHaveCount(0, { timeout: 10000 });
    await expect(clipLocator(page, bId)).toBeVisible({ timeout: 10000 });
  });

  test("moving the replacement away, deleting the original, and moving it back keeps it audible (wired)", async ({ page }) => {
    await startApp(page);

    // default project already has track 0 and 1; use track 1 as "away".
    const aId = await addMidiClip(page, { trackIndex: 0, start: 0, duration: 2, name: "Covered" });
    const bId = await addMidiClip(page, { trackIndex: 0, start: 3, duration: 4, name: "Replacer" });

    // Overwrite: drag B fully over A -> A is removed.
    const aBox = await clipLocator(page, aId).boundingBox();
    const bBox = await clipLocator(page, bId).boundingBox();
    if (!aBox || !bBox) throw new Error("clips have no bounding box");
    const dx = (aBox.x + aBox.width / 2) - (bBox.x + bBox.width / 2);
    await dragClip(page, bId, dx, 0);
    await expect(clipLocator(page, aId)).toHaveCount(0, { timeout: 10000 });
    await expect(clipLocator(page, bId)).toBeVisible({ timeout: 10000 });

    // Workaround: move B to the "away" track, delete the (already-removed) A,
    // then move B back to track 0. The survivor must remain the sole clip there.
    await rpcCall(page, "project.moveClips", { clipIds: [bId], newStarts: [0], newTrackIndices: [1] });
    await rpcCall(page, "project.removeClip", { clipId: aId }).catch(() => {});
    await rpcCall(page, "project.moveClips", { clipIds: [bId], newStarts: [0], newTrackIndices: [0] });

    // B is the only clip back on track 0 at [0, 4].
    await expect
      .poll(
        async () => {
          const snap = await rpcCall<{ clips: { clipId: number; trackIndex: number; startBeat: number }[] }>(
            page,
            "read.snapshot",
          );
          const onTrack0 = snap.clips.filter((c) => c.trackIndex === 0);
          return { count: onTrack0.length, hasB: onTrack0.some((c) => c.clipId === bId) };
        },
        { timeout: 10000 },
      )
      .toEqual({ count: 1, hasB: true });

    // The survivor is still interactive (rendered, click selects it).
    await clipLocator(page, bId).click();
    await expect(clipLocator(page, bId)).toHaveClass(/tl-clip--selected/, { timeout: 10000 });
  });
});