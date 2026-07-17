# HDAW Electron/React Frontend — Phase 4 Design

## Status

Approved. Packages the Electron app with `electron-builder` for distribution,
bundling the compiled renderer, Electron main process, and the Release-mode
engine binary into a single `.exe` installer (NSIS) and portable zip.

## Problem

The frontend currently requires `npm run dev` (Vite dev server) plus a
pre-built Debug engine binary at a hard-coded relative path. No distributable
exists — users must clone the repo and run from source.

## Solution

Use `electron-builder` to produce a self-contained Windows `.exe` (NSIS
installer) and a portable `.zip`. The package includes:

| Component | Source | Destination |
|---|---|---|
| Vite build output | `frontend/dist/` | `app/dist/` |
| Electron main (compiled JS) | `frontend/dist-electron/` | `app/dist-electron/` |
| Engine binary (Release) | `build/Release/HDAW.exe` | `resources/engine/HDAW.exe` |
| Node modules (production) | `frontend/node_modules/` | `app/node_modules/` |

## Path resolution

The Electron `main.ts` already checks `app.isPackaged`. The engine path
resolver is updated:

```
If packaged:  path.join(process.resourcesPath, "engine", "HDAW.exe")
If dev:       path.resolve(__dirname, "..", "..", "build", "Debug", "HDAW.exe")
```

## Build pipeline

```bash
cd frontend
npm run build          # tsc -p tsconfig.node.json + vite build
npm run package        # electron-builder (produces release/ directory)
```

## electron-builder config

Targets:
- `nsis` — single-file installer, one-click or assisted
- `portable` — self-contained `.exe` portable, no installer needed

Version: `0.9.1` (matching the C++ engine).

## Exit criteria

1. `npm run package` produces `release/HDAW Setup 0.9.1.exe` and `release/HDAW 0.9.1.exe` (portable)
2. The packaged app launches, spawns the bundled engine, and renders the UI
3. No hard-coded dev paths in the packaged output
4. Engine crash restart works in the packaged app (MAX_CRASHES=3 guard preserved)

## Files Changed

| File | Change |
|------|--------|
| `frontend/electron-builder.yml` | New config file for electron-builder |
| `frontend/electron/main.ts` | Update `spawnEngine` to resolve path from `resourcesPath` when packaged |
| `frontend/package.json` | Add `electron-builder` dep, `package` script, `build` config |
| `frontend/package-lock.json` | Updated via npm install |
