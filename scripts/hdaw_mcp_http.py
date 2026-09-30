#!/usr/bin/env python3
"""MCP **HTTP** client for an HDAW engine that is ALREADY RUNNING and serving
MCP over HTTP (default http://127.0.0.1:18765/mcp, loopback, no auth, no
session id).

Stdio twin: `scripts/mcp_call.py` -- that one spawns a FRESH HDAW_headless.exe
per invocation (stateless across invocations). This one talks to the SHARED
engine, so project state / batch state are the same ones the GUI (or another
agent) sees, and nothing is spawned or killed.

Why it exists: the repo's skill docs assume a host-provided
`await mcp({server: 'hdaw-http', tool: ..., args: ...})` tool. Agents hosted by
DSH have no such tool, so this CLI is the transport to the same shared engine.

Usage:
  python scripts/hdaw_mcp_http.py tools [--filter SUBSTR]   # count + names
  python scripts/hdaw_mcp_http.py whoami
  python scripts/hdaw_mcp_http.py desc NAME1,NAME2          # first 600 chars each
  python scripts/hdaw_mcp_http.py schemas [NAME1,NAME2]     # inputSchema <=900 chars
  python scripts/hdaw_mcp_http.py call TOOL '<json-args>' [--timeout SEC] [--full]
  python scripts/hdaw_mcp_http.py run STEPS.json

steps.json = [{"tool": "name", "args": {...}, "timeout": 600,
               "stop_on_error": true}, ...]
  * `timeout` is per step (default 60 s); `--timeout SEC` overrides every step.
  * `stop_on_error` defaults to true -- the first failing step ends the run.
  * The run exits non-zero if any step failed.

Response framing: BOTH `application/json` bodies and `text/event-stream` (SSE)
bodies are handled. For SSE the `data: ` lines are scanned and the JSON object
whose `id` matches the request id is used. (Measured 2026-09-29 against engine
v0.39.2: the engine answers `application/json` even when the request only
accepts `text/event-stream`, so the SSE branch is defensive/unverified against
this engine -- it is kept for transport-compatible servers.)

Failure is LOUD: a JSON-RPC `error`, a tool result with `isError: true`, an
unreachable endpoint, or an unparseable body prints a clear message on stderr
and exits non-zero. On success nothing but the payload is printed (no banner
noise), so results can be piped/grepped. Long tool text is truncated to 4000
chars (`...[truncated, total N chars]`, same convention as mcp_call.py) unless
`--full` is given.

urllib only -- no third-party dependencies.

stdout/stderr are forced to utf-8 (`errors="replace"`) by the script itself, so
non-ASCII payloads (arrows, ellipses, em-dashes in the tool registry) print on a
cp1252 Windows console without PYTHONIOENCODING being set.
"""
import json
import os
import sys
import time
import urllib.error
import urllib.request

DEFAULT_URL = "http://127.0.0.1:18765/mcp"
URL = (os.environ.get("HDAW_MCP_URL") or "").strip() or DEFAULT_URL

DEFAULT_TIMEOUT = 60.0
BRIEF_CHARS = 4000
DESC_CHARS = 600
SCHEMA_CHARS = 900

_ID = [0]


def next_id():
    _ID[0] += 1
    return _ID[0]


# --------------------------------------------------------------------------- #
# transport
# --------------------------------------------------------------------------- #
def post(payload, timeout):
    """POST one JSON-RPC payload; return the open response object.

    `timeout` is the socket timeout (per blocking operation), so a stalled
    server raises instead of hanging forever.
    """
    data = json.dumps(payload).encode("utf-8")
    req = urllib.request.Request(URL, data=data, headers={
        "Content-Type": "application/json",
        "Accept": "application/json, text/event-stream",
    })
    return urllib.request.urlopen(req, timeout=timeout)


def _sse_objects(stream, deadline):
    """Yield the JSON objects carried by `data: ` lines of an SSE stream.

    The `id` of the JSON-RPC response rides INSIDE the data payload, so each
    data line is one parseable object. Comment lines (`:`), `event:`/`id:`
    fields and `[DONE]` sentinels are skipped.
    """
    for raw in stream:
        if time.time() > deadline:
            raise TimeoutError("timed out while scanning the SSE stream")
        line = raw.decode("utf-8", "replace") if isinstance(raw, bytes) else raw
        line = line.strip()
        if not line or line.startswith(":"):
            continue
        if not line.startswith("data:"):
            continue
        body = line[5:].strip()
        if not body or body == "[DONE]":
            continue
        try:
            obj = json.loads(body)
        except ValueError:
            continue
        if isinstance(obj, dict):
            yield obj


