#!/usr/bin/env python3
"""MCP stdio client for the HDAW engine -- the transport mcp-launch.bat uses
(--mcp-stdio, newline-delimited JSON over the engine's stdin/stdout).

Usage:
  python scripts/mcp_call.py tools/list
  python scripts/mcp_call.py call <toolName> ['<json-args>']
  python scripts/mcp_call.py schemas [name1,name2,...]
  python scripts/mcp_call.py desc name1,name2,...
  python scripts/mcp_call.py run <steps.json>

steps.json = [{"tool": "name", "args": {...}, "timeout": 600}, ...]
Each invocation spawns a FRESH engine (stateless across invocations) -- use
`run` for workflows that must share project state. Prints compact results
(truncated to 4000 chars) -- never floods.
"""
import json
import subprocess
import sys
import os
import shlex
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ENGINE = ROOT / "build" / "HDAW_headless.exe"

# Extra engine argv (from --engine-args), appended after "--mcp-stdio". Parsed
# in main(); EMPTY for a plain invocation.
ENGINE_ARGS = []


def parse_engine_args(argv):
    """Extract `--engine-args "<extra>"` from argv (both `--engine-args X` and
    `--engine-args=X` forms).

    The value is extra engine spawn argv appended verbatim after `--mcp-stdio`,
    e.g. `--engine-args "--project C:\\tmp\\p.hdaw"` to verify the one-shot
    session bootstrap end-to-end. The value is split on whitespace; QUOTE any
    single argument that contains spaces, e.g.
    `--engine-args '--project "C:\\my projects\\p.hdaw"'`.
    Returns (extra_args, argv_without_the_flag).
    """
    extra = []
    cleaned = [argv[0]]
    i = 1
    while i < len(argv):
        a = argv[i]
        if a == "--engine-args":
            if i + 1 >= len(argv):
                sys.exit("--engine-args requires a value")
            extra = [t.strip('"') for t in shlex.split(argv[i + 1], posix=False)]
            i += 2
            continue
        if a.startswith("--engine-args="):
            extra = [t.strip('"')
                     for t in shlex.split(a[len("--engine-args="):], posix=False)]
            i += 1
            continue
        cleaned.append(a)
        i += 1
    return extra, cleaned

def spawn():
    env = dict(os.environ)
    scratch = ROOT / ".tmp_build_scratch" / "lnk"
    scratch.mkdir(parents=True, exist_ok=True)
    env["TMP"] = env["TEMP"] = str(scratch)
    proc = subprocess.Popen(
        [str(ENGINE), "--mcp-stdio"] + ENGINE_ARGS,
        stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL, cwd=str(ROOT), env=env,
        text=True, encoding="utf-8", bufsize=1)
    return proc

# The engine's HDAW_LOG file lives in the child's TMP (redirected in spawn()),
# NOT the client's own %TEMP%.
CHILD_LOG = ROOT / ".tmp_build_scratch" / "lnk" / "hdaw_debug.log"

def send(proc, payload):
    proc.stdin.write(json.dumps(payload) + "\n")
    proc.stdin.flush()

def recv(proc, msg_id, timeout=60):
    import time
    end = time.time() + timeout
    while time.time() < end:
        line = proc.stdout.readline()
        if not line:
            # stdout closed => the engine died (e.g. a --project load failure
            # exits 2). Report the exit code instead of silently hanging.
            try:
                code = proc.wait(timeout=5)
            except Exception:
                code = proc.poll()
            raise RuntimeError(
                f"engine exited (exit code {code}) before responding to id={msg_id} "
                f"- check the debug log ({CHILD_LOG})")
        line = line.strip()
        if not line:
            continue
        try:
            msg = json.loads(line)
        except Exception:
            continue
        if msg.get("id") == msg_id:
            return msg
    raise TimeoutError(f"no response for id={msg_id} in {timeout}s")

def session(proc):
    send(proc, {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {
        "protocolVersion": "2025-03-26", "capabilities": {},
        "clientInfo": {"name": "hdaw-cli", "version": "2.0"}}})
    recv(proc, 1, 30)
    send(proc, {"jsonrpc": "2.0", "method": "notifications/initialized"})

def call(proc, tool, args, timeout=60, ident=2):
    send(proc, {"jsonrpc": "2.0", "id": ident, "method": "tools/call",
                "params": {"name": tool, "arguments": args}})
    return recv(proc, ident, timeout)

def brief(result):
    txt = json.dumps(result.get("result", result), ensure_ascii=False)
    print(txt[:4000])
    if len(txt) > 4000:
        print(f"...[truncated, total {len(txt)} chars]")

def main():
    global ENGINE_ARGS
    ENGINE_ARGS, argv = parse_engine_args(sys.argv)
    mode = argv[1] if len(argv) > 1 else "tools/list"
    proc = spawn()
    try:
        session(proc)
        if mode == "tools/list":
            send(proc, {"jsonrpc": "2.0", "id": 2, "method": "tools/list"})
            r = recv(proc, 2, 30)
            tools = r.get("result", {}).get("tools", [])
            print("tools:", len(tools))
            for t in tools:
                print(t["name"])
        elif mode == "call":
            name = argv[2]
            args = json.loads(argv[3]) if len(argv) > 3 else {}
            brief(call(proc, name, args, timeout=int(argv[4]) if len(argv) > 4 else 60))
        elif mode == "desc":
            send(proc, {"jsonrpc": "2.0", "id": 2, "method": "tools/list"})
            r = recv(proc, 2, 30)
            want = set(argv[2].split(",")) if len(argv) > 2 else set()
            for t in r.get("result", {}).get("tools", []):
                if t["name"] in want:
                    print("==", t["name"])
                    print(t.get("description", "")[:600])
        elif mode == "schemas":
            send(proc, {"jsonrpc": "2.0", "id": 2, "method": "tools/list"})
            r = recv(proc, 2, 30)
            want = set(argv[2].split(",")) if len(argv) > 2 else None
            for t in r.get("result", {}).get("tools", []):
                if want is None or t["name"] in want:
                    print("==", t["name"], json.dumps(t.get("inputSchema", {}), ensure_ascii=False)[:900])
        elif mode == "run":
            steps = json.load(open(argv[2], encoding="utf-8"))
            ident = 10
            for s in steps:
                ident += 1
                print(f"=== step: {s['tool']}")
                try:
                    brief(call(proc, s["tool"], s.get("args", {}),
                               timeout=s.get("timeout", 120), ident=ident))
                except Exception as e:
                    print("STEP ERROR:", e)
                    if s.get("stop_on_error", True):
                        break
    except Exception as e:
        # Loud failure (engine died mid-call, etc.) instead of a bare traceback
        # or a silent hang.
        print("ERROR:", e, file=sys.stderr)
        sys.exit(1)
    finally:
        try:
            proc.stdin.close()
            proc.terminate()
        except Exception:
            pass

main()
