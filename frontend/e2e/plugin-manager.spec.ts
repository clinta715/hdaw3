import { test, expect } from "@playwright/test";
import { startApp } from "./helpers";

// PluginManagerDialog opens from the TransportBar 🎛️ button. It lists the
// plugins known to the engine, with filter tabs (All/Instruments/Effects/
// Blacklisted), a text filter, a "Show blacklisted" toggle, and a Rescan
// button. Per-plugin "Blacklist"/"Unblacklist" is the enable/disable control.
//
// The installed-plugin set depends on the host machine, so tests that need a
// plugin guard themselves with test.skip when the list is empty.
test.describe("Plugin Manager dialog (user journeys)", () => {
  test.beforeEach(async ({ page }) => {
    await startApp(page);
  });

  async function openDialog(page: import("@playwright/test").Page) {
    await page.locator("header.transport-bar [title='Plugin Manager']").click();
    await expect(page.locator(".plugin-manager")).toBeVisible({ timeout: 5000 });
    // Let the async getPlugins + per-plugin isBlacklisted fetches settle.
    await page.waitForTimeout(750);
  }

  test("opens from the transport bar button", async ({ page }) => {
    await openDialog(page);
    await expect(page.locator(".pm-header h2")).toHaveText("Plugin Manager");
  });

  test("header shows the plugin count", async ({ page }) => {
    await openDialog(page);
    await expect(page.locator(".pm-count")).toContainText(/plugins/);
  });

  test("four filter tabs are present", async ({ page }) => {
    await openDialog(page);
    await expect(page.locator(".pm-tab", { hasText: "All" })).toBeVisible();
    await expect(page.locator(".pm-tab", { hasText: "Instruments" })).toBeVisible();
    await expect(page.locator(".pm-tab", { hasText: "Effects" })).toBeVisible();
    await expect(page.locator(".pm-tab", { hasText: "Blacklisted" })).toBeVisible();
  });

  test("the All tab is active by default", async ({ page }) => {
    await openDialog(page);
    await expect(page.locator(".pm-tab", { hasText: "All" })).toHaveClass(/active/);
  });

  test("toolbar has filter input, show-blacklisted toggle and Rescan button", async ({ page }) => {
    await openDialog(page);
    await expect(page.locator(".pm-filter")).toBeVisible();
    await expect(page.locator(".pm-show-bl input[type='checkbox']")).toBeVisible();
    await expect(page.locator(".pm-scan-btn")).toBeVisible();
  });

  test("close (×) button closes the dialog", async ({ page }) => {
    await openDialog(page);
    await page.locator(".pm-close").click();
    await expect(page.locator(".plugin-manager")).toBeHidden({ timeout: 5000 });
  });

  test("switching tabs moves the active class", async ({ page }) => {
    await openDialog(page);
    const instrumentsTab = page.locator(".pm-tab", { hasText: "Instruments" });
    await instrumentsTab.click();
    await expect(instrumentsTab).toHaveClass(/active/);
    await expect(page.locator(".pm-tab", { hasText: "All" })).not.toHaveClass(/active/);
    // Switch back.
    await page.locator(".pm-tab", { hasText: "All" }).click();
    await expect(page.locator(".pm-tab", { hasText: "All" })).toHaveClass(/active/);
  });

  test("show-blacklisted checkbox toggles its checked state", async ({ page }) => {
    await openDialog(page);
    const cb = page.locator(".pm-show-bl input[type='checkbox']");
    await expect(cb).toBeChecked(); // default: show blacklisted
    await cb.uncheck();
    await expect(cb).not.toBeChecked();
    await cb.check();
    await expect(cb).toBeChecked();
  });

  test("list renders plugin items or an empty state", async ({ page }) => {
    await openDialog(page);
    const itemCount = await page.locator(".pm-item").count();
    const emptyVisible = await page.locator(".pm-empty").isVisible();
    expect(itemCount > 0 || emptyVisible).toBeTruthy();
  });

  test("text filter narrows the list to a no-match state", async ({ page }) => {
    await openDialog(page);
    const count = await page.locator(".pm-item").count();
    test.skip(count === 0, "requires at least one installed plugin to filter");
    await page.locator(".pm-filter").fill("zzz_no_such_plugin_zzz");
    await expect(page.locator(".pm-empty")).toContainText(/No plugins match filter/i, { timeout: 5000 });
    // Clearing the filter restores the full list.
    await page.locator(".pm-filter").fill("");
    await expect(page.locator(".pm-item")).toHaveCount(count, { timeout: 5000 });
  });

  test("blacklist toggle flips a plugin between Blacklist and Unblacklist", async ({ page }) => {
    await openDialog(page);
    const items = page.locator(".pm-item");
    const count = await items.count();
    // Find the first non-blacklisted plugin (button reads "Blacklist").
    let targetIdx = -1;
    for (let i = 0; i < count; i++) {
      const t = await items.nth(i).locator(".pm-bl-btn").textContent();
      if (t?.trim() === "Blacklist") { targetIdx = i; break; }
    }
    test.skip(targetIdx < 0, "requires a non-blacklisted plugin to toggle");

    // Blacklist it.
    await items.nth(targetIdx).locator(".pm-bl-btn").click();
    await expect(items.nth(targetIdx).locator(".pm-bl-btn")).toHaveText("Unblacklist", { timeout: 5000 });
    await expect(items.nth(targetIdx)).toHaveClass(/blacklisted/, { timeout: 5000 });

    // Unblacklist it (restores original state).
    await items.nth(targetIdx).locator(".pm-bl-btn").click();
    await expect(items.nth(targetIdx).locator(".pm-bl-btn")).toHaveText("Blacklist", { timeout: 5000 });
    await expect(items.nth(targetIdx)).not.toHaveClass(/blacklisted/, { timeout: 5000 });
  });

  test("rescan runs and the button returns to Rescan when done", async ({ page }) => {
    await openDialog(page);
    const scanBtn = page.locator(".pm-scan-btn");
    await expect(scanBtn).toBeEnabled();
    await scanBtn.click();
    // The scan runs on a background thread and always broadcasts a done
    // notification; the button re-enables and reverts to "Rescan".
    await expect(scanBtn).toBeEnabled({ timeout: 60000 });
    await expect(scanBtn).toHaveText("Rescan", { timeout: 5000 });
  });
});
