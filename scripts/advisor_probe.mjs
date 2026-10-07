#!/usr/bin/env node
// Functional probe for the pi-fabric advisor directive-prose coercion patch
// (scripts/patch_pi_fabric_advisor.py v3/v4). Locates the validator chunk
// DYNAMICALLY (chunk hashes change every pi-fabric upgrade), then asserts the
// four behaviours: prose -> message, empty -> silent, non-directive -> still
// fails loudly, valid JSON -> passes through untouched. Exits non-zero on any
// mismatch. Run: node scripts/advisor_probe.mjs
import fs from "node:fs";
import path from "node:path";
import os from "node:os";

const DIST = path.join(os.homedir(), ".pi", "agent", "npm", "node_modules", "pi-fabric", "dist");
const INVARIANT = "Structured agent output was invalid";

function findValidatorFile() {
  const candidates = [];
  for (const entry of ["chunks", "."]) {
    const dir = path.join(DIST, entry);
    if (!fs.existsSync(dir)) continue;
    for (const f of fs.readdirSync(dir)) {
      if (f.endsWith(".js")) candidates.push(path.join(dir, f));
    }
  }
  for (const f of candidates) {
    try {
      if (fs.readFileSync(f, "utf8").includes(INVARIANT) && !f.endsWith(".map")) return f;
    } catch { /* skip unreadable */ }
  }
  return null;
}

const file = findValidatorFile();
if (!file) { console.error("PROBE FAIL: no validator file found under " + DIST); process.exit(2); }
const { validateAgentResult } = await import("file://" + file);

const directive = { type: "object", properties: { action: { type: "string", enum: ["silent", "message"] }, message: { type: "string" } } };
const fails = [];
const check = (name, cond, detail) => { if (cond) console.log("PASS", name, detail); else { fails.push(name); console.log("FAIL", name, detail); } };

const r1 = validateAgentResult({ status: "completed", text: "Gate 2 wording: engine+common merge is fine." }, directive);
check("prose->message", r1.status === "completed" && r1.value && r1.value.action === "message" && /Gate 2 wording/.test(r1.value.message), JSON.stringify(r1.value));
const r2 = validateAgentResult({ status: "completed", text: "   " }, directive);
check("empty->silent", r2.status === "completed" && r2.value && r2.value.action === "silent", JSON.stringify(r2.value));
const r3 = validateAgentResult({ status: "completed", text: "prose" }, { type: "object", properties: { name: { type: "string" } } });
check("nondirective->still-fails", r3.status === "failed" && /Structured agent output was invalid/.test(r3.error || ""), (r3.error || "").slice(0, 60));
const r4 = validateAgentResult({ status: "completed", text: '{"action":"silent"}' }, directive);
check("json->untouched", r4.status === "completed" && r4.value && r4.value.action === "silent", JSON.stringify(r4.value));

if (fails.length) { console.error("PROBE FAIL: " + fails.join(", ") + " (validator: " + file + ")"); process.exit(1); }
console.log("PROBE PASS (validator: " + file + ")");
