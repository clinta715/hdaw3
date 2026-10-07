#!/usr/bin/env python3
# Patch pi-fabric's structured-output validator so a DIRECTIVE-mode actor (its
# schema has action.enum containing "silent") whose model answers in prose - or
# with empty output - produces a valid COMPLETED record instead of failing the
# run with "Structured agent output was invalid":
#     prose -> {"action": "message", "message": <prose, capped at 2400 chars>}
#     empty -> {"action": "silent"}
# Non-directive schemas still fail loudly (unchanged).
#
# History: v1 coerced prose to silent (swallowed genuine advice). v2/v2.1
# wrapped prose as a message capped at 900 chars - advice arrived cut
# mid-sentence - and empty output still failed. All of v1/v2 anchored on ONE
# hardcoded chunk hash + exact minified substrings, so every pi-fabric upgrade
# silently disabled the fix (it broke again on 2026-09-29, pi-fabric 0.101.1).
# A first v3 draft inserted its guard AFTER the catch's
# `record.status = "failed"` and without the directive-schema conditional, so
# every coerced record came back status=failed - caught by the functional probe
# (.tmp_build_scratch/advisor_probe.mjs), fixed by inserting the conditional
# guard BEFORE the failed assignment. Verify with that probe after any change.
#
# v3 (this file) is upgrade-proof by construction:
#   * it finds validator sites DYNAMICALLY: every dist/**/*.js containing the
#     invariant error string, catch block located by backward search, record and
#     schema identifier names taken from the actual code;
#   * two site shapes are handled:
#       - a validateAgentResult(record, schema)-style function: a conditional
#         guard BEFORE the failed assignment uses the signature's schema
#         parameter and returns the record completed;
#       - an inline worker catch whose try read the schema via
#         options.schemaFile (the const is out of scope in the catch): the
#         guard re-reads the schema file defensively and wraps the failure
#         branch in `if (!directiveSchema) { ... }` - no `return`, which would
#         skip writeRunRecord and strand the harness;
#   * a file whose shape it cannot understand is reported and exits NON-ZERO:
#     drift must be loud, never a silent regression.
#
# v4 (2026-10-07, pi-fabric 0.109.0): the worker-site schema read moved INSIDE
# the try and the fs import gained a bundler alias (fs2.readFileSync), so the
# v3 worker regex (hardcoded "fs.") refused and drift went loud again (the
# re-apply extension had also been retired to .disabled, so upgrades silently
# reverted the fix). v4 captures the fs alias from the actual read line and
# re-reads the schema defensively with it. Verification probe moved to
# scripts/advisor_probe.mjs (locates the validator chunk dynamically).
#
# Re-applied automatically at session start by .pi/extensions/advisor-prose-patch.ts.
# Idempotent.

import re
import shutil
import sys
from pathlib import Path

DIST = Path.home() / ".pi" / "agent" / "npm" / "node_modules" / "pi-fabric" / "dist"
INVARIANT = "Structured agent output was invalid"
MARKER_V3 = "advisor directive-prose coercion v3 (2026-09-29)"
MARKER = "advisor directive-prose coercion v4 (2026-10-07)"
MESSAGE_CAP = 2400

CATCH_RE = re.compile(r'(\} catch \((\w+)\) \{\n)(\s*)((\w+)\.status = "failed";)')


def directive_guard(record_id, schema_id, indent):
    i = indent
    return (
        f'{i}if ({schema_id} && {schema_id}.properties && {schema_id}.properties.action && Array.isArray({schema_id}.properties.action.enum) && {schema_id}.properties.action.enum.includes("silent")) {{\n'
        f'{i}  const prose = String({record_id}.text || "").trim();\n'
        f'{i}  {record_id}.value = prose ? {{ action: "message", message: prose.slice(0, {MESSAGE_CAP}) }} : {{ action: "silent" }}; // {MARKER}\n'
        f'{i}  return {record_id};\n'
        f'{i}}}\n'
    )


