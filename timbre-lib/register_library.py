#!/usr/bin/env python3
"""Register a folder as an HDAW audio library in registry.json.

Called by analyze.sh --library NAME. Deduplicates by path, prunes dead
hdaw_tests temp entries, and never touches entries for live paths. The
engine loads the registry at construction, so a running engine must be
restarted (or a new session started) to see the new entry; scan_library
then ingests the .timbre.json sidecars this pipeline wrote.
"""
import argparse, json, os, re, secrets, sys

def wsl_to_win(p):
    m = re.match(r"^/mnt/([a-zA-Z])/(.*)$", p)
    if m:
        return m.group(1).upper() + ":\\" + m.group(2).replace("/", "\\")
    return p

def default_registry():
    # %APPDATA% on Windows via WSL interop, with a sane fallback.
    try:
        import subprocess
        out = subprocess.run(
            ["cmd.exe", "/c", "echo", "%APPDATA%"],
            capture_output=True, text=True, timeout=15).stdout.strip()
        appdata = out.split("\\") and out  # keep raw
        m = re.match(r"^([A-Z]):\\(.*)$", out)
        if m:
            # %APPDATA% uses backslashes; on the WSL side they are filename
            # characters, not separators - normalize or the registry write
            # lands in a bogus literal-backslash directory.
            return f"/mnt/{m.group(1).lower()}/{m.group(2).replace(chr(92), '/')}/HDAW/libraries/registry.json"
    except Exception:
        pass
    import glob
    hits = glob.glob("/mnt/c/Users/*/AppData/Roaming/HDAW/libraries/registry.json")
    return hits[0] if hits else None

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--path", required=True, help="folder (WSL or Windows path)")
    ap.add_argument("--name", required=True)
    ap.add_argument("--type", default="audio", choices=["audio", "midi"],
                    help="library type (default: audio)")
    a = ap.parse_args()

    folder = a.path
    win_path = wsl_to_win(folder) if folder.startswith("/") else folder
    wsl_path = folder if folder.startswith("/") else None

    reg = os.environ.get("HDAW_REGISTRY") or default_registry()
    if not reg:
        print("error: cannot locate HDAW registry.json (set HDAW_REGISTRY)", file=sys.stderr)
        sys.exit(1)

    libs = []
    if os.path.exists(reg):
        try:
            libs = json.load(open(reg)).get("libraries", [])
        except Exception as e:
            print(f"error: cannot parse {reg}: {e}", file=sys.stderr)
            sys.exit(1)

    def same_path(p):
        if not p:
            return False
        pn = p.replace("\\", "/").lower().rstrip("/")
        return pn == win_path.replace("\\", "/").lower().rstrip("/") or (
            wsl_path and pn == wsl_path.lower().rstrip("/"))

    # prune dead hdaw_tests temp entries (test-suite leftovers)
    pruned = [l for l in libs
              if not ("hdaw_tests" in l.get("path", "") and not os.path.isdir(
                  l["path"].replace("\\", "/").replace("D:/", "/mnt/d/").replace("C:/", "/mnt/c/")
                  if l["path"][1:2] == ":" else l["path"]))]
    if len(pruned) != len(libs):
        print(f"pruned {len(libs) - len(pruned)} dead hdaw_tests entries")

    hit = next((l for l in pruned if same_path(l.get("path"))), None)
    if hit is not None:
        if hit.get("name") != a.name:
            hit["name"] = a.name
            json.dump({"libraries": pruned}, open(reg, "w"), indent=2)
            print(f"renamed library '{win_path}' -> '{a.name}'")
        else:
            print(f"library already registered: '{a.name}' -> {win_path} (id {hit['id']})")
        return

    entry = {"id": secrets.token_hex(6), "name": a.name, "path": win_path,
             "type": a.type, "lastScan": "", "fileCount": 0, "autoScan": False}
    pruned.append(entry)
    os.makedirs(os.path.dirname(reg), exist_ok=True)
    json.dump({"libraries": pruned}, open(reg, "w"), indent=2)
    print(f"registered library '{a.name}' -> {win_path} (id {entry['id']}) in {reg}")

if __name__ == "__main__":
    main()
