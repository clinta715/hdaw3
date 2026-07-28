import { test, expect, Page } from "@playwright/test";
import {
  startApp,
  rpcCall,
  clipLocator,
  addMidiClip,
  addAudioClip,
  writeSineWav,
  tempWavPath,
} from "./helpers";
import * as fs from "fs";

// Targeted React error #300 reproduction: exercise every path where a clip
// editor (ClipEditor / AudioClipEditor / PianoRoll) is mounted, then the
// selected clip is deleted — forcing the editor to unmount mid-render.
// If the sentinel catches error #300, the test fails with the component name.

const errors: string[] = [];
const sentinelLogs: string[] = [];

function attachSentinelMonitoring(page: Page) {
  page.on("console", (m) => {
    const text = m.text();
    if (text.includes("[HookSentinel]")) {
      sentinelLogs.push(text);
    }
    if (m.type() === "error") {
      errors.push(text);
    }
  });
  page.on("pageerror", (e) => {
    errors.push(`[pageerror] ${e.message}`);
  });
}

test.afterEach(async ({}, info) => {
  const s = `=== "${info.title}" ===\nsentinel:\n${sentinelLogs.join("\n") || "(none)"}\nerrors:\n${errors.join("\n") || "(none)"}`;
  console.log(s);
  await info.attach("diagnostics", { body: s, contentType: "text/plain" });
  errors.length = 0;
  sentinelLogs.length = 0;
});

