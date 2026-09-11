/**
 * graphify-watch: auto-start the graphify --watch background process with pi.
 *
 * A project-local pi extension. pi loads extensions at startup; this one
 * subscribes to the "session_start" lifecycle event (the doc-recommended place
 * to start deferred background resources) and spawns the graphify watcher for
 * THIS project if one isn't already running.
 *
 * Idempotent: every pi session start re-checks for a live graphify.watch
 * process before spawning, so resuming/reloading never stacks a second watcher
 * (two watchers would race over graphify-out/graph.json).
 *
 * It only runs when pi starts in this project (project-local .pi/extensions
 * load for the trusted project), and never breaks pi startup on error.
 */
import type { ExtensionAPI } from "@earendil-works/pi-coding-agent";
import { spawn, execFile } from "node:child_process";
import * as fs from "node:fs";
import * as path from "node:path";

const PROJECT = "/mnt/d/pdf/roo projects/hdaw3";
const GRAPHIFY = process.env.GRAPHIFY_BIN ?? "/home/hapbt/.local/bin/graphify";

export default function (pi: ExtensionAPI): void {
  pi.on("session_start", async () => {
    await ensureGraphifyWatch();
  });
}

async function ensureGraphifyWatch(): Promise<void> {
  try {
    if (await isWatchRunning()) {
      console.log("[graphify-watch] already running - skipping spawn");
      return;
    }
    const outDir = path.join(PROJECT, "graphify-out");
    fs.mkdirSync(outDir, { recursive: true });
    const outFd = fs.openSync(path.join(outDir, "watch.log"), "a");
    const errFd = fs.openSync(path.join(outDir, "watch.err.log"), "a");
    const child = spawn(
      GRAPHIFY,
      ["watch", ".", "--debounce", "3"],
      {
        cwd: PROJECT,
        detached: true,
        windowsHide: true,
        stdio: ["ignore", outFd, errFd],
      },
    );
    child.on("error", (e) => console.error("[graphify-watch] spawn error:", e));
    child.unref();
    console.log("[graphify-watch] spawned watcher (pid " + (child.pid ?? "?") + ")");
  } catch (e) {
    // Never let this extension break pi startup.
    console.error("[graphify-watch] failed to start watcher:", e);
  }
}

/** True if any graphify.watch process is already alive. */
function isWatchRunning(): Promise<boolean> {
  return new Promise((resolve) => {
    execFile(
      "pgrep",
      ["-f", "graphify(\\.watch| watch)"],
      { timeout: 10000, windowsHide: true },
      (err, stdout) => {
        if (!err) {
          resolve((stdout || "").trim().length > 0);
          return;
        }

        const code = typeof (err as { code?: unknown }).code === "number"
          ? (err as { code: number }).code
          : undefined;

        if (code === 1) {
          // pgrep exits 1 when no matching process exists.
          resolve(false);
          return;
        }

        // Unknown state -> refuse to spawn a second watcher (avoid racing
        // over graph.json), but say so.
        console.error("[graphify-watch] could not check for running watcher:", err);
        resolve(true);
      },
    );
  });
}
