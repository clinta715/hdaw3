import { test, expect } from "@playwright/test";
import { startApp, rpcCall, waitForTrackCount } from "./helpers";

test.describe("Timeline context menu (user journeys)", () => {
  test.beforeEach(async ({ page }) => {
    await startApp(page);
  });

  async function rightClickEmptyTimeline(page: import("@playwright/test").Page) {
    const tracksInner = page.locator(".tl-tracks-inner");
    await expect(tracksInner).toBeVisible({ timeout: 5000 });
    const box = await tracksInner.boundingBox();
    if (!box) throw new Error(".tl-tracks-inner has no bounding box");
    await tracksInner.click({ button: "right", position: { x: box.width - 20, y: box.height - 20 } });
  }

  // Right-click in the dead space below the last track row (inside the scroll
  // container but outside .tl-tracks-inner).
  async function rightClickBelowTracks(page: import("@playwright/test").Page) {
    const tracks = page.locator(".tl-tracks");
    await expect(tracks).toBeVisible({ timeout: 5000 });
    const box = await tracks.boundingBox();
    if (!box) throw new Error(".tl-tracks has no bounding box");
    await tracks.click({ button: "right", position: { x: box.width - 30, y: box.height - 30 } });
  }

  async function waitForContextMenu(page: import("@playwright/test").Page) {
    await expect(page.locator(".clip-context-menu")).toBeVisible({ timeout: 5000 });
  }

  test("right-clicking empty timeline shows context menu with Add Audio Track", async ({ page }) => {
    await rightClickEmptyTimeline(page);
    await waitForContextMenu(page);
    await expect(page.locator(".clip-context-menu")).toContainText("Add Audio Track");
    await expect(page.locator(".clip-context-menu")).toContainText("Add MIDI Track");
  });

  test("right-clicking empty timeline shows Add MIDI Clip option", async ({ page }) => {
    await rightClickEmptyTimeline(page);
    await waitForContextMenu(page);
    await expect(page.locator(".clip-context-menu")).toContainText(/Add MIDI Clip/i);
  });

  test("right-clicking empty timeline shows Set Global BPM option", async ({ page }) => {
    await rightClickEmptyTimeline(page);
    await waitForContextMenu(page);
    await expect(page.locator(".clip-context-menu")).toContainText(/Set Global BPM/i);
  });

  test("Add Audio Track from context menu creates a new track", async ({ page }) => {
    const before = await page.locator(".th-row").count();
    await rightClickEmptyTimeline(page);
    await waitForContextMenu(page);
    await page.locator(".clip-context-menu button", { hasText: "Add Audio Track" }).click();
    await expect(page.locator(".th-row")).toHaveCount(before + 1, { timeout: 5000 });
  });

  test("Add MIDI Clip from context menu creates a clip", async ({ page }) => {
    await rightClickEmptyTimeline(page);
    await waitForContextMenu(page);
    await page.locator(".clip-context-menu button", { hasText: /Add MIDI Clip/i }).click();
    await expect(page.locator(".tl-clip").first()).toBeVisible({ timeout: 5000 });
  });

  test("right-clicking a clip shows clip-specific menu with Delete", async ({ page }) => {
    await rpcCall(page, "project.addMidiClip", { trackIndex: 0, start: 0, duration: 2, name: "E2E MIDI" });
    const clip = page.locator(".tl-clip").first();
    await expect(clip).toBeVisible({ timeout: 5000 });
    await clip.click({ button: "right" });
    const menu = page.locator(".clip-context-menu");
    await expect(menu).toBeVisible({ timeout: 3000 });
    await expect(menu).toContainText("Delete");
  });

  test("clip context menu shows Duplicate option", async ({ page }) => {
    await rpcCall(page, "project.addMidiClip", { trackIndex: 0, start: 0, duration: 2, name: "E2E MIDI" });
    const clip = page.locator(".tl-clip").first();
    await expect(clip).toBeVisible({ timeout: 5000 });
    await clip.click({ button: "right" });
    const menu = page.locator(".clip-context-menu");
    await expect(menu).toContainText("Duplicate");
  });

  test("clip context menu shows Split option", async ({ page }) => {
    await rpcCall(page, "project.addMidiClip", { trackIndex: 0, start: 0, duration: 2, name: "E2E MIDI" });
    const clip = page.locator(".tl-clip").first();
    await expect(clip).toBeVisible({ timeout: 5000 });
    await clip.click({ button: "right" });
    const menu = page.locator(".clip-context-menu");
    await expect(menu).toContainText("Split");
  });

  test("clip context menu shows Loop toggle", async ({ page }) => {
    await rpcCall(page, "project.addMidiClip", { trackIndex: 0, start: 0, duration: 2, name: "E2E MIDI" });
    const clip = page.locator(".tl-clip").first();
    await expect(clip).toBeVisible({ timeout: 5000 });
    await clip.click({ button: "right" });
    const menu = page.locator(".clip-context-menu");
    await expect(menu).toContainText(/Looped/i);
  });

  test("deleting a clip from context menu removes it", async ({ page }) => {
    const addedId = await rpcCall(page, "project.addMidiClip", { trackIndex: 0, start: 50, duration: 2, name: "E2E MIDI" });
    await expect(page.locator(`[data-clip-id="${addedId}"]`)).toBeVisible({ timeout: 5000 });
    const clip = page.locator(`[data-clip-id="${addedId}"]`);
    await clip.click({ button: "right" });
    await page.locator(".clip-context-menu button", { hasText: "Delete" }).click();
    await expect(page.locator(`[data-clip-id="${addedId}"]`)).not.toBeVisible({ timeout: 5000 });
  });

  test("clicking outside context menu closes it", async ({ page }) => {
    await rightClickEmptyTimeline(page);
    await waitForContextMenu(page);
    await page.locator(".tl-tracks-inner").click({ position: { x: 10, y: 10 } });
    await expect(page.locator(".clip-context-menu")).not.toBeVisible({ timeout: 3000 });
  });

  test("right-clicking below the existing tracks shows Add Audio Track", async ({ page }) => {
    await rightClickBelowTracks(page);
    await waitForContextMenu(page);
    await expect(page.locator(".clip-context-menu")).toContainText("Add Audio Track");
  });

  test("Add MIDI Track from below-tracks menu creates a MIDI track", async ({ page }) => {
    const before = await page.locator(".th-row").count();
    await rightClickBelowTracks(page);
    await waitForContextMenu(page);
    await page.locator(".clip-context-menu button", { hasText: "Add MIDI Track" }).click();
    await expect(page.locator(".th-row")).toHaveCount(before + 1, { timeout: 5000 });
    // The new track is typed MIDI (instrument badge).
    await expect(page.locator(".th-row").last().locator(".th-type-badge")).toContainText("\u266B", { timeout: 5000 });
  });

  test("empty-lane context menu shows Delete Track option", async ({ page }) => {
    await rightClickEmptyTimeline(page);
    await waitForContextMenu(page);
    await expect(page.locator(".clip-context-menu")).toContainText("Delete Track");
  });

  test("Delete Track from empty-lane menu removes the track", async ({ page }) => {
    const before = await page.locator(".th-row").count();
    await rpcCall(page, "project.addTrack", {});
    await expect(page.locator(".th-row")).toHaveCount(before + 1, { timeout: 5000 });
    await rightClickEmptyTimeline(page);
    await waitForContextMenu(page);
    await page.locator(".clip-context-menu button", { hasText: "Delete Track" }).click();
    await expect(page.locator(".th-row")).toHaveCount(before, { timeout: 5000 });
  });

  test("right-clicking a track header shows track operations", async ({ page }) => {
    const row = page.locator(".th-row").first();
    await row.click({ button: "right" });
    const menu = page.locator(".clip-context-menu");
    await expect(menu).toBeVisible({ timeout: 3000 });
    await expect(menu).toContainText("Add Audio Track");
    await expect(menu).toContainText("Add MIDI Track");
    await expect(menu).toContainText("Duplicate Track");
    await expect(menu).toContainText("Delete Track");
  });

  test("Delete Track from header menu removes the track", async ({ page }) => {
    const before = await page.locator(".th-row").count();
    await rpcCall(page, "project.addTrack", {});
    await expect(page.locator(".th-row")).toHaveCount(before + 1, { timeout: 5000 });
    await page.locator(".th-row").last().click({ button: "right" });
    await page.locator(".clip-context-menu button", { hasText: "Delete Track" }).click();
    await expect(page.locator(".th-row")).toHaveCount(before, { timeout: 5000 });
  });

  test("Duplicate Track from header menu adds a track", async ({ page }) => {
    const before = await page.locator(".th-row").count();
    await page.locator(".th-row").first().click({ button: "right" });
    await page.locator(".clip-context-menu button", { hasText: "Duplicate Track" }).click();
    await expect(page.locator(".th-row")).toHaveCount(before + 1, { timeout: 5000 });
  });

  test("the + button below the tracks offers a track-type choice", async ({ page }) => {
    // The previous test leaves an extra track; wait for the fresh project's
    // default 3 tracks so `before` isn't captured against the stale snapshot.
    await waitForTrackCount(page, 3);
    const before = await page.locator(".th-row").count();
    const addBtn = page.locator(".tl-add-track .add-track-trigger");
    await expect(addBtn).toBeVisible({ timeout: 5000 });
    await addBtn.click();
    // Popover offers both types.
    await expect(page.locator(".add-track-popover")).toContainText("Audio Track");
    await expect(page.locator(".add-track-popover")).toContainText("MIDI Track");
    await page.locator(".add-track-popover .add-track-opt", { hasText: "MIDI Track" }).click();
    await expect(page.locator(".th-row")).toHaveCount(before + 1, { timeout: 5000 });
    await expect(page.locator(".th-row").last().locator(".th-type-badge")).toContainText("\u266B", { timeout: 5000 });
  });

  test("Set Type from the header menu retypes a track", async ({ page }) => {
    // Default track 0 is audio; switch it to MIDI.
    await page.locator(".th-row").first().click({ button: "right" });
    await page.locator(".clip-context-menu button", { hasText: "Set Type: MIDI" }).click();
    await expect(page.locator(".th-row").first().locator(".th-type-badge")).toContainText("\u266B", { timeout: 5000 });
  });

  test("audio and MIDI tracks show distinct type chips", async ({ page }) => {
    await rpcCall(page, "project.addTrack", { trackType: 1 });
    const audioBadge = page.locator(".th-row").first().locator(".th-type-badge");
    const midiBadge = page.locator(".th-row").last().locator(".th-type-badge");
    await expect(audioBadge).toContainText("\u25B2"); // audio triangle
    await expect(midiBadge).toContainText("\u266B");  // midi note
  });
});
