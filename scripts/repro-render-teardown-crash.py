#!/usr/bin/env python3
"""Reproducer: HDAW engine SIGABRT (glibc "double free or corruption (out)")
during offline-render teardown.

Crash-dump trace (2026-10-06, verified with a core captured by
scripts/hdaw-engine-coredump.sh):

    glibc: fatal "double free or corruption (out)"
      malloc_printerr
      -> _int_free_merge_chunk (already-freed 48-byte chunk)
      -> std::_Rb_tree<juce::AudioProcessorGraph::NodeID>::_M_erase
      -> juce::NodeStates::clear()                 juce_AudioProcessorGraph.cpp:497
      -> juce::AudioProcessorGraph::Pimpl::clear()  :1715/:1722
      -> juce::AudioProcessorGraph::clear()         :1978   (UpdateKind::sync)
      -> HDAW::ExportManager::renderThreadFunc      src/engine/ExportManager.cpp:683

Mechanism: the render thread's `renderGraph.clear()` calls `NodeStates::clear()`,
which mutates its `std::set<NodeID> preparedNodes` WITHOUT holding the class
mutex -- while the same set is mutated UNDER the mutex by
`NodeStates::applySettings()` from the graph's LockingAsyncUpdater, serviced on
the message pump. Concurrent mutation of a std::set corrupts the red-black tree
and the next erase double-frees (classic unstructured data race).

Trigger (measured): alternating a PROJECT render (export_audio, which goes
through launchProjectRender/renderTrackWindow's sibling) with bursts of
WINDOWED renders (verify_part -> 2 renders/call). verify_part ALONE did not
reproduce in ~414 renders; the export+verify_part interleave crashed at
cycle 9 (~9 min).

Run: engine must already be serving MCP HTTP (see scripts/hdaw-engine-coredump.sh
so a core is captured on the abort).
    HDAW_MCP_URL=http://127.0.0.1:18797/mcp python3 scripts/repro-render-teardown-crash.py
"""
import json
import os
import subprocess
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROJECT = os.path.join(REPO, "compositions", "rift_dub", "rift_dub_final.hdaw")


def call(tool, args, timeout=180):
    r = subprocess.run(
        ["python3", os.path.join(REPO, "scripts", "hdaw_mcp_http.py"),
         "call", tool, json.dumps(args), "--timeout", str(timeout)],
        cwd=REPO, capture_output=True, text=True)
    return r.stdout.strip() or r.stderr.strip()


def dead(s):
    return "Connection refused" in s or "Remote end closed" in s


def main():
    cycles = int(sys.argv[1]) if len(sys.argv) > 1 else 30
    print("load:", call("load_project", {"filePath": PROJECT})[:60])
    out = os.path.join(REPO, "compositions", "rift_dub", "repro_probe.wav")
    t0 = time.time()
    for cyc in range(cycles):
        # (1) PROJECT render: export_audio -> ExportManager::startExport with the
        #     MCP/Router onComplete installed on the shared ExportManager.
        r = call("export_audio", {"outputPath": out, "wait": True,
                                  "startBeat": 128, "endBeat": 144}, timeout=180)
        if dead(r):
            print(f"*** CRASH during export_audio, cycle {cyc}"); return 1
        # (2) WINDOWED renders: verify_part issues two offline renders per call,
        #     each a fresh render graph torn down on a fresh render thread.
        for k in range(4):
            v = call("verify_part", {"trackIndex": k + 1, "windowSeconds": 4,
                                     "startBeat": 128, "endBeat": 144})
            if dead(v):
                print(f"*** CRASH during verify_part cycle {cyc} track {k+1} "
                      f"({time.time()-t0:.0f}s) -- core in crash-captures/")
                return 1
        if cyc % 5 == 0:
            print(f"  cycle {cyc} ok ({time.time()-t0:.0f}s)")
    print(f"no crash in {cycles} cycles ({time.time()-t0:.0f}s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
