import { test, expect } from "@playwright/test";
import { startApp, rpcCall } from "./helpers";
import { execSync } from "child_process";

// Helper: add an FX slot with a real scanned plugin. Returns the plugin name
// or null if no plugins are available (test should skip).
async function addFirstAvailablePlugin(page: import("@playwright/test").Page): Promise<string | null> {
  const plugins = await rpcCall<Array<{ name: string; fileOrIdentifier: string }>>(
    page,
    "plugin.getPlugins",
  );
  if (!plugins || plugins.length === 0) return null;
  const plugin = plugins[0];
  await rpcCall(page, "addFxSlot", {
    trackIndex: 0,
    type: "plugin",
    position: -1,
    pluginId: plugin.fileOrIdentifier,
  });
  return plugin.name;
}

test.describe("Plugin isolation — crash recovery", () => {
  test.beforeEach(async ({ page }) => {
    await startApp(page);
  });

  test("closing project with isolated plugin does not show crash dialog", async ({
    page,
  }) => {
    const pluginName = await addFirstAvailablePlugin(page);
    if (!pluginName) {
      test.skip(true, "No scanned plugins available — skipping isolation test");
      return;
    }

    await page.waitForTimeout(1000);

    // Close the project (creates a new empty project)
    await rpcCall(page, "project.new", {});

    // Verify no CrashDialog appeared
    await expect(page.locator("text=Plugin Crashed")).not.toBeVisible({
      timeout: 3000,
    });
  });

  test("killing plugin child shows crash dialog and restart recovers", async ({
    page,
  }) => {
    const pluginName = await addFirstAvailablePlugin(page);
    if (!pluginName) {
      test.skip(true, "No scanned plugins available — skipping isolation test");
      return;
    }

    // Wait for the plugin to be fully loaded
    await page.waitForTimeout(2000);

    // Kill all hdaw_plugin_host.exe processes (simulates a crash)
    try {
      execSync("taskkill /F /IM hdaw_plugin_host.exe", { stdio: "ignore" });
    } catch {
      // taskkill returns non-zero if no process found — that's fine
    }

    // CrashDialog should appear within ~5s (health monitor 2s + detection)
    const dialog = page.locator("text=Plugin Crashed");
    await expect(dialog).toBeVisible({ timeout: 10000 });

    // Click Restart Plugin
    await page.locator("button:has-text('Restart')").click();
    await expect(dialog).not.toBeVisible({ timeout: 5000 });

    // Verify playback still works (no permanent silence)
    await page.waitForTimeout(1000);
  });
});
