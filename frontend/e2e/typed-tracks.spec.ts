import { test, expect } from "@playwright/test";
import { startApp, rpcCall } from "./helpers";

test.describe("Typed tracks (user journeys)", () => {
  test.beforeEach(async ({ page }) => {
    await startApp(page);
  });

  test("adding a folder track renders it with the folder icon", async ({ page }) => {
    await rpcCall(page, "project.addTrack", { trackType: 2 });
    const folderRow = page.locator(".th-row.th-row--folder").first();
    await expect(folderRow).toBeVisible({ timeout: 10000 });
    await expect(folderRow.locator(".th-chevron")).toBeVisible();
  });

  test("setting trackType on an existing track updates the type badge", async ({ page }) => {
    await rpcCall(page, "project.setTrackType", { trackIndex: 0, trackType: 1 });
    const badge = page.locator(".th-row").first().locator(".th-type-badge");
    await expect(badge).toContainText("\u266B", { timeout: 10000 });
  });

  test("collapsing a folder hides its children in the track headers", async ({ page }) => {
    const initialCount = await page.locator(".th-row").count();

    const folderIndex = await rpcCall<number>(page, "project.addTrack", { trackType: 2 });
    const folderRow = page.locator(".th-row.th-row--folder").first();
    await expect(folderRow).toBeVisible({ timeout: 10000 });

    const childIndex = await rpcCall<number>(page, "project.addTrack", { trackType: 0 });
    const allRows = page.locator(".th-row");
    await expect(allRows).toHaveCount(initialCount + 2, { timeout: 10000 });

    // Move the audio track into the folder
    await rpcCall(page, "project.moveTrackIntoFolder", {
      trackIndex: Number(childIndex),
      folderIndex: Number(folderIndex),
    });

    const targetFolder = allRows.nth(Number(folderIndex));
    await expect(targetFolder).toBeVisible({ timeout: 5000 });
    const childRow = allRows.nth(Number(childIndex));

    // Collapse the folder
    await targetFolder.locator(".th-chevron").click();
    await expect(childRow).not.toBeVisible({ timeout: 5000 });

    // Expand it again
    await targetFolder.locator(".th-chevron").click();
    await expect(childRow).toBeVisible({ timeout: 5000 });
  });

  test("collapsing a folder reduces visible timeline track rows", async ({ page }) => {
    const folderIndex = await rpcCall<number>(page, "project.addTrack", { trackType: 2 });
    const folderRow = page.locator(".th-row.th-row--folder").first();
    await expect(folderRow).toBeVisible({ timeout: 10000 });

    const childIndex = await rpcCall<number>(page, "project.addTrack", { trackType: 0 });
    await rpcCall(page, "project.moveTrackIntoFolder", {
      trackIndex: Number(childIndex),
      folderIndex: Number(folderIndex),
    });

    await rpcCall(page, "project.addMidiClip", {
      trackIndex: Number(childIndex),
      start: 0,
      duration: 2,
      name: "E2E MIDI",
    });
    await expect(page.locator(".tl-clip").first()).toBeVisible({ timeout: 10000 });

    // Re-locate after the fullSync from moveTrackIntoFolder/setTrackCollapsed
    const folderRowAfter = page.locator(".th-row.th-row--folder").nth(0);
    await expect(folderRowAfter).toBeVisible({ timeout: 5000 });

    const beforeCount = await page.locator(".tl-track-row").count();
    await folderRowAfter.locator(".th-chevron").click();
    await expect(page.locator(".tl-track-row")).toHaveCount(beforeCount - 1, { timeout: 5000 });
  });

  test("child track is indented in the track headers", async ({ page }) => {
    const folderIndex = await rpcCall<number>(page, "project.addTrack", { trackType: 2 });
    await expect(page.locator(".th-row.th-row--folder").first()).toBeVisible({ timeout: 10000 });

    const childIndex = await rpcCall<number>(page, "project.addTrack", { trackType: 0 });
    await rpcCall(page, "project.moveTrackIntoFolder", {
      trackIndex: Number(childIndex),
      folderIndex: Number(folderIndex),
    });

    const childRow = page.locator(".th-row").nth(Number(childIndex));
    await expect(childRow).toBeVisible({ timeout: 10000 });
    const pl = await childRow.evaluate((el) => parseInt(getComputedStyle(el).paddingLeft, 10));
    expect(pl).toBeGreaterThan(0);
  });

  test("muting a folder cascades mute to children (child M button lights up)", async ({ page }) => {
    const folderIndex = await rpcCall<number>(page, "project.addTrack", { trackType: 2 });
    await expect(page.locator(".th-row.th-row--folder").first()).toBeVisible({ timeout: 10000 });

    const childIndex = await rpcCall<number>(page, "project.addTrack", { trackType: 0 });
    await rpcCall(page, "project.moveTrackIntoFolder", {
      trackIndex: Number(childIndex),
      folderIndex: Number(folderIndex),
    });

    // Wait for the DOM to reflect both new tracks (default 3 + folder + child = 5)
    await expect(page.locator(".th-row")).toHaveCount(3 + 2, { timeout: 5000 });

    await rpcCall(page, "project.setTrackMuted", { trackIndex: Number(folderIndex), muted: true });

    const childRow = page.locator(".th-row").nth(Number(childIndex));
    const muteBtn = childRow.locator(".th-mute");
    await expect(muteBtn).toHaveClass(/active/, { timeout: 10000 });

    await rpcCall(page, "project.setTrackMuted", { trackIndex: Number(folderIndex), muted: false });
    await expect(muteBtn).not.toHaveClass(/active/, { timeout: 10000 });
  });

  test("existing tracks default to trackType 0 (audio)", async ({ page }) => {
    const badge = page.locator(".th-row").first().locator(".th-type-badge");
    await expect(badge).toContainText("\u25B2");
  });

  test("folder track gets the th-folder CSS class", async ({ page }) => {
    await rpcCall(page, "project.addTrack", { trackType: 2 });
    const folderRow = page.locator(".th-row.th-row--folder").first();
    await expect(folderRow).toBeVisible({ timeout: 10000 });
  });
});
