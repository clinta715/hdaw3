import { test, expect } from "@playwright/test";
import { startApp, rpcCall } from "./helpers";

// Song plan + cells over the live engine (Phase D journey): a pinned plan
// syncs arranger regions, the Song Plan panel reads it back, and cells fill
// window-exact clips whose clip is REUSED on reroll (no duplicate clips).
test.describe("Song plan and cells (user journeys)", () => {
  test.beforeEach(async ({ page }) => {
    await startApp(page);
  });

  test("panel shows an applied plan and its cells", async ({ page }) => {
    await rpcCall(page, "composition.setSongPlan", {
      bpm: 140, keyRoot: 5, scaleMode: 7, style: "full-on", seed: 7, totalBars: 24,
      sections: [
        { name: "intro", kind: "intro", bars: 8 },
        { name: "main", kind: "mainA", bars: 16 },
      ],
    });
    await rpcCall(page, "composition.setCellRecipe", {
      section: "main", role: "bass", trackId: 1, source: "rhythm",
      params: { pulseA: 4, pulseB: 3 }, seed: 11, locked: false,
    });

    await page.locator("header.transport-bar [title^='Compose']").click();
    await page.locator(".pgd-mode-select").selectOption("6");
    const panel = page.getByTestId("song-plan-panel");
    await expect(panel).toBeVisible();
    await expect(page.getByTestId("section-row-0")).toContainText("intro");
    await expect(page.getByTestId("section-row-1")).toContainText("main");
    // Cells table appears once there is at least one cell.
    await expect(page.getByTestId("cell-row-0")).toContainText("bass");
  });

  test("fill cells writes window clips and reroll reuses the same clip", async ({ page }) => {
    await rpcCall(page, "composition.setSongPlan", {
      bpm: 140, keyRoot: 5, scaleMode: 7, style: "full-on", seed: 7, totalBars: 16,
      sections: [
        { name: "intro", kind: "intro", bars: 8 },
        { name: "main", kind: "mainA", bars: 8 },
      ],
    });
    await rpcCall(page, "composition.setCellRecipe", {
      section: "main", role: "bass", trackId: 1, source: "rhythm",
      params: { pulseA: 4, pulseB: 3 }, seed: 11, locked: false,
    });

    await page.locator("header.transport-bar [title^='Compose']").click();
    await page.locator(".pgd-mode-select").selectOption("6");
    await expect(page.getByTestId("song-plan-panel")).toBeVisible();

    const clipsBefore = await page.locator(".tl-clip").count();
    await page.getByTestId("fill-cells").click();
    await expect(async () => {
      expect(await page.locator(".tl-clip").count()).toBe(clipsBefore + 1);
    }).toPass({ timeout: 10000 });

    // Reroll reuses the clip (count stays +1, no duplicate clip).
    await page.getByTitle("Reroll cell").first().click();
    await expect(async () => {
      expect(await page.locator(".tl-clip").count()).toBe(clipsBefore + 1);
    }).toPass({ timeout: 10000 });
    await expect(page.getByTestId("plan-msg")).toContainText("Rerolled");
  });

  test("energy arc renders bars from a real render + analysis", async ({ page }) => {
    await rpcCall(page, "composition.setSongPlan", {
      bpm: 140, keyRoot: 5, scaleMode: 7, style: "full-on", seed: 3, totalBars: 8,
      sections: [
        { name: "a", kind: "intro", bars: 4 },
        { name: "b", kind: "mainA", bars: 4 },
      ],
    });
    await rpcCall(page, "composition.setCellRecipe", {
      section: "b", role: "kick", trackId: 1, source: "rhythm",
      params: { pulseA: 8, pulseB: 0 }, seed: 5, locked: false,
    });
    await rpcCall(page, "composition.fillCells", { mode: "all" });

    await page.locator("header.transport-bar [title^='Compose']").click();
    await page.locator(".pgd-mode-select").selectOption("6");
    await expect(page.getByTestId("song-plan-panel")).toBeVisible();

    await page.getByTestId("check-energy").click();
    // Full offline render + FFT analysis on the headless engine: allow generous time.
    await expect(page.getByTestId("energy-row-0")).toBeVisible({ timeout: 90000 });
    await expect(page.getByTestId("energy-row-1")).toBeVisible();
    await expect(page.getByTestId("energy-arc")).toBeVisible();
  });
});