def enclosing_fn_signature(src, pos):
    """Nearest `function name(a, b)` before pos, only if no other function
    declaration sits between it and pos (i.e. pos is in its direct body)."""
    fn = None
    for fm in re.finditer(r'function \w+\((\w+), (\w+)\)', src[:pos]):
        fn = fm
    if fn is None or re.search(r'\bfunction \w+\(', src[fn.end():pos]):
        return None
    return fn


def patch_function_site(src, pos):
    opener = src.rfind("} catch (", 0, pos)
    if opener == -1:
        return None
    m = CATCH_RE.match(src[opener:opener + 300])
    if not m:
        return None
    fn = enclosing_fn_signature(src, opener)
    if fn is None:
        return None
    record_id, schema_id = m.group(5), fn.group(2)
    if m.group(5) != fn.group(1):
        return None
    indent = m.group(3)
    guard = directive_guard(record_id, schema_id, indent)
    replacement = m.group(1) + guard + m.group(3) + m.group(4)
    return src[:opener] + replacement + src[opener + len(m.group(0)):]


def patch_worker_site(src, pos):
    opener = src.rfind("} catch (", 0, pos)
    if opener == -1:
        return None
    m = CATCH_RE.match(src[opener:opener + 300])
    if not m:
        return None
    window = src[max(0, opener - 700):opener]
    # v4: the read may live inside the try and the fs import may be aliased by
    # the bundler (fs2, fs3, ...). Capture whichever identifier the file uses.
    mfs = re.search(r'const \w+ = JSON\.parse\((\w+)\.readFileSync\(options\.schemaFile, "utf8"\)\);', window)
    if not mfs:
        return None
    fs_alias = mfs.group(1)
    indent = m.group(3)
    i = indent
    ref = (
        f'{i}const schemaRef = (() => {{ try {{ return JSON.parse({fs_alias}.readFileSync(options.schemaFile, "utf8")); }} catch {{ return null; }} }})();\n'
        f'{i}const directiveSchema = schemaRef && schemaRef.properties && schemaRef.properties.action && Array.isArray(schemaRef.properties.action.enum) && schemaRef.properties.action.enum.includes("silent"); // {MARKER}\n'
        f'{i}if (!directiveSchema) {{\n'
    )
    head = m.group(1) + ref + m.group(3) + m.group(4)
    out = src[:opener] + head + src[opener + len(m.group(0)):]
    # Close the wrapper after the record.error assignment inside this catch.
    err = out.find("record.error = `" + INVARIANT, opener)
    if err == -1:
        return None
    first_tick = out.find("`", err + len("record.error = `"))
    semi = out.find(";", out.find("`", first_tick + 1))
    if semi == -1:
        return None
    return out[:semi + 1] + "\n" + i + "}" + out[semi + 1:]


def process(path: Path) -> bool:
    try:
        src = path.read_text(encoding="utf-8")
    except OSError as e:
        print(f"{path}: unreadable ({e})", file=sys.stderr)
        return False
    if MARKER in src or MARKER_V3 in src:
        print(path.name + ": already patched (v3/v4 marker present)")
        return True
    if INVARIANT not in src:
        return True  # not a validator file
    backup = path.with_suffix(path.suffix + ".advisor-orig")
    if not backup.exists():
        shutil.copyfile(path, backup)
    ok = True
    work = src
    for m in re.finditer(re.escape(INVARIANT), src):
        pos = m.start()
        patched = patch_function_site(work, pos)
        if patched is None:
            patched = patch_worker_site(work, pos)
        if patched is None:
            print(path.name + ": ERROR unsupported validator shape at offset " + str(pos) + " - NOT patched", file=sys.stderr)
            ok = False
        else:
            work = patched
    if not ok:
        return False
    path.write_text(work, encoding="utf-8", newline="")
    print(path.name + ": patched v4 (backup " + backup.name + ")")
    return True


def main() -> int:
    if not DIST.is_dir():
        print("pi-fabric dist not found (" + str(DIST) + ") - nothing to patch")
        return 0
    ok = True
    for path in sorted(DIST.rglob("*.js")):
        if not process(path):
            ok = False
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
