#!/usr/bin/env python3
"""pytest suite for the HTTP-MCP CLI's output contract (hdaw_mcp_http.py).

Locks the truncation rules: `call` prints the FULL payload by default and
stdout is machine-parseable (payload bytes only -- any truncation notice goes
to STDERR); truncation happens only behind the explicit `--brief` flag.
`whoami`/`run` keep their documented truncate-by-default behaviour with
`--full` as the unlock. No network, no engine: the transport is monkeypatched.
"""
import io
import json
import os
import sys

import pytest

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import hdaw_mcp_http as M

LIMIT = M.BRIEF_CHARS  # 4000


def _capture(fn, *args, **kwargs):
    """Run fn() with swapped stdout/stderr; return (out, err) strings."""
    out, err = io.StringIO(), io.StringIO()
    old = sys.stdout, sys.stderr
    sys.stdout, sys.stderr = out, err
    try:
        fn(*args, **kwargs)
    finally:
        sys.stdout, sys.stderr = old
    return out.getvalue(), err.getvalue()


def _text_result(text):
    """A tools/call result whose content is one text block."""
    return {"content": [{"type": "text", "text": text}]}


def _notice(total):
    return f"...[truncated, total {total} chars]\n"


# ── brief(): the shared printer ──────────────────────────────────────────────

def test_brief_limit_none_prints_full_regardless_of_length():
    for text in ("tiny", "x" * (LIMIT * 3)):
        out, err = _capture(M.brief, text, limit=None)
        assert out == text + "\n"
        assert err == ""


def test_brief_short_text_untouched():
    text = "x" * (LIMIT - 1)
    out, err = _capture(M.brief, text)  # default numeric limit
    assert out == text + "\n"
    assert err == ""


def test_brief_exact_boundary_not_truncated():
    text = "x" * LIMIT
    out, err = _capture(M.brief, text)
    assert out == text + "\n"
    assert err == ""


def test_brief_truncation_keeps_notice_off_stdout():
    text = "x" * (LIMIT + 1234)
    out, err = _capture(M.brief, text)
    assert out == text[:LIMIT] + "\n"
    assert err == _notice(len(text))


# ── mode_call: full by default, --brief is the only truncation switch ────────

@pytest.fixture()
def fake_call(monkeypatch):
    """Replace the transport; record (tool, args, timeout), return canned."""
    calls = {}

    def install(result):
        def fake(tool, args, timeout=M.DEFAULT_TIMEOUT):
            calls.update(tool=tool, args=args, timeout=timeout)
            return result
        monkeypatch.setattr(M, "call_tool", fake)
        return calls

    return install


def test_call_default_full_payload_stdout_clean(fake_call):
    payload = json.dumps({"ok": True, "rows": ["y" * 5000], "n": 12})
    fake_call(_text_result(payload))
    out, err = _capture(M.mode_call, ["some_tool", '{"k":1}'])
    assert out == payload + "\n"
    assert err == ""
    assert json.loads(out) == json.loads(payload)  # stdout machine-parseable


def test_call_long_payload_default_not_truncated(fake_call):
    payload = "x" * (LIMIT * 3)
    fake_call(_text_result(payload))
    out, err = _capture(M.mode_call, ["some_tool", "{}"])
    assert out == payload + "\n"
    assert err == ""


def test_call_brief_flag_truncates_stdout_only(fake_call):
    payload = json.dumps({"big": "z" * (LIMIT * 2)})
    fake_call(_text_result(payload))
    out, err = _capture(M.mode_call, ["some_tool", "{}", "--brief"])
    assert out == payload[:LIMIT] + "\n"
    assert err == _notice(len(payload))


def test_call_brief_combines_with_timeout(fake_call):
    calls = fake_call(_text_result("ok"))
    out, err = _capture(M.mode_call, ["t", "{}", "--timeout", "5", "--brief"])
    assert calls["timeout"] == 5.0
    assert calls["tool"] == "t"
    assert calls["args"] == {}
    assert out == "ok\n"
    assert err == ""


def test_call_requires_tool_name_mentions_brief():
    with pytest.raises(SystemExit) as ei:
        _capture(M.mode_call, [])
    assert ei.value.code == 2


def test_call_non_object_args_rejected():
    with pytest.raises(SystemExit) as ei:
        _capture(M.mode_call, ["t", "[1,2]"])
    assert ei.value.code == 2


# ── whoami / run: truncate-by-default contract preserved, notice on stderr ──

def test_whoami_truncates_by_default_notice_on_stderr(fake_call):
    payload = "w" * (LIMIT + 10)
    fake_call(_text_result(payload))
    out, err = _capture(M.mode_whoami, [])
    assert out == payload[:LIMIT] + "\n"
    assert err == _notice(len(payload))


def test_whoami_full_unlocks(fake_call):
    payload = "w" * (LIMIT + 10)
    fake_call(_text_result(payload))
    out, err = _capture(M.mode_whoami, ["--full"])
    assert out == payload + "\n"
    assert err == ""


def test_run_step_truncates_by_default_notice_on_stderr(fake_call, tmp_path):
    payload = "r" * (LIMIT + 7)
    fake_call(_text_result(payload))
    steps = tmp_path / "steps.json"
    steps.write_text(json.dumps([{"tool": "t", "args": {}}]), encoding="utf-8")
    out, err = _capture(M.mode_run, [str(steps)])
    assert out == "=== step: t\n" + payload[:LIMIT] + "\n"
    assert err == _notice(len(payload))


def test_run_full_flag_unlocks(fake_call, tmp_path):
    payload = "r" * (LIMIT + 7)
    fake_call(_text_result(payload))
    steps = tmp_path / "steps.json"
    steps.write_text(json.dumps([{"tool": "t", "args": {}}]), encoding="utf-8")
    out, err = _capture(M.mode_run, [str(steps), "--full"])
    assert out == "=== step: t\n" + payload + "\n"
    assert err == ""