def _scan_sse_text(text, msg_id):
    """Defensive fallback: find id-matching JSON in an already-read body that
    turned out to be SSE-framed despite an `application/json` content type."""
    for line in text.splitlines():
        line = line.strip()
        if not line.startswith("data:"):
            continue
        body = line[5:].strip()
        if not body or body == "[DONE]":
            continue
        try:
            obj = json.loads(body)
        except ValueError:
            continue
        if isinstance(obj, dict) and obj.get("id") == msg_id:
            return obj
    return None


def recv(resp, msg_id, deadline):
    """Decode one JSON-RPC response, matching `msg_id` for SSE framing."""
    ctype = ""
    try:
        ctype = (resp.headers.get("Content-Type") or "").lower()
    except Exception:
        pass

    if "text/event-stream" in ctype:
        for obj in _sse_objects(resp, deadline):
            if obj.get("id") == msg_id:
                return obj
        raise RuntimeError(
            f"SSE stream ended without a response for id={msg_id}")

    body = resp.read()
    text = body.decode("utf-8", "replace")
    if not text.strip():
        raise RuntimeError(
            f"empty response body for id={msg_id} (HTTP {getattr(resp, 'status', '?')})")
    try:
        return json.loads(text)
    except ValueError:
        obj = _scan_sse_text(text, msg_id)
        if obj is not None:
            return obj
        raise RuntimeError(
            "unparseable response body (neither JSON nor id-matching SSE): "
            + text[:200].replace("\n", " "))


def rpc(method, params=None, timeout=DEFAULT_TIMEOUT):
    """One JSON-RPC round trip. Returns the decoded response object."""
    msg_id = next_id()
    payload = {"jsonrpc": "2.0", "id": msg_id, "method": method}
    if params is not None:
        payload["params"] = params
    deadline = time.time() + float(timeout)
    try:
        resp = post(payload, timeout)
    except urllib.error.HTTPError as e:
        detail = ""
        try:
            detail = e.read().decode("utf-8", "replace")[:300]
        except Exception:
            pass
        raise RuntimeError(f"HTTP {e.code} from {URL}: {e.reason} {detail}".strip())
    except urllib.error.URLError as e:
        raise RuntimeError(
            f"cannot reach MCP HTTP endpoint at {URL}: {e.reason} "
            f"-- is the shared HDAW engine running with --mcp-http? "
            f"(override with HDAW_MCP_URL)")
    except TimeoutError:
        raise TimeoutError(f"no HTTP response from {URL} within {timeout}s")
    try:
        return recv(resp, msg_id, deadline)
    finally:
        try:
            resp.close()
        except Exception:
            pass


# --------------------------------------------------------------------------- #
# helpers
# --------------------------------------------------------------------------- #
def fail(msg, code=1):
    """Loud failure: message on stderr, non-zero exit. Never silent."""
    print("ERROR:", msg, file=sys.stderr)
    sys.exit(code)


def brief(text, full=False, limit=BRIEF_CHARS):
    """Print `text`, truncated to `limit` with mcp_call.py's marker."""
    if full or len(text) <= limit:
        print(text)
        return
    print(text[:limit])
    print(f"...[truncated, total {len(text)} chars]")


def tool_text(result):
    """The text content of a tools/call result (falls back to the raw JSON)."""
    if isinstance(result, dict):
        parts = [c.get("text", "") for c in (result.get("content") or [])
                 if isinstance(c, dict) and c.get("type") == "text"]
        text = "".join(parts)
        if text:
            return text
        if result.get("structuredContent") is not None:
            return json.dumps(result["structuredContent"], ensure_ascii=False)
    return json.dumps(result, ensure_ascii=False)


