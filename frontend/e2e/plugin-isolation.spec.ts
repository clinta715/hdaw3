import { test, expect, Page } from "@playwright/test";
import { startApp, rpcCall } from "./helpers";
import { execSync } from "child_process";

type PluginInfo = { name: string; fileOrIdentifier: string };

// The startup plugin scan runs on a background thread and populates the plugin
// list asynchronously (the frontend re-fetches on notify.scanProgress). Poll
// until at least one scanned plugin is available instead of reading once.
async function waitForPlugins(page: Page, timeoutMs = 30_000): Promise<PluginInfo[]> {
  const start = Date.now();
  while (Date.now() - start < timeoutMs) {
    const plugins = await rpcCall<PluginInfo[]>(page, "plugin.getPlugins");
    if (Array.isArray(plugins) && plugins.length > 0) return plugins;
    await page.waitForTimeout(500);
  }
  return [];
}

// Add an FX slot with a real scanned plugin on track 0. Returns the plugin name,
// or null if no scanned plugins are available (test should skip). Only an
// *external* plugin runs in an isolated hdaw_plugin_host.exe child — internal
// FX run in-process and never exercise the crash/recovery path.
async function addFirstAvailablePlugin(page: Page): Promise<string | null> {
  const plugins = await waitForPlugins(page);
  if (plugins.length === 0) return null;
  const plugin = plugins[0];
  await rpcCall(page, "project.addFxSlot", {
    trackIndex: 0,
    type: "plugin",
    position: -1,
    pluginId: plugin.fileOrIdentifier,
  });
  return plugin.name;
}

function pluginHostRunning(): boolean {
  try {
    const out = execSync('tasklist /FI "IMAGENAME eq hdaw_plugin_host.exe"', {
      encoding: "utf8",
    });
    return out.includes("hdaw_plugin_host.exe");
  } catch {
    return false;
  }
}

// Poll until the isolated plugin host child has spawned (or timeout).
async function waitForPluginHost(page: Page, timeoutMs = 20_000): Promise<boolean> {
  const start = Date.now();
  while (Date.now() - start < timeoutMs) {
    if (pluginHostRunning()) return true;
    await page.waitForTimeout(250);
  }
  return false;
}

function killPluginHosts(): void {
  try {
    execSync("taskkill /F /IM hdaw_plugin_host.exe", { stdio: "ignore" });
  } catch {
    // taskkill returns non-zero if no matching process — that's fine.
  }
}

// Select track 0 and open the FX Chain tab. The crash banner is rendered only
// inside the FXChain component, so it is only visible while this tab is active
// and a track is selected (selectedTrackIndex defaults to null).
async function openFxChainForTrack0(page: Page): Promise<void> {
  // Click a track type badge to set selectedTrackIndex (no stopPropagation).
  await page.locator(".th-row .th-type-badge").first().click();
  await page.locator(".bt-tab", { hasText: "FX Chain" }).click();
}

test.describe("Plugin isolation — crash recovery", () => {
  // Cold plugin scans can take a while; give each test a generous ceiling.
  test.describe.configure({ timeout: 180_000 });

  test.beforeEach(async ({ page }) => {
    await startApp(page);
  });

  test("killing plugin child shows crash banner, then auto-respawn recovers", async ({
    page,
  }) => {
    const pluginName = await addFirstAvailablePlugin(page);
    if (!pluginName) {
      test.skip(true, "No scanned plugins available — skipping isolation test");
      return;
    }

    // Wait for the isolated host child to actually spawn before killing it,
    // otherwise taskkill has nothing to kill and no crash is detected.
    const hostUp = await waitForPluginHost(page);
    if (!hostUp) {
      test.skip(true, "Isolated plugin host did not spawn — skipping");
      return;
    }

    // Open the FX Chain for track 0 so the (per-slot) crash banner can render.
    await openFxChainForTrack0(page);
    await expect(page.locator(".fx-slot").first()).toBeVisible({ timeout: 10_000 });

    // Kill the plugin host child (simulates a plugin crash).
    killPluginHosts();

    // The crash banner appears once the health monitor detects the dead child
    // (~2s) and stays up while CrashRecoveryManager respawns the slot (500ms
    // grace + plugin reload in a fresh host).
    const banner = page.locator(".fx-slot-crash").first();
    await expect(banner).toBeVisible({ timeout: 15_000 });
    await expect(banner.locator(".fx-slot-crash-msg")).toContainText("Plugin crashed");

    // Auto-recovery: a fresh host child respawns...
    await expect
      .poll(() => pluginHostRunning(), { timeout: 20_000 })
      .toBe(true);
    // ...and notify.pluginRecovered clears the banner.
    await expect(page.locator(".fx-slot-crash")).not.toBeVisible({ timeout: 15_000 });
  });

  test("crash state does not leak into a new project", async ({ page }) => {
    const pluginName = await addFirstAvailablePlugin(page);
    if (!pluginName) {
      test.skip(true, "No scanned plugins available — skipping isolation test");
      return;
    }

    const hostUp = await waitForPluginHost(page);
    if (!hostUp) {
      test.skip(true, "Isolated plugin host did not spawn — skipping");
      return;
    }

    // Crash the plugin so the slot is marked crashed in the current project.
    killPluginHosts();

    // Replace the project with a fresh empty one.
    await rpcCall(page, "project.newProject", {});

    // The new project's track 0 has no FX slots, so no crash banner shows even
    // though the previous project had a crashed slot.
    await openFxChainForTrack0(page);
    await expect(page.locator(".fx-slot-crash")).toHaveCount(0, { timeout: 5_000 });
    await expect(page.locator(".fx-slot")).toHaveCount(0, { timeout: 5_000 });
  });
});
