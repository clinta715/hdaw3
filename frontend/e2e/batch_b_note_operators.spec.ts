import { test, expect } from "@playwright/test";
import { startApp, rpcCall, addMidiClip } from "./helpers";

type NoteSnap = {
  noteId: number;
  pitch: number;
  velocity: number;
  startBeat: number;
  durationBeats: number;
  chance: number;
  repeatCount: number;
  noteGain: number;
};

test.describe("Note Operators pane", () => {
  test.beforeEach(async ({ page }) => {
    await startApp(page);
  });

  test("NoteOperatorsPane appears when a note is selected", async ({ page }) => {
    const clipId = await addMidiClip(page, { name: "OpsTest" });

    const noteId = await rpcCall<number>(page, "project.addNote", {
      clipId,
      pitch: 60,
      startBeat: 0,
      durationBeats: 1,
      velocity: 100,
    });

    // Refresh notes in the store so NoteGrid renders the note
    await page.evaluate(
      ([cid]: [number]) => (window as any).rpc.call("read.getNotes", { clipId: cid }).then(
        (notes: unknown) => {
          const store = (window as any).__projectStore;
          if (store) {
            const m = new Map(store.getState().notesByClip);
            m.set(cid, notes);
            store.setState({ notesByClip: m });
          }
        },
      ),
      [clipId] as [number],
    );

    // Click the note element to select it
    const noteEl = page.locator(`.ng-note[data-note-id="${noteId}"]`);
    await expect(noteEl).toBeVisible({ timeout: 10000 });
    await noteEl.click();

    // The NoteOperatorsPane should appear
    await expect(page.locator(".nop-pane")).toBeVisible({ timeout: 5000 });
    await expect(page.locator(".nop-header-title")).toHaveText("Note Operators");
    await expect(page.locator(".nop-section-title").first()).toHaveText("Operators");
  });

  test("NoteOperatorsPane shows all operator and expression fields", async ({ page }) => {
    const clipId = await addMidiClip(page, { name: "FieldsTest" });

    await rpcCall<number>(page, "project.addNote", {
      clipId,
      pitch: 60,
      startBeat: 0,
      durationBeats: 1,
      velocity: 100,
    });

    await page.evaluate(
      ([cid]: [number]) => (window as any).rpc.call("read.getNotes", { clipId: cid }).then(
        (notes: unknown) => {
          const store = (window as any).__projectStore;
          if (store) {
            const m = new Map(store.getState().notesByClip);
            m.set(cid, notes);
            store.setState({ notesByClip: m });
          }
        },
      ),
      [clipId] as [number],
    );

    const noteEl = page.locator(".ng-note").first();
    await expect(noteEl).toBeVisible({ timeout: 10000 });
    await noteEl.click();

    await expect(page.locator(".nop-pane")).toBeVisible({ timeout: 5000 });

    // Check operator fields
    for (const label of ["Chance", "Repeat Cnt", "Repeat Rate", "Repeat Curve", "Occurrence", "Recurrence"]) {
      await expect(page.locator(".nop-label", { hasText: label })).toBeVisible();
    }

    // Check expression fields
    for (const label of ["Gain", "Pan", "Pitch", "Timbre", "Pressure"]) {
      await expect(page.locator(".nop-label", { hasText: label })).toBeVisible();
    }

    // Check clip seed
    await expect(page.locator(".nop-seed-label")).toHaveText("Clip Seed");
  });

  test("changing chance value calls setNoteChance RPC", async ({ page }) => {
    const clipId = await addMidiClip(page, { name: "ChanceTest" });

    const noteId = await rpcCall<number>(page, "project.addNote", {
      clipId,
      pitch: 60,
      startBeat: 0,
      durationBeats: 1,
      velocity: 100,
    });

    await page.evaluate(
      ([cid]: [number]) => (window as any).rpc.call("read.getNotes", { clipId: cid }).then(
        (notes: unknown) => {
          const store = (window as any).__projectStore;
          if (store) {
            const m = new Map(store.getState().notesByClip);
            m.set(cid, notes);
            store.setState({ notesByClip: m });
          }
        },
      ),
      [clipId] as [number],
    );

    const noteEl = page.locator(`.ng-note[data-note-id="${noteId}"]`);
    await expect(noteEl).toBeVisible({ timeout: 10000 });
    await noteEl.click();

    await expect(page.locator(".nop-pane")).toBeVisible({ timeout: 5000 });

    // Change the chance slider to 50%
    const chanceSlider = page.locator(".nop-slider").first();
    await chanceSlider.fill("0.5");

    // Verify the value was applied via RPC
    await expect(async () => {
      const notes = await rpcCall<NoteSnap[]>(page, "read.getNotes", { clipId });
      const note = notes.find((n) => n.noteId === noteId);
      expect(note?.chance).toBeCloseTo(0.5, 1);
    }).toPass({ timeout: 10000 });
  });

  test("changing gain value calls setNoteGain RPC", async ({ page }) => {
    const clipId = await addMidiClip(page, { name: "GainTest" });

    const noteId = await rpcCall<number>(page, "project.addNote", {
      clipId,
      pitch: 60,
      startBeat: 0,
      durationBeats: 1,
      velocity: 100,
    });

    await page.evaluate(
      ([cid]: [number]) => (window as any).rpc.call("read.getNotes", { clipId: cid }).then(
        (notes: unknown) => {
          const store = (window as any).__projectStore;
          if (store) {
            const m = new Map(store.getState().notesByClip);
            m.set(cid, notes);
            store.setState({ notesByClip: m });
          }
        },
      ),
      [clipId] as [number],
    );

    const noteEl = page.locator(`.ng-note[data-note-id="${noteId}"]`);
    await expect(noteEl).toBeVisible({ timeout: 10000 });
    await noteEl.click();

    await expect(page.locator(".nop-pane")).toBeVisible({ timeout: 5000 });

    // The gain slider is the 3rd slider (index 2: Chance=0, RepeatCurve=1, Gain=2)
    const gainSlider = page.locator(".nop-slider").nth(2);
    await gainSlider.fill("1.5");

    await expect(async () => {
      const notes = await rpcCall<NoteSnap[]>(page, "read.getNotes", { clipId });
      const note = notes.find((n) => n.noteId === noteId);
      expect(note?.noteGain).toBeCloseTo(1.5, 1);
    }).toPass({ timeout: 10000 });
  });

  test("collapse toggle persists to localStorage", async ({ page }) => {
    const clipId = await addMidiClip(page, { name: "CollapseTest" });

    await rpcCall<number>(page, "project.addNote", {
      clipId,
      pitch: 60,
      startBeat: 0,
      durationBeats: 1,
      velocity: 100,
    });

    await page.evaluate(
      ([cid]: [number]) => (window as any).rpc.call("read.getNotes", { clipId: cid }).then(
        (notes: unknown) => {
          const store = (window as any).__projectStore;
          if (store) {
            const m = new Map(store.getState().notesByClip);
            m.set(cid, notes);
            store.setState({ notesByClip: m });
          }
        },
      ),
      [clipId] as [number],
    );

    const noteEl = page.locator(".ng-note").first();
    await expect(noteEl).toBeVisible({ timeout: 10000 });
    await noteEl.click();

    await expect(page.locator(".nop-pane")).toBeVisible({ timeout: 5000 });

    // Click the header to collapse
    await page.locator(".nop-header").click();
    await expect(page.locator(".nop-body--collapsed")).toBeVisible();

    // Verify localStorage was set
    const collapsed = await page.evaluate(() => localStorage.getItem("noteOperatorsCollapsed"));
    expect(collapsed).toBe("true");

    // Click again to expand
    await page.locator(".nop-header").click();
    await expect(page.locator(".nop-body--collapsed")).not.toBeVisible();
  });

  test("pane hides when no notes selected", async ({ page }) => {
    const clipId = await addMidiClip(page, { name: "HideTest" });

    await rpcCall<number>(page, "project.addNote", {
      clipId,
      pitch: 60,
      startBeat: 0,
      durationBeats: 1,
      velocity: 100,
    });

    await page.evaluate(
      ([cid]: [number]) => (window as any).rpc.call("read.getNotes", { clipId: cid }).then(
        (notes: unknown) => {
          const store = (window as any).__projectStore;
          if (store) {
            const m = new Map(store.getState().notesByClip);
            m.set(cid, notes);
            store.setState({ notesByClip: m });
          }
        },
      ),
      [clipId] as [number],
    );

    const noteEl = page.locator(".ng-note").first();
    await expect(noteEl).toBeVisible({ timeout: 10000 });

    // Click the note to select it
    await noteEl.click();
    await expect(page.locator(".nop-pane")).toBeVisible({ timeout: 5000 });

    // Click on empty space in the grid to deselect
    const gridArea = page.locator(".note-grid");
    const box = await gridArea.boundingBox();
    if (box) {
      // Click far to the right of any note (empty area)
      await page.mouse.click(box.x + box.width - 20, box.y + box.height / 2);
    }

    // Pane should disappear
    await expect(page.locator(".nop-pane")).not.toBeVisible({ timeout: 5000 });
  });
});