def checked(resp, tool):
    """Raise on JSON-RPC error / isError result; return the result object."""
    if not isinstance(resp, dict):
        raise RuntimeError(f"malformed JSON-RPC response: {resp!r}")
    if "error" in resp and resp["error"] is not None:
        err = resp["error"]
        if isinstance(err, dict):
            raise RuntimeError(
                "JSON-RPC error {}: {}".format(
                    err.get("code", "?"), err.get("message", json.dumps(err)[:300])))
        raise RuntimeError(f"JSON-RPC error: {err}")
    result = resp.get("result")
    if result is None:
        raise RuntimeError(f"no result for {tool}: {json.dumps(resp)[:300]}")
    if isinstance(result, dict) and result.get("isError"):
        raise RuntimeError(f"tool '{tool}' failed: {tool_text(result)[:1500]}")
    return result


def list_tools():
    resp = rpc("tools/list", None, timeout=30)
    if not isinstance(resp, dict) or "result" not in resp:
        raise RuntimeError(f"tools/list failed: {json.dumps(resp)[:300]}")
    if resp.get("error"):
        raise RuntimeError(f"tools/list JSON-RPC error: {json.dumps(resp['error'])[:300]}")
    return resp["result"].get("tools", [])


def call_tool(tool, args, timeout=DEFAULT_TIMEOUT):
    resp = rpc("tools/call", {"name": tool, "arguments": args}, timeout=timeout)
    return checked(resp, tool)


def split_names(spec):
    return [n.strip() for n in spec.split(",") if n.strip()] if spec else []


def parse_flags(argv, value_flags=(), bool_flags=()):
    """Extract `--flag VALUE` / `--flag=VALUE` / bare bool flags from argv.

    Returns (positionals, flags). Unknown `--x` tokens stay positional so a
    typo surfaces as a clear "unknown mode"/JSON error instead of being eaten.
    """
    flags, pos, i = {}, [], 0
    while i < len(argv):
        a = argv[i]
        matched = False
        for f in value_flags:
            if a == f:
                if i + 1 >= len(argv):
                    fail(f"{f} requires a value", 2)
                flags[f.lstrip('-')] = argv[i + 1]
                i += 2
                matched = True
                break
            if a.startswith(f + "="):
                flags[f.lstrip('-')] = a[len(f) + 1:]
                i += 1
                matched = True
                break
        if matched:
            continue
        if a in bool_flags:
            flags[a.lstrip('-')] = True
            i += 1
            continue
        pos.append(a)
        i += 1
    return pos, flags


def json_arg(text, what="args"):
    try:
        val = json.loads(text)
    except ValueError as e:
        fail(f"{what} is not valid JSON ({e}): {text[:200]!r}", 2)
    return val


# --------------------------------------------------------------------------- #
# modes
# --------------------------------------------------------------------------- #
def mode_tools(argv):
    _, flags = parse_flags(argv, value_flags=("--filter",), bool_flags=("--full",))
    flt = flags.get("filter")
    tools = list_tools()
    total = len(tools)
    shown = [t for t in tools if flt is None or flt.lower() in t.get("name", "").lower()]
    if flt is None:
        print(f"tools: {total}")
    else:
        print(f"tools: {len(shown)} (of {total}, filter {flt!r})")
    for t in shown:
        print(t.get("name", "?"))


def mode_whoami(argv):
    _, flags = parse_flags(argv, bool_flags=("--full",))
    result = call_tool("whoami", {})
    brief(tool_text(result), full=flags.get("full", False))


def mode_desc(argv):
    pos, flags = parse_flags(argv, bool_flags=("--full",))
    want = split_names(pos[0]) if pos else None
    tools = list_tools()
    seen = set()
    for t in tools:
        name = t.get("name", "")
        if want is None or name in want:
            seen.add(name)
            print("==", name)
            desc = t.get("description", "") or ""
            print(desc if flags.get("full") else desc[:DESC_CHARS])
    for name in (want or []):
        if name not in seen:
            print(f"WARNING: no such tool: {name}", file=sys.stderr)


def mode_schemas(argv):
    pos, flags = parse_flags(argv, bool_flags=("--full",))
    want = split_names(pos[0]) if pos else None
    tools = list_tools()
    seen = set()
    for t in tools:
        name = t.get("name", "")
        if want is None or name in want:
            seen.add(name)
            schema = json.dumps(t.get("inputSchema", {}), ensure_ascii=False)
            print("==", name, schema if flags.get("full") else schema[:SCHEMA_CHARS])
    for name in (want or []):
        if name not in seen:
            print(f"WARNING: no such tool: {name}", file=sys.stderr)


