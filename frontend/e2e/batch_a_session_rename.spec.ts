import { test, expect } from "@playwright/test";
import { startApp, rpcCall, addMidiClip } from "./helpers";

type ClipSnap = { clipId: number; name: string; sceneIndex?: number };

test.describe("Clip rename and session features", () => {
  test.beforeEach(async ({ page }) => {
    await startApp(page);
  });

  test("rename clip via RPC and verify snapshot reflects new name", async ({ page }) => {
    const clipId = await addMidiClip(page, { name: "Original" });

    await rpcCall(page, "project.setClipName", { clipId, name: "Renamed" });

    await expect(async () => {
      const clip = await rpcCall<ClipSnap>(page, "read.getClip", { clipId });
      expect(clip.name).toBe("Renamed");
    }).toPass({ timeout: 10000 });
  });

  test("rename clip via context menu prompt", async ({ page }) => {
    const clipId = await addMidiClip(page, { name: "BeforeRename" });

    const clipEl = page.locator(`.tl-clip[data-clip-id="${clipId}"]`);
    await clipEl.click({ button: "right" });

    const renameBtn = page.locator(".clip-context-menu button", { hasText: "Rename" });
    await expect(renameBtn).toBeVisible();

    page.once("dialog", (dialog) => dialog.accept("AfterRename"));
    await renameBtn.click();

    await expect(async () => {
      const clip = await rpcCall<ClipSnap>(page, "read.getClip", { clipId });
      expect(clip.name).toBe("AfterRename");
    }).toPass({ timeout: 10000 });
  });

  test("Inspector shows editable clip name", async ({ page }) => {
    const clipId = await addMidiClip(page, { name: "InspClip" });

    await page.locator(`.tl-clip[data-clip-id="${clipId}"]`).click();

    await page.locator('button:has-text("Inspector")').click();
    await page.waitForTimeout(300);

    const nameInput = page.locator(".insp-panel .insp-input[type=\"text\"]").first();
    await expect(nameInput).toBeVisible({ timeout: 5000 });
    await expect(nameInput).toHaveValue("InspClip");

    await nameInput.fill("NewName");
    await nameInput.press("Enter");

    await expect(async () => {
      const clip = await rpcCall<ClipSnap>(page, "read.getClip", { clipId });
      expect(clip.name).toBe("NewName");
    }).toPass({ timeout: 10000 });
  });

  test("assign clip to scene via RPC", async ({ page }) => {
    const clipId = await addMidiClip(page, { name: "SceneClip" });

    await rpcCall(page, "session.setClipScene", { clipId, sceneIndex: 2 });

    await expect(async () => {
      const snapshot = await rpcCall<{ clips: ClipSnap[] }>(page, "read.snapshot");
      const clip = snapshot.clips.find((c) => c.clipId === clipId);
      expect(clip?.sceneIndex).toBe(2);
    }).toPass({ timeout: 10000 });
  });

  test("remove clip from session via RPC", async ({ page }) => {
    const clipId = await addMidiClip(page, { name: "RemoveScene" });
    await rpcCall(page, "session.setClipScene", { clipId, sceneIndex: 3 });

    await rpcCall(page, "session.setClipScene", { clipId, sceneIndex: -1 });

    await expect(async () => {
      const snapshot = await rpcCall<{ clips: ClipSnap[] }>(page, "read.snapshot");
      const clip = snapshot.clips.find((c) => c.clipId === clipId);
      expect(clip?.sceneIndex == null || clip?.sceneIndex < 0).toBe(true);
    }).toPass({ timeout: 10000 });
  });

  test("Stop All button exists in session view and clicking does not error", async ({ page }) => {
    await page.locator('[title="Toggle Session/Arrangement View (Tab)"]').click();
    await expect(page.locator(".sv-root")).toBeVisible();

    const stopAllBtn = page.locator("button", { hasText: "Stop All" });
    await expect(stopAllBtn).toBeVisible({ timeout: 5000 });
    await stopAllBtn.click();
  });

  test("Assign to Scene submenu appears in clip context menu", async ({ page }) => {
    const clipId = await addMidiClip(page, { name: "SubmenuClip" });

    const clipEl = page.locator(`.tl-clip[data-clip-id="${clipId}"]`);
    await clipEl.click({ button: "right" });

    const assignBtn = page.locator(".clip-context-menu button", { hasText: "Assign to Scene" });
    await expect(assignBtn).toBeVisible();
  });
});
