#!/usr/bin/env python3
"""Port-shape matrix over every installed CLAP instrument.

Drives tools/clap_min_host.c (build it first — the header carries the command)
under TWO host layouts and prints the comparison:

  OLD = HDAW before 2026-10-07: ONE host output port whose channel count is the
        SUM of every plugin port. Surge XT returns CLAP_PROCESS_ERROR -> silence.
  NEW = HDAW after the fix: `audio_outputs_count` equals the plugin's own port
        count. Surge XT renders.

Build the host first:
  gcc -O0 -g -std=c11 -I<repo>/build/clap-juce-extensions-src/clap-libs/clap/include \
      -o /tmp/clapmin tools/clap_min_host.c -ldl -lm

Usage: python3 tools/clap_port_matrix.py [--min-host PATH]
"""
import os
import re
import subprocess

HOST = os.environ.get("CLAPMIN", "/tmp/clapmin")


def discover():
    """Every CLAP *instrument* in HDAW's plugin cache, as (name, path)."""
    import xml.etree.ElementTree as ET

    cache = os.path.expanduser("~/.config/HDAW/plugin_cache.xml")
    out = []
    if not os.path.exists(cache):
        return out
    try:
        root = ET.parse(cache).getroot()
    except Exception:
        return out
    for pl in root:
        if pl.get("format") != "CLAP" or pl.get("isInstrument") != "1":
            continue
        path = pl.get("file")
        if path and os.path.exists(path):
            out.append((pl.get("name"), path))
    return out


def run(path, env_extra):
    """Use the host's own summary FILE: some plugins (NodalRed2x) flood stdout
    with 300 KB+ of emulator logs, which corrupts line-oriented capture."""
    env = dict(os.environ)
    env.update(env_extra)
    summ = "/tmp/_pm_summary.txt"
    env["MINHOST_SUMMARY"] = summ
    if os.path.exists(summ):
        os.remove(summ)
    try:
        subprocess.run([HOST, path], stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL, timeout=180, env=env)
    except subprocess.TimeoutExpired:
        return ("TIMEOUT", 0.0, -1, 0)
    if not os.path.exists(summ):
        return ("CRASH/no-summary", 0.0, -1, 0)
    txt = open(summ).read()
    m = re.search(r"PEAK=([\d.]+) RMS=([\d.]+) blocks=\d+ ports=(\d+) status=(-?\d+)", txt)
    if not m:
        return ("unparseable:" + txt[:40], 0.0, -1, 0)
    return (0, float(m.group(1)), int(m.group(4)), int(m.group(3)))


def main():
    print(f"{'plugin':12s} {'ports':>5s} | {'OLD (1 summed port)':>30s} | {'NEW (1 port/plugin port)':>26s}")
    print(f"{'':12s} {'':>5s} | {'status':>6s} {'peak':>9s} {'verdict':>12s} | {'status':>6s} {'peak':>9s} {'verdict':>8s}")
    print("-" * 96)
    claps = discover()
    if not claps:
        print("no CLAP instruments found in ~/.config/HDAW/plugin_cache.xml")
        return 1
    if not os.path.exists(HOST):
        print(f"minimal host not found at {HOST}; build tools/clap_min_host.c first")
        return 1
    N = int(os.environ.get("MINHOST_REPEATS", "3"))
    print(f"(max peak over {N} runs per shape — the gearmulator plugins have a "
          f"free-running clock and vary run to run)")
    print()
    for name, path in claps:
        if not os.path.exists(path):
            print(f"{name:12s} --- not installed")
            continue
        # discover the plugin's port count in the NEW shape
        _, _, _, ports = run(path, {"MINHOST_MULTIPORT": "1"})
        res_old = [run(path, {"MINHOST_OUTCH": "6"}) for _ in range(N)]
        res_new = [run(path, {"MINHOST_MULTIPORT": "1"}) for _ in range(N)]
        pk_old = max(r[1] for r in res_old)
        pk_new = max(r[1] for r in res_new)
        st_old = res_old[-1][2]
        st_new = res_new[-1][2]
        err_old = sum(1 for r in res_old if r[2] == 0)
        err_new = sum(1 for r in res_new if r[2] == 0)
        vo = "AUDIBLE" if pk_old > 0.001 else "SILENT"
        vn = "AUDIBLE" if pk_new > 0.001 else "SILENT"
        eo = f" ({err_old}/{N} ERROR)" if err_old else ""
        en = f" ({err_new}/{N} ERROR)" if err_new else ""
        print(f"{name:12s} {ports:>5d} | {st_old:>6d} {pk_old:>9.5f}{eo:>11s} {vo:>9s} | "
              f"{st_new:>6d} {pk_new:>9.5f}{en:>11s} {vn:>8s}")
    print()
    print("OLD = HDAW before the fix (buildBuses summed every port into ONE host port)")
    print("NEW = HDAW after the fix (audio_outputs_count == the plugin's port count)")
    print()
    print("A plugin SILENT in BOTH shapes is a DIFFERENT defect, not a port-layout one:")
    print("  Osirus  -> closed filter at boot (read Cutoff back and reopen it)")
    print("  OsTIrus -> crashes inside its own emulator before any render")


if __name__ == "__main__":
    main()
