import { test, expect } from "@playwright/test";
import { startApp, rpcCall } from "./helpers";

test.describe("Typed tracks (user journeys)", () => {
  test.beforeEach(async ({ page }) => {
    await startApp(page);
  });

  test("adding a folder track renders it with the folder icon", async ({ page }) => {
    await rpcCall(page, "project.addTrack", { trackType: 2 });
    const row = page.locator(".th-row.th-folder").first();
    await expect(row).toBeVisible({ timeout: 5000 });
    await expect(row.locator(".th-chevron")).toBeVisible();
  });

  test("setting trackType on an existing track updates the type badge", async ({ page }) => {
    await rpcCall(page, "project.setTrackType", { trackIndex: 0, trackType: 1 });
    const badge = page.locator(".th-row").first().locator(".th-type-badge");
    await expect(badge).toContainText("\u266B");
  });

  test("collapsing a folder hides its children in the track headers", async ({ page }) => {
    const folderIdx = await rpcCall<number>(page, "project.addTrack", { trackType: 2 });
    const childIdx = await rpcCall<number>(page, "project.addTrack", { trackType: 0 });
    await rpcCall(page, "project.moveTrackIntoFolder", {
      trackIndex: childIdx,
      folderIndex: folderIdx,
    });

    const childRow = page.locator(".th-row").filter({ hasText: `Track ${childIdx + 1}` });
    await expect(childRow).toBeVisible({ timeout: 5000 });

    const folderRow = page.locator(".th-row.th-folder").first();
    await folderRow.locator(".th-chevron").click();
    await expect(childRow).not.toBeVisible({ timeout: 3000 });

    await folderRow.locator(".th-chevron").click();
    await expect(childRow).toBeVisible({ timeout: 3000 });
  });

  test("collapsing a folder reduces visible timeline track rows", async ({ page }) => {
    const folderIdx = await rpcCall<number>(page, "project.addTrack", { trackType: 2 });
    const childIdx = await rpcCall<number>(page, "project.addTrack", { trackType: 0 });
    await rpcCall(page, "project.moveTrackIntoFolder", {
      trackIndex: childIdx,
      folderIndex: folderIdx,
    });

    await rpcCall(page, "project.addMidiClip", {
      trackIndex: childIdx,
      start: 0,
      duration: 2,
    });

    await expect(page.locator(".tl-track-row")).toHaveCount(3, { timeout: 5000 });

    const folderRow = page.locator(".th-row.th-folder").first();
    await folderRow.locator(".th-chevron").click();

    await expect(page.locator(".tl-track-row").nth(2)).not.toBeVisible({ timeout: 3000 });
  });

  test("child track is indented in the track headers", async ({ page }) => {
    const folderIdx = await rpcCall<number>(page, "project.addTrack", { trackType: 2 });
    const childIdx = await rpcCall<number>(page, "project.addTrack", { trackType: 0 });
    await rpcCall(page, "project.moveTrackIntoFolder", {
      trackIndex: childIdx,
      folderIndex: folderIdx,
    });

    const childRow = page.locator(".th-row").nth(childIdx);
    const pl = await childRow.evaluate((el) => parseInt(getComputedStyle(el).paddingLeft, 10));
    expect(pl).toBeGreaterThan(0);
  });

  test("muting a folder cascades mute to children (child M button lights up)", async ({ page }) => {
    const folderIdx = await rpcCall<number>(page, "project.addTrack", { trackType: 2 });
    const childIdx = await rpcCall<number>(page, "project.addTrack", { trackType: 0 });
    await rpcCall(page, "project.moveTrackIntoFolder", {
      trackIndex: childIdx,
      folderIndex: folderIdx,
    });

    await rpcCall(page, "project.setTrackMuted", { trackIndex: folderIdx, muted: true });

    const childRow = page.locator(".th-row").nth(childIdx);
    const muteBtn = childRow.locator(".th-mute");
    await expect(muteBtn).toHaveClass(/active/, { timeout: 3000 });

    await rpcCall(page, "project.setTrackMuted", { trackIndex: folderIdx, muted: false });
    await expect(muteBtn).not.toHaveClass(/active/, { timeout: 3000 });
  });

  test("existing tracks default to trackType 0 (audio)", async ({ page }) => {
    const badge = page.locator(".th-row").first().locator(".th-type-badge");
    // Audio type 0 uses the triangle icon
    await expect(badge).toContainText("\u25B2");
  });

  test("folder track gets the th-folder CSS class", async ({ page }) => {
    await rpcCall(page, "project.addTrack", { trackType: 2 });
    const folderRow = page.locator(".th-row.th-folder").first();
    await expect(folderRow).toBeVisible({ timeout: 5000 });
  });
});
