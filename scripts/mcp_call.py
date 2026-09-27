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
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ENGINE = ROOT / "build" / "HDAW_headless.exe"

def spawn():
    env = dict(os.environ)
    scratch = ROOT / ".tmp_build_scratch" / "lnk"
    scratch.mkdir(parents=True, exist_ok=True)
    env["TMP"] = env["TEMP"] = str(scratch)
    proc = subprocess.Popen(
        [str(ENGINE), "--mcp-stdio"],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL, cwd=str(ROOT), env=env,
        text=True, encoding="utf-8", bufsize=1)
    return proc

def send(proc, payload):
    proc.stdin.write(json.dumps(payload) + "\n")
    proc.stdin.flush()

def recv(proc, msg_id, timeout=60):
    import time
    end = time.time() + timeout
    while time.time() < end:
        line = proc.stdout.readline()
        if not line:
            raise RuntimeError("engine closed stdout")
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
    mode = sys.argv[1] if len(sys.argv) > 1 else "tools/list"
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
            name = sys.argv[2]
            args = json.loads(sys.argv[3]) if len(sys.argv) > 3 else {}
            brief(call(proc, name, args, timeout=int(sys.argv[4]) if len(sys.argv) > 4 else 60))
        elif mode == "desc":
            send(proc, {"jsonrpc": "2.0", "id": 2, "method": "tools/list"})
            r = recv(proc, 2, 30)
            want = set(sys.argv[2].split(",")) if len(sys.argv) > 2 else set()
            for t in r.get("result", {}).get("tools", []):
                if t["name"] in want:
                    print("==", t["name"])
                    print(t.get("description", "")[:600])
        elif mode == "schemas":
            send(proc, {"jsonrpc": "2.0", "id": 2, "method": "tools/list"})
            r = recv(proc, 2, 30)
            want = set(sys.argv[2].split(",")) if len(sys.argv) > 2 else None
            for t in r.get("result", {}).get("tools", []):
                if want is None or t["name"] in want:
                    print("==", t["name"], json.dumps(t.get("inputSchema", {}), ensure_ascii=False)[:900])
        elif mode == "run":
            steps = json.load(open(sys.argv[2], encoding="utf-8"))
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
    finally:
        try:
            proc.stdin.close()
            proc.terminate()
        except Exception:
            pass

main()
