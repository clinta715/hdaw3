import { test, expect, Page } from "@playwright/test";
import { startApp, rpcCall, addMidiClip, clipLocator } from "./helpers";

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

// Create a note through the piano-roll UI (grid double-click), mirroring the
// green piano-roll.spec idiom. The UI path is optimistic + reconciled: it
// renders the note immediately, calls project.addNote, then re-fetches via
// syncNotes — so the DOM's data-note-id converges on the real engine note id.
// (The previous approach injected notes via a window.__projectStore dev hook
// that no longer exists; the injection silently no-opped and .ng-note never
// rendered. An RPC-only addNote can't just be awaited either: notesByClip is
// fetched lazily per clip — the timeline MIDI thumbnail caches it as [] the
// moment the clip renders — and a fullSync does not invalidate that cache, so
// the piano roll would never re-fetch the engine-added note.)
async function createNoteViaPianoRoll(page: Page, clipId: number): Promise<number> {
  // Selecting the clip auto-switches the bottom panel to the piano roll;
  // click the tab explicitly as well so the test doesn't depend on that.
  await clipLocator(page, clipId).click();
  await page.locator(".bt-tab", { hasText: "Piano Roll" }).click();
  const grid = page.locator(".note-grid");
  await expect(grid).toBeVisible({ timeout: 5000 });
  await grid.dblclick({ position: { x: 60, y: 50 } });
  await expect(page.locator(".ng-note").first()).toBeVisible({ timeout: 5000 });

  // The addNote RPC commits the note engine-side; poll until it's readable and
  // capture its real noteId.
  let noteId = -1;
  await expect(async () => {
    const notes = await rpcCall<NoteSnap[]>(page, "read.getNotes", { clipId });
    expect(notes.length).toBe(1);
    noteId = notes[0].noteId;
  }).toPass({ timeout: 5000 });

  // Wait for the store/DOM to reconcile onto the engine id (syncNotes).
  await expect(page.locator(`.ng-note[data-note-id="${noteId}"]`)).toBeVisible({ timeout: 10000 });
  return noteId;
}

test.describe("Note Operators pane", () => {
  test.beforeEach(async ({ page }) => {
    await startApp(page);
  });

  test("NoteOperatorsPane appears when a note is selected", async ({ page }) => {
    const clipId = await addMidiClip(page, { name: "OpsTest" });
    const noteId = await createNoteViaPianoRoll(page, clipId);

    // Click the note element to select it
    const noteEl = page.locator(`.ng-note[data-note-id="${noteId}"]`);
    await noteEl.click();

    // The NoteOperatorsPane should appear
    await expect(page.locator(".nop-pane")).toBeVisible({ timeout: 5000 });
    await expect(page.locator(".nop-header-title")).toHaveText("Note Operators");
    await expect(page.locator(".nop-section-title").first()).toHaveText("Operators");
  });

  test("NoteOperatorsPane shows all operator and expression fields", async ({ page }) => {
    const clipId = await addMidiClip(page, { name: "FieldsTest" });
    await createNoteViaPianoRoll(page, clipId);

    const noteEl = page.locator(".ng-note").first();
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
    const noteId = await createNoteViaPianoRoll(page, clipId);

    const noteEl = page.locator(`.ng-note[data-note-id="${noteId}"]`);
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
    const noteId = await createNoteViaPianoRoll(page, clipId);

    const noteEl = page.locator(`.ng-note[data-note-id="${noteId}"]`);
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
    await createNoteViaPianoRoll(page, clipId);

    const noteEl = page.locator(".ng-note").first();
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
    await createNoteViaPianoRoll(page, clipId);

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
