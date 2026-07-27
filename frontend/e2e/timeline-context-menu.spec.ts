import { test, expect } from "@playwright/test";
import { startApp, rpcCall } from "./helpers";

test.describe("Timeline context menu (user journeys)", () => {
  test.beforeEach(async ({ page }) => {
    await startApp(page);
  });

  test("right-clicking empty timeline shows context menu with Add Track", async ({ page }) => {
    const timeline = page.locator(".tl-tracks-inner, .timeline-minimal");
    await timeline.click({ button: "right", position: { x: 200, y: 100 } });
    const menu = page.locator(".clip-context-menu");
    await expect(menu).toBeVisible({ timeout: 3000 });
    await expect(menu).toContainText("Add Track");
  });

  test("right-clicking empty timeline shows Add MIDI Clip option", async ({ page }) => {
    const timeline = page.locator(".tl-tracks-inner, .timeline-minimal");
    await timeline.click({ button: "right", position: { x: 200, y: 100 } });
    const menu = page.locator(".clip-context-menu");
    await expect(menu).toBeVisible({ timeout: 3000 });
    await expect(menu).toContainText(/Add MIDI Clip/i);
  });

  test("right-clicking empty timeline shows Set Global BPM option", async ({ page }) => {
    const timeline = page.locator(".tl-tracks-inner, .timeline-minimal");
    await timeline.click({ button: "right", position: { x: 200, y: 100 } });
    const menu = page.locator(".clip-context-menu");
    await expect(menu).toBeVisible({ timeout: 3000 });
    await expect(menu).toContainText(/Set Global BPM/i);
  });

  test("Add Track from context menu creates a new track", async ({ page }) => {
    const before = await page.locator(".th-row").count();
    const timeline = page.locator(".tl-tracks-inner, .timeline-minimal");
    await timeline.click({ button: "right", position: { x: 200, y: 100 } });
    await page.locator(".clip-context-menu button", { hasText: "Add Track" }).click();
    await expect(page.locator(".th-row")).toHaveCount(before + 1, { timeout: 5000 });
  });

  test("Add MIDI Clip from context menu creates a clip", async ({ page }) => {
    const timeline = page.locator(".tl-tracks-inner, .timeline-minimal");
    await timeline.click({ button: "right", position: { x: 200, y: 100 } });
    await page.locator(".clip-context-menu button", { hasText: /Add MIDI Clip/i }).click();
    // A clip should appear on the timeline
    await expect(page.locator(".tl-clip").first()).toBeVisible({ timeout: 5000 });
  });

  test("right-clicking a clip shows clip-specific menu with Delete", async ({ page }) => {
    await rpcCall(page, "project.addMidiClip", { trackIndex: 0, start: 0, duration: 2 });
    const clip = page.locator(".tl-clip").first();
    await expect(clip).toBeVisible({ timeout: 5000 });
    await clip.click({ button: "right" });
    const menu = page.locator(".clip-context-menu");
    await expect(menu).toBeVisible({ timeout: 3000 });
    await expect(menu).toContainText("Delete");
  });

  test("clip context menu shows Duplicate option", async ({ page }) => {
    await rpcCall(page, "project.addMidiClip", { trackIndex: 0, start: 0, duration: 2 });
    const clip = page.locator(".tl-clip").first();
    await expect(clip).toBeVisible({ timeout: 5000 });
    await clip.click({ button: "right" });
    const menu = page.locator(".clip-context-menu");
    await expect(menu).toContainText("Duplicate");
  });

  test("clip context menu shows Split option", async ({ page }) => {
    await rpcCall(page, "project.addMidiClip", { trackIndex: 0, start: 0, duration: 2 });
    const clip = page.locator(".tl-clip").first();
    await expect(clip).toBeVisible({ timeout: 5000 });
    await clip.click({ button: "right" });
    const menu = page.locator(".clip-context-menu");
    await expect(menu).toContainText("Split");
  });

  test("clip context menu shows Loop toggle", async ({ page }) => {
    await rpcCall(page, "project.addMidiClip", { trackIndex: 0, start: 0, duration: 2 });
    const clip = page.locator(".tl-clip").first();
    await expect(clip).toBeVisible({ timeout: 5000 });
    await clip.click({ button: "right" });
    const menu = page.locator(".clip-context-menu");
    await expect(menu).toContainText(/Looped/i);
  });

  test("deleting a clip from context menu removes it", async ({ page }) => {
    await rpcCall(page, "project.addMidiClip", { trackIndex: 0, start: 0, duration: 2 });
    await expect(page.locator(".tl-clip").first()).toBeVisible({ timeout: 5000 });
    const clip = page.locator(".tl-clip").first();
    await clip.click({ button: "right" });
    await page.locator(".clip-context-menu button", { hasText: "Delete" }).click();
    await expect(page.locator(".tl-clip")).toHaveCount(0, { timeout: 5000 });
  });

  test("clicking outside context menu closes it", async ({ page }) => {
    const timeline = page.locator(".tl-tracks-inner, .timeline-minimal");
    await timeline.click({ button: "right", position: { x: 200, y: 100 } });
    await expect(page.locator(".clip-context-menu")).toBeVisible({ timeout: 3000 });
    await page.locator(".timeline-minimal").click({ position: { x: 50, y: 50 } });
    await expect(page.locator(".clip-context-menu")).not.toBeVisible({ timeout: 3000 });
  });
});