def mode_call(argv):
    pos, flags = parse_flags(argv, value_flags=("--timeout",), bool_flags=("--full",))
    if not pos:
        fail("call requires a tool name: call TOOL '<json-args>' [--timeout SEC] [--full]", 2)
    tool = pos[0]
    args = json_arg(pos[1], "args") if len(pos) > 1 else {}
    if not isinstance(args, dict):
        fail(f"args must be a JSON object, got {type(args).__name__}", 2)
    timeout = float(flags["timeout"]) if "timeout" in flags else DEFAULT_TIMEOUT
    result = call_tool(tool, args, timeout=timeout)
    brief(tool_text(result), full=flags.get("full", False))


def mode_run(argv):
    pos, flags = parse_flags(argv, value_flags=("--timeout",), bool_flags=("--full",))
    if not pos:
        fail("run requires a steps file: run STEPS.json [--timeout SEC] [--full]", 2)
    path = pos[0]
    try:
        with open(path, encoding="utf-8") as fh:
            steps = json.load(fh)
    except OSError as e:
        fail(f"cannot read steps file {path}: {e}", 2)
    except ValueError as e:
        fail(f"steps file {path} is not valid JSON: {e}", 2)
    if not isinstance(steps, list):
        fail(f"steps file {path} must contain a JSON array of steps", 2)

    override = float(flags["timeout"]) if "timeout" in flags else None
    full = flags.get("full", False)
    failed = 0
    for idx, step in enumerate(steps):
        if not isinstance(step, dict) or "tool" not in step:
            print(f"=== step {idx + 1}: <malformed step>", file=sys.stderr)
            failed += 1
            break
        tool = step["tool"]
        print(f"=== step: {tool}")
        timeout = override if override is not None else float(
            step.get("timeout", DEFAULT_TIMEOUT))
        try:
            result = call_tool(tool, step.get("args", {}) or {}, timeout=timeout)
            brief(tool_text(result), full=full)
        except Exception as e:
            print("STEP ERROR:", e, file=sys.stderr)
            failed += 1
            if step.get("stop_on_error", True):
                break
    if failed:
        sys.exit(1)


MODES = {
    "tools": mode_tools,
    "whoami": mode_whoami,
    "desc": mode_desc,
    "schemas": mode_schemas,
    "call": mode_call,
    "run": mode_run,
}

USAGE = (
    "usage: python scripts/hdaw_mcp_http.py "
    "{tools [--filter SUBSTR] | whoami | desc NAME1,NAME2 | schemas [NAME1,NAME2] | "
    "call TOOL '<json-args>' [--timeout SEC] [--full] | run STEPS.json}\n"
    f"endpoint: {URL} (env HDAW_MCP_URL)"
)


def use_utf8_console():
    """Force utf-8 on our own stdout/stderr -- do NOT rely on the environment.

    Windows consoles and pipes default to the ANSI code page (cp1252 here), and
    the engine registry is full of non-ASCII: `->` arrows, `...` ellipses,
    em-dashes in patch/section names. Under cp1252 a `print()` of such a payload
    raises UnicodeEncodeError, so the payload is lost entirely and the process
    exits 1 for a reason that is not the engine's fault. `errors="replace"`
    guarantees a payload is never lost to one unencodable byte.
    """
    for _stream in (sys.stdout, sys.stderr):
        try:
            _stream.reconfigure(encoding="utf-8", errors="replace")
        except (AttributeError, ValueError):
            pass


def main():
    use_utf8_console()
    argv = sys.argv[1:]
    if not argv or argv[0] in ("-h", "--help", "help"):
        print(USAGE)
        sys.exit(0 if argv else 2)
    mode = argv[0]
    if mode not in MODES:
        fail(f"unknown mode {mode!r}\n{USAGE}", 2)
    try:
        MODES[mode](argv[1:])
    except SystemExit:
        raise
    except Exception as e:
        fail(e)


if __name__ == "__main__":
    try:
        main()
    except BrokenPipeError:
        # `... | Select-Object -First 3` closes the pipe early; keep the
        # interpreter's shutdown quiet instead of printing an ignored-exception
        # traceback into the pipeline.
        try:
            devnull = os.open(os.devnull, os.O_WRONLY)
            os.dup2(devnull, sys.stdout.fileno())
        except Exception:
            pass
        sys.exit(0)
