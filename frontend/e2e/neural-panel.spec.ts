import { test, expect } from "@playwright/test";
import { startApp } from "./helpers";

// RAVE #4: the Neural bottom-panel tab renders in the stable bottom-panel
// frame (no dialogs, no floating windows). Requires an engine binary with
// the rave.* RPCs (Router_Rave.cpp) — the panel loads models via
// rave.listModels on mount and shows a friendly empty state when none exist.

test.describe("Neural panel", () => {
  test("Neural bottom tab opens the panel with model list or empty state", async ({ page }) => {
    await startApp(page);

    // The Neural tab is a button in the bottom tab bar.
    const tab = page.locator(".bt-tab", { hasText: "Neural" });
    await expect(tab).toBeVisible();
    await tab.click();

    // Panel content renders inside the bottom panel — heading always present,
    // and either the model select (models found) or the friendly empty state.
    const panel = page.locator(".neural-panel");
    await expect(panel).toBeVisible();
    await expect(panel.locator(".neural-panel__title").first()).toContainText("RAVE Neural Render");

    const select = panel.locator(".neural-panel__select");
    const empty = panel.locator(".neural-panel__empty");
    await expect(async () => {
      const hasSelect = await select.isVisible().catch(() => false);
      const hasEmpty = await empty.isVisible().catch(() => false);
      expect(hasSelect || hasEmpty).toBeTruthy();
    }).toPass({ timeout: 10000 });

    // Core controls are docked in the tab (never a dialog).
    await expect(panel.getByRole("button", { name: /Start Transform|Rendering/ })).toBeVisible();
  });

  test("rave.listModels RPC is reachable through the app connection", async ({ page }) => {
    await startApp(page);
    // Window rpc seam (helpers.rpcCall): the engine must answer rave.listModels
    // with a models array (possibly empty) — proves the engine binary includes
    // the rave RPC surface the panel depends on.
    const result = await page.evaluate(() =>
      (window as any).rpc.call("rave.listModels", {}),
    );
    expect(result).toBeTruthy();
    expect(Array.isArray((result as { models?: unknown[] }).models)).toBeTruthy();
  });
});
