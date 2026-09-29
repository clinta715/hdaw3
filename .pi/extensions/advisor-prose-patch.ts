/**
 * advisor-prose-patch: keep the pi-fabric advisor directive-prose coercion
 * applied across pi-fabric upgrades.
 *
 * scripts/patch_pi_fabric_advisor.py (v3) makes directive-mode actors whose
 * model answers in prose (or empty output) produce a valid
 * {"action":"message"|"silent"} value instead of failing the run with
 * "Structured agent output was invalid". Its v1/v2 predecessors anchored on a
 * hardcoded chunk hash + exact substrings, so every pi-fabric upgrade silently
 * disabled them. v3 locates the validator dynamically, and THIS extension
 * closes the loop: pi loads project-local .pi/extensions at startup, so every
 * session start re-runs the script (idempotent, ~ms) before any advisor run.
 *
 * Never breaks pi startup: all failures are logged, never thrown.
 */
import type { ExtensionAPI } from "@earendil-works/pi-coding-agent";
import { execFile } from "node:child_process";
import * as fs from "node:fs";
import * as path from "node:path";

export default function (pi: ExtensionAPI): void {
  pi.on("session_start", async () => {
    await reapply();
  });
}

async function reapply(): Promise<void> {
  try {
    const root = process.cwd();
    const script = path.join(root, "scripts", "patch_pi_fabric_advisor.py");
    if (!fs.existsSync(script)) {
      console.error("[advisor-prose-patch] script not found: " + script);
      return;
    }
    await new Promise<void>((resolve) => {
      execFile("python", [script], { cwd: root, timeout: 30000, windowsHide: true }, (err, stdout) => {
        const out = String(stdout || "").trim();
        if (err) {
          console.error("[advisor-prose-patch] patch FAILED (advisor prose answers will error until fixed): " + (err.message || err));
        } else if (out) {
          console.log("[advisor-prose-patch] " + out.split(/\r?\n/).join(" | "));
        }
        resolve();
      });
    });
  } catch (e) {
    console.error("[advisor-prose-patch] unexpected error:", e);
  }
}
