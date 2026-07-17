# HDAW Electron/React Frontend — Phase 4 Implementation Plan

**Goal:** Configure `electron-builder` to package the Electron app for
distribution, bundling the renderer, Electron shell, and engine binary.

---

### Task 0: Write spec + plan docs

- [ ] **Done**

---

### Task 1: Create `electron-builder.yml`

Create `frontend/electron-builder.yml`:

```yaml
appId: com.hdaw.daw
productName: HDAW
copyright: "Copyright 2026 HDAW"

directories:
  output: release
  buildResources: build-resources

files:
  - dist/**/*
  - dist-electron/**/*
  - package.json

extraResources:
  - from: ../build/Release/HDAW.exe
    to: engine/HDAW.exe

win:
  target:
    - target: nsis
      arch: [x64]
    - target: portable
      arch: [x64]

nsis:
  oneClick: false
  allowToChangeInstallationDirectory: true
  createDesktopShortcut: true
  createStartMenuShortcut: true

portable:
  artifactName: "${productName} ${version} Portable.exe"
```

### Task 2: Update `electron/main.ts` — packaged engine path

Modify `spawnEngine` to resolve the engine path based on `app.isPackaged`:

```typescript
function enginePath(): string {
  if (app.isPackaged) {
    return path.join(process.resourcesPath, "engine", "HDAW.exe");
  }
  return path.resolve(__dirname, "..", "..", "build", "Debug", "HDAW.exe");
}
```

Replace the inline path in `spawnEngine` with `enginePath()`.

### Task 3: Update `package.json`

Add `electron-builder` to devDependencies and the `package` script plus
`build` config for electron-builder:

```json
{
  "version": "0.9.1",
  "scripts": {
    "build": "tsc -p tsconfig.node.json && vite build",
    "electron:dev": "concurrently -k \"vite\" \"wait-on http://localhost:5173 && tsc -p tsconfig.node.json && electron .\"",
    "package": "electron-builder --win --x64",
    "package:dir": "electron-builder --win --x64 --dir"
  },
  "devDependencies": {
    "electron-builder": "^25.1.0"
  }
}
```

### Task 4: Build engine (Release) + install electron-builder

```bash
cd frontend
npm install
```

### Task 5: Build the package (verification)

```bash
cd frontend
npm run build
npm run package:dir
```

This creates `frontend/release/win-unpacked/` — a directory with the
unpacked app. Verifies the build succeeds without errors.

### Task 6: Commit

```bash
git add frontend/electron-builder.yml frontend/electron/main.ts frontend/package.json frontend/package-lock.json docs/superpowers/specs/2026-07-17-hdaw-electron-frontend-phase4-design.md docs/superpowers/plans/2026-07-17-hdaw-electron-frontend-phase4.md
git commit -m "frontend: add electron-builder packaging for distributable exe

Phase 4 — Electron packaging:
- electron-builder.yml: NSIS installer + portable target, engine binary
  bundled as extraResource under resources/engine/
- main.ts: enginePath() resolves from process.resourcesPath when packaged
- package.json: electron-builder dep, version set to 0.9.1, package script"
```
