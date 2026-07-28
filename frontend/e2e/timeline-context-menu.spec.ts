import { test, expect } from "@playwright/test";
import { startApp, rpcCall } from "./helpers";

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

  test("right-clicking empty timeline shows context menu with Add Track", async ({ page }) => {
    await rightClickEmptyTimeline(page);
    await waitForContextMenu(page);
    await expect(page.locator(".clip-context-menu")).toContainText("Add Track");
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

  test("Add Track from context menu creates a new track", async ({ page }) => {
    const before = await page.locator(".th-row").count();
    await rightClickEmptyTimeline(page);
    await waitForContextMenu(page);
    await page.locator(".clip-context-menu button", { hasText: "Add Track" }).click();
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

  test("right-clicking below the existing tracks shows Add Track", async ({ page }) => {
    await rightClickBelowTracks(page);
    await waitForContextMenu(page);
    await expect(page.locator(".clip-context-menu")).toContainText("Add Track");
  });

  test("Add Track from below-tracks menu creates a track", async ({ page }) => {
    const before = await page.locator(".th-row").count();
    await rightClickBelowTracks(page);
    await waitForContextMenu(page);
    await page.locator(".clip-context-menu button", { hasText: "Add Track" }).click();
    await expect(page.locator(".th-row")).toHaveCount(before + 1, { timeout: 5000 });
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
    await expect(menu).toContainText("Add Track");
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
});
