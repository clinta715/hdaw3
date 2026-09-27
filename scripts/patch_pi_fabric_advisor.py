#!/usr/bin/env python3
# Patch pi-fabric's directive validator (both dist copies) so a directive-mode
# actor whose model answers in PROSE is coerced to {"action":"silent"} instead
# of failing with "Structured agent output was invalid". The ambient advisor
# prefers silence; prose was never going to be displayed, so swallowing it
# matches the intended semantics. Other agents' schema failures still fail
# loudly. Re-apply after every pi-fabric upgrade. Idempotent.

import shutil
from pathlib import Path

DIST = Path.home() / ".pi" / "agent" / "npm" / "node_modules" / "pi-fabric" / "dist"
CHUNK = DIST / "chunks" / "chunk-3QECYQGA.js"
WORKER = DIST / "worker.js"
MARKER = "advisor directive-prose coercion (2026-09-27)"

CHUNK_OLD = '  } catch (error) {\n    record.status = "failed";'
CHUNK_NEW = (
    '  } catch (error) {\n'
    '    if (schema && schema.properties && schema.properties.action && Array.isArray(schema.properties.action.enum) && schema.properties.action.enum.includes("silent")) {\n'
    '      record.value = { action: "silent" }; // ' + MARKER + '\n'
    '      return record;\n'
    '    }\n'
    '    record.status = "failed";'
)

WORKER_OLD = '    } catch (error) {\n      record.status = "failed";'
WORKER_NEW = (
    '    } catch (error) {\n'
    '      const directive = schema2 && schema2.properties && schema2.properties.action && Array.isArray(schema2.properties.action.enum) && schema2.properties.action.enum.includes("silent");\n'
    '      if (directive) {\n'
    '        record.value = { action: "silent" }; // ' + MARKER + '\n'
    '      } else {\n'
    '        record.status = "failed";'
)
WORKER_TAIL_OLD = '      const reason = error instanceof Error ? error.message : String(error);'
WORKER_TAIL_NEW = '        const reason = error instanceof Error ? error.message : String(error);'
WORKER_CLOSE_OLD = '      record.error = `Structured agent output was invalid: ${reason}${snippet ? ` (output: ${snippet}${output.length > 200 ? "\\u2026" : ""})` : ""}`;\n    }'
WORKER_CLOSE_NEW = '        record.error = `Structured agent output was invalid: ${reason}${snippet ? ` (output: ${snippet}${output.length > 200 ? "\\u2026" : ""})` : ""}`;\n      }\n    }'

def patch(path, old, new, count_check=1, tail=None):
    src = path.read_text(encoding="utf-8")
    if MARKER in src:
        print(path.name + ": already patched")
        return
    backup = path.with_suffix(path.suffix + ".advisor-orig")
    shutil.copyfile(path, backup)
    assert src.count(old) == count_check, path.name + " anchor count " + str(src.count(old))
    src = src.replace(old, new)
    if tail is not None:
        assert src.count(tail[0]) == 1, path.name + " tail anchor missing"
        src = src.replace(tail[0], tail[1])
    path.write_text(src, encoding="utf-8", newline="")
    print(path.name + ": patched (backup " + backup.name + ")")

def main():
    patch(CHUNK, CHUNK_OLD, CHUNK_NEW)
    patch(WORKER, WORKER_OLD, WORKER_NEW, tail=(WORKER_CLOSE_OLD, WORKER_CLOSE_NEW))

main()