test.describe("React error #300: delete-while-editor-open", () => {
  test("MIDI clip: select → piano-roll opens → delete → no #300", async ({ page }) => {
    attachSentinelMonitoring(page);
    await startApp(page);

    const clipId = await addMidiClip(page, {
      trackIndex: 0,
      start: 0,
      duration: 4,
      name: "DelMIDI",
    });

    // Select the clip — this opens the ClipEditor and auto-switches to piano-roll
    await clipLocator(page, clipId).click();

    // Wait for the editor to be visible
    await expect(page.locator(".clip-editor-container")).toBeVisible({ timeout: 5000 });
    await expect(page.locator(".piano-roll")).toBeVisible({ timeout: 5000 });

    // Delete while the editor is open — this is the crash window
    await page.keyboard.press("Delete");

    // Wait for the clip to be removed
    await expect(clipLocator(page, clipId)).toHaveCount(0, { timeout: 5000 });

    // Wait a beat for any async reconciliation to settle
    await page.waitForTimeout(500);

    // Check for sentinel errors
    const hookErrors = sentinelLogs.filter((l) => l.includes("#300"));
    expect(hookErrors).toHaveLength(0);

    // Also check for page errors
    const fatalErrors = errors.filter(
      (e) => e.includes("#300") || e.includes("fewer hooks")
    );
    expect(fatalErrors).toHaveLength(0);
  });

  test("Audio clip: select → audio-editor opens → delete → no #300", async ({ page }) => {
    attachSentinelMonitoring(page);
    await startApp(page);

    const wavPath = tempWavPath("del-audio");
    writeSineWav(wavPath, { seconds: 2, freq: 440 });

    try {
      const clipId = await addAudioClip(page, {
        trackIndex: 0,
        start: 0,
        duration: 4,
        sourceFile: wavPath,
        name: "DelAudio",
      });

      // Select — opens ClipEditor + auto-switches to audio-editor
      await clipLocator(page, clipId).click();
      await expect(page.locator(".clip-editor-container")).toBeVisible({ timeout: 5000 });
      await expect(page.locator(".audio-clip-editor")).toBeVisible({ timeout: 5000 });

      // Delete while audio editor is open
      await page.keyboard.press("Delete");

      await expect(clipLocator(page, clipId)).toHaveCount(0, { timeout: 5000 });
      await page.waitForTimeout(500);

      const hookErrors = sentinelLogs.filter((l) => l.includes("#300"));
      expect(hookErrors).toHaveLength(0);

      const fatalErrors = errors.filter(
        (e) => e.includes("#300") || e.includes("fewer hooks")
      );
      expect(fatalErrors).toHaveLength(0);
    } finally {
      if (fs.existsSync(wavPath)) fs.unlinkSync(wavPath);
    }
  });

  test("rapid create-select-delete loop stress test", async ({ page }) => {
    test.setTimeout(60_000);
    attachSentinelMonitoring(page);
    await startApp(page);

    for (let i = 0; i < 20; i++) {
      const isMidi = i % 2 === 0;
      let clipId: number;

      if (isMidi) {
        clipId = await addMidiClip(page, {
          trackIndex: 0,
          start: 0,
          duration: 4,
          name: `Stress${i}`,
        });
      } else {
        const wavPath = tempWavPath(`stress${i}`);
        writeSineWav(wavPath, { seconds: 1, freq: 220 + i * 10 });
        try {
          clipId = await addAudioClip(page, {
            trackIndex: 0,
            start: 0,
            duration: 4,
            sourceFile: wavPath,
            name: `Stress${i}`,
          });
        } finally {
          if (fs.existsSync(wavPath)) fs.unlinkSync(wavPath);
        }
      }

      // Select to open the editor
      await clipLocator(page, clipId).click();
      await page.waitForTimeout(100);

      // Delete immediately — minimal time for the editor to settle
      await page.keyboard.press("Delete");

      // Wait for deletion to complete
      await expect(clipLocator(page, clipId)).toHaveCount(0, { timeout: 5000 });

      // Quick health check
      const alive = await page.evaluate(() => 1, { timeout: 3000 }).catch(() => 0);
      expect(alive).toBe(1);

      const boundary = await page.locator("text=HDAW hit a render error").count();
      expect(boundary).toBe(0);
    }

    // Final check for sentinel errors
    const hookErrors = sentinelLogs.filter((l) => l.includes("#300"));
    expect(hookErrors).toHaveLength(0);

    const fatalErrors = errors.filter(
      (e) => e.includes("#300") || e.includes("fewer hooks")
    );
    expect(fatalErrors).toHaveLength(0);
  });

  test("multi-select delete: select 2 clips → delete both → no #300", async ({ page }) => {
    attachSentinelMonitoring(page);
    await startApp(page);

    const c1 = await addMidiClip(page, {
      trackIndex: 0,
      start: 0,
      duration: 2,
      name: "Multi1",
    });
    const c2 = await addMidiClip(page, {
      trackIndex: 0,
      start: 6,
      duration: 2,
      name: "Multi2",
    });

    // Select first (opens editor), then ctrl-select second
    await clipLocator(page, c1).click();
    await expect(page.locator(".clip-editor-container")).toBeVisible({ timeout: 5000 });
    await clipLocator(page, c2).click({ modifiers: ["Control"] });

    // Delete both at once
    await page.keyboard.press("Delete");

    await expect(clipLocator(page, c1)).toHaveCount(0, { timeout: 5000 });
    await expect(clipLocator(page, c2)).toHaveCount(0, { timeout: 5000 });
    await page.waitForTimeout(500);

    const hookErrors = sentinelLogs.filter((l) => l.includes("#300"));
    expect(hookErrors).toHaveLength(0);
  });

  test("context menu delete: right-click → Delete from menu → no #300", async ({ page }) => {
    attachSentinelMonitoring(page);
    await startApp(page);

    const clipId = await addMidiClip(page, {
      trackIndex: 0,
      start: 0,
      duration: 4,
      name: "CtxDel",
    });

    // Select the clip to open editor
    await clipLocator(page, clipId).click();
    await expect(page.locator(".clip-editor-container")).toBeVisible({ timeout: 5000 });

    // Right-click to open context menu
    await clipLocator(page, clipId).click({ button: "right" });
    await page.waitForTimeout(200);

    // Click "Delete" in the context menu
    const deleteBtn = page.locator(".clip-context-menu button", { hasText: "Delete" });
    if (await deleteBtn.count() > 0) {
      await deleteBtn.click();
    } else {
      // Fallback: keyboard delete
      await page.keyboard.press("Delete");
    }

    await expect(clipLocator(page, clipId)).toHaveCount(0, { timeout: 5000 });
    await page.waitForTimeout(500);

    const hookErrors = sentinelLogs.filter((l) => l.includes("#300"));
    expect(hookErrors).toHaveLength(0);
  });

  test("undo-delete cycle: delete clip → undo → no #300", async ({ page }) => {
    attachSentinelMonitoring(page);
    await startApp(page);

    const clipId = await addMidiClip(page, {
      trackIndex: 0,
      start: 0,
      duration: 4,
      name: "UndoDel",
    });

    // Select → editor opens
    await clipLocator(page, clipId).click();
    await expect(page.locator(".clip-editor-container")).toBeVisible({ timeout: 5000 });

    // Delete
    await page.keyboard.press("Delete");
    await expect(clipLocator(page, clipId)).toHaveCount(0, { timeout: 5000 });

    // Undo — the clip reappears
    await page.keyboard.press("Control+z");
    await expect(clipLocator(page, clipId)).toBeVisible({ timeout: 5000 });

    // Re-select the restored clip (undo doesn't restore selection)
    await clipLocator(page, clipId).click();
    await expect(page.locator(".clip-editor-container")).toBeVisible({ timeout: 5000 });

    // Delete again
    await page.keyboard.press("Delete");
    await expect(clipLocator(page, clipId)).toHaveCount(0, { timeout: 5000 });

    await page.waitForTimeout(500);

    const hookErrors = sentinelLogs.filter((l) => l.includes("#300"));
    expect(hookErrors).toHaveLength(0);
  });
});
