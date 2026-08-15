# HDAW Electron/React Frontend — Phase 1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Electron + React thin vertical slice that spawns the headless engine, renders transport + tracks + mixer + piano roll + minimal timeline, and round-trips real mutations via JSON-RPC 2.0 over WebSocket.

**Architecture:** Electron main process spawns `HDAW.exe --headless --port=8766` as a child process and opens a `BrowserWindow` loading the Vite dev server. The renderer connects via a WebSocket JSON-RPC client to the engine's `FrontendServer`. Three Zustand stores hold project state, transport state, and meter levels. Components are thin-slice implementations that prove the wire protocol and render pipeline.

**Tech Stack:** Electron 33, React 19, Vite 6, TypeScript 5.6+, Zustand 5, Canvas 2D API

---

### Task 0: Commit Phase 0 engine-side server

**Files:**
- Create (track): `src/frontend/FrontendServer.h`
- Create (track): `src/frontend/FrontendServer.cpp`
- Create (track): `src/frontend/FrontendRpc.h`
- Create (track): `src/frontend/FrontendRouter.h`
- Create (track): `src/frontend/FrontendRouter.cpp`
- Create (track): `src/frontend/FrontendTreeWatcher.h`
- Create (track): `src/frontend/FrontendTreeWatcher.cpp`
- Modify (track): `src/main.cpp`
- Modify (track): `CMakeLists.txt`
- Create (track): `tests/unit/frontend/frontend_server_test.cpp`
- Modify (track): `tests/CMakeLists.txt`

- [ ] **Step 1: Stage and commit all Phase 0 files**

These files already exist on disk (created in a previous session) but are untracked. Stage them and the CMake/modified-main changes, then commit with the Phase 0 message.

```bash
git add src/frontend/ src/main.cpp CMakeLists.txt tests/unit/frontend/ tests/CMakeLists.txt
git commit -m "frontend: add WebSocket JSON-RPC server for HTML/Electron UI

Phase 0 of the HTML frontend project (see plan-sess_568153bd-a062-484e-b6f7-1fe411456cdf.md).

- FrontendServer: QWebSocketServer on port 8766 (loopback only), 30 Hz
  push timers for meter + transport notifications
- FrontendRouter: dispatch across all 7 abstract command/service interfaces
- FrontendTreeWatcher: root-tree ValueTree::Listener with 16ms debounce
- FrontendRpc.h: method name constants + Snapshot->JSON converters
- --headless flag and --port=N override in main.cpp
- CMake: Qt6::WebSockets linked, frontend sources added
- 7 integration tests covering RPC round-trips, errors, notifications"
```

- [ ] **Step 2: Verify tests pass**

```bash
cmake --build build --config Debug
build/Debug/hdaw_tests.exe --gtest_filter=FrontendServer.*
```

Expected: All 7 tests pass (SnapshotRoundTrip, AddTrackMutation, SetTrackNameAndRead, UnknownMethodReturnsError, MissingParamReturnsError, NotificationGetsNoResponse, MutationBroadcastsTreeChanged).

---

### Task 1: Scaffold frontend directory

**Files:**
- Create: `frontend/package.json`
- Create: `frontend/vite.config.ts`
- Create: `frontend/tsconfig.json`
- Create: `frontend/tsconfig.node.json`
- Create: `frontend/index.html`

- [ ] **Step 1: Create `frontend/package.json`**

```json
{
  "name": "hdaw-frontend",
  "version": "0.1.0",
  "private": true,
  "main": "dist-electron/main.js",
  "scripts": {
    "dev": "vite",
    "electron:dev": "concurrently \"vite\" \"wait-on http://localhost:5173 && electron .\"",
    "build": "tsc && vite build",
    "preview": "vite preview"
  },
  "dependencies": {
    "react": "^19.0.0",
    "react-dom": "^19.0.0",
    "zustand": "^5.0.0"
  },
  "devDependencies": {
    "@types/react": "^19.0.0",
    "@types/react-dom": "^19.0.0",
    "@vitejs/plugin-react": "^4.4.0",
    "concurrently": "^9.1.0",
    "electron": "^33.0.0",
    "typescript": "^5.6.0",
    "vite": "^6.0.0",
    "wait-on": "^8.0.0"
  }
}
```

- [ ] **Step 2: Create `frontend/vite.config.ts`**

```typescript
import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

export default defineConfig({
  plugins: [react()],
  base: "./",
  server: {
    port: 5173,
    strictPort: true,
  },
  build: {
    outDir: "dist",
  },
});
```

- [ ] **Step 3: Create `frontend/tsconfig.json`**

```json
{
  "compilerOptions": {
    "target": "ES2020",
    "useDefineForClassFields": true,
    "lib": ["ES2020", "DOM", "DOM.Iterable"],
    "module": "ESNext",
    "skipLibCheck": true,
    "moduleResolution": "bundler",
    "allowImportingTsExtensions": true,
    "isolatedModules": true,
    "moduleDetection": "force",
    "noEmit": true,
    "jsx": "react-jsx",
    "strict": true,
    "noUnusedLocals": false,
    "noUnusedParameters": false,
    "noFallthroughCasesInSwitch": true,
    "forceConsistentCasingInFileNames": true
  },
  "include": ["src"]
}
```

- [ ] **Step 4: Create `frontend/tsconfig.node.json`**

```json
{
  "compilerOptions": {
    "target": "ES2020",
    "module": "ESNext",
    "moduleResolution": "bundler",
    "allowSyntheticDefaultImports": true,
    "esModuleInterop": true,
    "outDir": "dist-electron",
    "skipLibCheck": true
  },
  "include": ["electron"]
}
```

- [ ] **Step 5: Create `frontend/index.html`**

```html
<!DOCTYPE html>
<html lang="en">
  <head>
    <meta charset="UTF-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1.0" />
    <title>HDAW</title>
  </head>
  <body>
    <div id="root"></div>
    <script type="module" src="/src/main.tsx"></script>
  </body>
</html>
```

- [ ] **Step 6: Install dependencies**

```bash
cd frontend
npm install
```

Expected: `node_modules/` populated, `package-lock.json` created.

- [ ] **Step 7: Commit**

```bash
git add frontend/package.json frontend/vite.config.ts frontend/tsconfig.json frontend/tsconfig.node.json frontend/index.html
git commit -m "frontend: scaffold Electron + Vite + React project"
```

---

### Task 2: Electron shell

**Files:**
- Create: `frontend/electron/main.ts`
- Create: `frontend/electron/preload.ts`

The main process:
1. Reads `--port` from `process.argv` or defaults to `8766`
2. Spawns `../../build/Debug/HDAW.exe --headless --port=<port>` (relative to `frontend/`)
3. Polls `ws://127.0.0.1:<port>` until the engine accepts connections
4. Creates a `BrowserWindow` that loads `http://localhost:5173` (dev)
5. On child exit (non-zero or signal), shows a restart dialog
6. On `app.quit()`, kills the child process

- [ ] **Step 1: Create `frontend/electron/main.ts`**

```typescript
import { app, BrowserWindow, dialog } from "electron";
import { ChildProcess, spawn } from "child_process";
import * as path from "path";
import * as net from "net";

const DEFAULT_PORT = 8766;

let childProcess: ChildProcess | null = null;
let mainWindow: BrowserWindow | null = null;

function getPort(): number {
  const idx = process.argv.indexOf("--port");
  if (idx >= 0 && idx + 1 < process.argv.length) {
    const p = parseInt(process.argv[idx + 1], 10);
    if (!isNaN(p)) return p;
  }
  return DEFAULT_PORT;
}

function waitForPort(port: number, timeoutMs = 8000): Promise<void> {
  const start = Date.now();
  return new Promise((resolve, reject) => {
    const tryConnect = () => {
      const sock = new net.Socket();
      sock.once("connect", () => {
        sock.destroy();
        resolve();
      });
      sock.once("error", () => {
        sock.destroy();
        if (Date.now() - start > timeoutMs) {
          reject(new Error(`Timed out waiting for port ${port}`));
        } else {
          setTimeout(tryConnect, 200);
        }
      });
      sock.connect(port, "127.0.0.1");
    };
    tryConnect();
  });
}

function spawnEngine(port: number): ChildProcess {
  const enginePath = path.resolve(__dirname, "..", "..", "..", "build", "Debug", "HDAW.exe");
  const proc = spawn(enginePath, ["--headless", `--port=${port}`], {
    stdio: ["ignore", "pipe", "pipe"],
  });
  proc.stdout?.on("data", (data) => console.log(`[engine] ${data}`));
  proc.stderr?.on("data", (data) => console.error(`[engine] ${data}`));
  proc.on("exit", (code, signal) => {
    console.log(`[engine] exited code=${code} signal=${signal}`);
    if (mainWindow) {
      dialog.showMessageBox(mainWindow, {
        type: "error",
        title: "Engine Crashed",
        message: "The audio engine has stopped unexpectedly.",
        detail: `Exit code: ${code}${signal ? ` Signal: ${signal}` : ""}`,
        buttons: ["Restart", "Quit"],
        defaultId: 0,
      }).then(({ response }) => {
        if (response === 0) {
          childProcess = spawnEngine(port);
          waitForPort(port).then(() => {
            mainWindow?.webContents.reload();
          }).catch((err) => {
            console.error("Failed to restart engine:", err);
            app.quit();
          });
        } else {
          app.quit();
        }
      });
    }
  });
  return proc;
}

async function createWindow() {
  mainWindow = new BrowserWindow({
    width: 1400,
    height: 900,
    backgroundColor: "#141416",
    webPreferences: {
      preload: path.join(__dirname, "preload.js"),
      contextIsolation: true,
      nodeIntegration: false,
    },
  });
  if (process.env.NODE_ENV === "development" || !app.isPackaged) {
    await mainWindow.loadURL("http://localhost:5173");
    mainWindow.webContents.openDevTools();
  } else {
    await mainWindow.loadFile(path.join(__dirname, "..", "dist", "index.html"));
  }
}

app.whenReady().then(async () => {
  const port = getPort();
  childProcess = spawnEngine(port);
  try {
    await waitForPort(port);
    console.log("[main] Engine ready on port", port);
  } catch (err) {
    dialog.showErrorBox("Engine Startup Failed", String(err));
    app.quit();
    return;
  }
  await createWindow();
});

app.on("window-all-closed", () => {
  if (childProcess) {
    childProcess.kill();
    childProcess = null;
  }
  app.quit();
});

app.on("before-quit", () => {
  if (childProcess) {
    childProcess.kill();
    childProcess = null;
  }
});
```

- [ ] **Step 2: Create `frontend/electron/preload.ts`**

```typescript
import { contextBridge, ipcRenderer } from "electron";

contextBridge.exposeInMainWorld("hdaw", {
  invoke: (channel: string, ...args: unknown[]) =>
    ipcRenderer.invoke(channel, ...args),
});
```

- [ ] **Step 3: Add `"type": "module"` to package.json if not already present (Electron main uses ESM in dev)**

Edit `frontend/package.json` to add the type field. Since Vite handles the renderer ESM and we compile electron/ with tsc, we use CommonJS for electron. Add a build step for electron in package.json scripts.

Actually, the simpler approach for electron is to compile with `tsc` using `tsconfig.node.json` to output CommonJS to `dist-electron/`. Let me update the scripts in package.json.

Read the current package.json first (from Task 1) and update:

```bash
cd frontend && npm pkg set scripts.electron:dev="concurrently \"vite\" \"wait-on http://localhost:5173 && tsc -p tsconfig.node.json && electron .\""
npm pkg set scripts.electron:build="tsc -p tsconfig.node.json"
```

Or simply update package.json manually. Let me just ensure the electron script is:

```json
"electron:dev": "concurrently -k \"vite\" \"wait-on http://localhost:5173 && tsc -p tsconfig.node.json && electron .\""
```

The `-k` flag on concurrently kills the other process when one exits.

- [ ] **Step 4: Add electron `"main"` field to package.json pointing to the compiled output**

The `package.json` `"main"` should be `"dist-electron/main.js"` (already set in Task 1).

- [ ] **Step 5: Verify Electron compiles**

```bash
cd frontend
npx tsc -p tsconfig.node.json --noEmit
```

Expected: No type errors. (Compilation to JS happens at runtime via `tsc -p tsconfig.node.json` before launching electron.)

- [ ] **Step 6: Commit**

```bash
git add frontend/electron/main.ts frontend/electron/preload.ts
git commit -m "frontend: add Electron main process with engine spawning"
```

---

### Task 3: RPC WebSocket client

**Files:**
- Create: `frontend/src/rpc/client.ts`

The RPC client manages a single WebSocket connection to `ws://127.0.0.1:<port>`. It provides a `call(method, params)` that returns a `Promise` resolved with the result, and a notification subscription API.

- [ ] **Step 1: Create `frontend/src/rpc/client.ts`**

```typescript
type JsonValue = string | number | boolean | null | JsonValue[] | { [key: string]: JsonValue };

interface JsonRpcRequest {
  jsonrpc: "2.0";
  id: number;
  method: string;
  params?: unknown;
}

interface JsonRpcResponse {
  jsonrpc: "2.0";
  id: number;
  result?: unknown;
  error?: { code: number; message: string };
}

interface JsonRpcNotification {
  jsonrpc: "2.0";
  method: string;
  params?: unknown;
}

type NotificationHandler = (method: string, params: unknown) => void;

class RpcError extends Error {
  constructor(public code: number, message: string) {
    super(message);
    this.name = "RpcError";
  }
}

export class RpcClient {
  private ws: WebSocket | null = null;
  private nextId = 1;
  private pending = new Map<number, { resolve: (v: unknown) => void; reject: (e: Error) => void }>();
  private handlers = new Map<string, Set<NotificationHandler>>();
  private reconnectTimer: ReturnType<typeof setTimeout> | null = null;
  private destroyed = false;

  constructor(private port: number) {}

  connect(): Promise<void> {
    return new Promise((resolve, reject) => {
      if (this.ws?.readyState === WebSocket.OPEN) return resolve();

      const url = `ws://127.0.0.1:${this.port}`;
      this.ws = new WebSocket(url);

      this.ws.onopen = () => resolve();

      this.ws.onmessage = (event) => {
        let msg: unknown;
        try {
          msg = JSON.parse(event.data as string);
        } catch {
          return;
        }
        const obj = msg as Record<string, unknown>;
        if (obj && typeof obj === "object" && "method" in obj && typeof obj.method === "string") {
          // Notification
          this.dispatchNotification(obj.method, obj.params);
        } else if (obj && typeof obj === "object" && "id" in obj) {
          // Response
          const resp = obj as unknown as JsonRpcResponse;
          const pd = this.pending.get(resp.id);
          if (pd) {
            this.pending.delete(resp.id);
            if (resp.error) {
              pd.reject(new RpcError(resp.error.code, resp.error.message));
            } else {
              pd.resolve(resp.result);
            }
          }
        }
      };

      this.ws.onerror = () => {
        reject(new Error("WebSocket connection failed"));
      };

      this.ws.onclose = () => {
        if (!this.destroyed) {
          this.scheduleReconnect();
        }
      };
    });
  }

  private scheduleReconnect() {
    if (this.reconnectTimer) return;
    this.reconnectTimer = setTimeout(() => {
      this.reconnectTimer = null;
      if (this.destroyed) return;
      this.connect().catch(() => this.scheduleReconnect());
    }, 1000);
  }

  disconnect() {
    this.destroyed = true;
    if (this.reconnectTimer) {
      clearTimeout(this.reconnectTimer);
      this.reconnectTimer = null;
    }
    if (this.ws) {
      this.ws.onclose = null;
      this.ws.close();
      this.ws = null;
    }
    // Reject all pending
    for (const [, pd] of this.pending) {
      pd.reject(new Error("disconnected"));
    }
    this.pending.clear();
  }

  async call(method: string, params?: unknown): Promise<unknown> {
    if (!this.ws || this.ws.readyState !== WebSocket.OPEN) {
      await this.connect();
    }
    const id = this.nextId++;
    const req: JsonRpcRequest = { jsonrpc: "2.0", id, method };
    if (params !== undefined) req.params = params;

    return new Promise((resolve, reject) => {
      this.pending.set(id, { resolve, reject });
      this.ws!.send(JSON.stringify(req));
    });
  }

  onNotification(method: string, handler: NotificationHandler): () => void {
    if (!this.handlers.has(method)) {
      this.handlers.set(method, new Set());
    }
    this.handlers.get(method)!.add(handler);
    return () => {
      this.handlers.get(method)?.delete(handler);
    };
  }

  private dispatchNotification(method: string, params: unknown) {
    const handlers = this.handlers.get(method);
    if (handlers) {
      for (const h of handlers) {
        try { h(method, params); } catch { /* ignore */ }
      }
    }
  }
}
```

- [ ] **Step 2: Define TypeScript types for the snapshot JSON shapes**

Create `frontend/src/rpc/types.ts` with interfaces matching the `FrontendRpc.h` JSON converter outputs.

```typescript
export interface TransportSnapshot {
  bpm: number;
  isPlaying: boolean;
  isLooping: boolean;
  isRecording: boolean;
  loopStart: number;
  loopEnd: number;
  currentTimeSeconds: number;
  sampleRate: number;
}

export interface TrackSnapshot {
  index: number;
  name: string;
  color: number;
  volume: number;
  pan: number;
  muted: boolean;
  soloed: boolean;
  armed: boolean;
  inputMonitor: boolean;
  height: number;
  midiChannel: number;
  clipCount: number;
}

export interface GainEnvelopePoint {
  time: number;
  gain: number;
}

export interface ClipSnapshot {
  clipId: number;
  trackIndex: number;
  name: string;
  sourceFile: string;
  startBeat: number;
  durationBeats: number;
  offset: number;
  gain: number;
  fadeIn: number;
  fadeOut: number;
  looping: boolean;
  isMidi: boolean;
  sourceBpm: number;
  stretchMode: number;
  stretchRatio: number;
  sourceDuration: number;
  gainEnvelope: GainEnvelopePoint[];
}

export interface NoteSnapshot {
  noteId: number;
  pitch: number;
  velocity: number;
  startBeat: number;
  durationBeats: number;
}

export interface ProjectSnapshot {
  name: string;
  transport: TransportSnapshot;
  tracks: TrackSnapshot[];
  clips: ClipSnapshot[];
  scaleRoot: number;
  scaleMode: number;
}

export interface MeterLevels {
  l: number;
  r: number;
}

export interface MetersPayload {
  master: MeterLevels;
  tracks: MeterLevels[];
}
```

- [ ] **Step 3: Commit**

```bash
git add frontend/src/rpc/client.ts frontend/src/rpc/types.ts
git commit -m "frontend: add WebSocket JSON-RPC client and type definitions"
```

---

### Task 4: Theme and Zustand stores

**Files:**
- Create: `frontend/src/theme.ts`
- Create: `frontend/src/store/projectStore.ts`
- Create: `frontend/src/store/transportStore.ts`
- Create: `frontend/src/store/meterStore.ts`

- [ ] **Step 1: Create `frontend/src/theme.ts`**

Port the colors from `src/ui/Theme.h` to CSS custom properties applied on `:root`.

```typescript
// Port of src/ui/Theme.h to CSS custom properties.
// Color values are in #rrggbb or rgba() format.
export const theme = {
  bgWindow: "#141416",
  bgPanel: "#1e1e22",
  bgHeader: "#1e1e22",
  bgWidget: "#2a2a2e",
  bgInput: "#2a2a2e",
  bgElevated: "#323236",
  bgToolbar: "rgba(20, 20, 22, 0.9)",

  border: "#2a2a2e",
  borderLight: "#3a3a40",

  textPrimary: "#e8e8ec",
  textSecondary: "#a8a8b0",
  textMuted: "#787880",

  accent: "#d97706",
  accentDim: "#b45309",
  accentBright: "#f59e0b",

  danger: "#ef4444",
  warning: "#eab308",
  success: "#10b981",
  info: "#38b2df",

  vuGreen: "#10b981",
  vuYellow: "#f59e0b",
  vuRed: "#ef4444",

  trackFill1: "#28282c",
  trackFill2: "#2c2c30",
  trackColor: "rgba(217, 119, 6, 0.16)",
  rulerBg: "#222226",

  automationFill: "rgba(217, 119, 6, 0.16)",
  automationLine: "rgba(217, 119, 6, 0.78)",

  scrollbarBg: "#1e1e22",
  scrollbarHandle: "#3a3a40",
  scrollbarHover: "#d97706",

  gridLineBar: "rgba(255, 255, 255, 0.07)",
  gridLineBeat: "rgba(255, 255, 255, 0.03)",
  gridLineSub: "rgba(255, 255, 255, 0.016)",
  placeholderText: "rgba(255, 255, 255, 0.31)",
} as const;

export function injectTheme() {
  const root = document.documentElement;
  for (const [key, value] of Object.entries(theme)) {
    const cssName = "--" + key.replace(/([A-Z])/g, "-$1").toLowerCase();
    root.style.setProperty(cssName, value);
  }
}
```

- [ ] **Step 2: Create `frontend/src/store/projectStore.ts`**

On `notify.treeChanged`, re-fetches `read.snapshot()` and replaces the entire project state.

```typescript
import { create } from "zustand";
import { RpcClient } from "../rpc/client";
import { ProjectSnapshot, TrackSnapshot, ClipSnapshot, NoteSnapshot } from "../rpc/types";

interface ProjectState {
  snapshot: ProjectSnapshot | null;
  notesByClip: Map<number, NoteSnapshot[]>;
  lastSync: number;

  // Actions
  syncSnapshot: (rpc: RpcClient) => Promise<void>;
  syncNotes: (rpc: RpcClient, clipId: number) => Promise<void>;
  getTrack: (index: number) => TrackSnapshot | undefined;
  getClip: (clipId: number) => ClipSnapshot | undefined;
}

export const useProjectStore = create<ProjectState>((set, get) => ({
  snapshot: null,
  notesByClip: new Map(),
  lastSync: 0,

  syncSnapshot: async (rpc: RpcClient) => {
    const result = await rpc.call("read.snapshot") as ProjectSnapshot;
    set({ snapshot: result, lastSync: Date.now() });
  },

  syncNotes: async (rpc: RpcClient, clipId: number) => {
    const result = await rpc.call("read.getNotes", { clipId }) as NoteSnapshot[];
    const notesByClip = new Map(get().notesByClip);
    notesByClip.set(clipId, result);
    set({ notesByClip });
  },

  getTrack: (index: number) => {
    return get().snapshot?.tracks.find((t) => t.index === index);
  },

  getClip: (clipId: number) => {
    return get().snapshot?.clips.find((c) => c.clipId === clipId);
  },
}));
```

- [ ] **Step 3: Create `frontend/src/store/transportStore.ts`**

Updated by `notify.transport` at 30 Hz.

```typescript
import { create } from "zustand";
import { TransportSnapshot } from "../rpc/types";

interface TransportState {
  transport: TransportSnapshot;
  update: (data: TransportSnapshot) => void;
}

export const useTransportStore = create<TransportState>((set) => ({
  transport: {
    bpm: 120,
    isPlaying: false,
    isLooping: false,
    isRecording: false,
    loopStart: 0,
    loopEnd: 8,
    currentTimeSeconds: 0,
    sampleRate: 0,
  },
  update: (data) => set({ transport: data }),
}));
```

- [ ] **Step 4: Create `frontend/src/store/meterStore.ts`**

Updated by `notify.meters` at 30 Hz.

```typescript
import { create } from "zustand";
import { MetersPayload, MeterLevels } from "../rpc/types";

interface MeterState {
  master: MeterLevels;
  tracks: MeterLevels[];
  update: (data: MetersPayload) => void;
}

export const useMeterStore = create<MeterState>((set) => ({
  master: { l: 0, r: 0 },
  tracks: [],
  update: (data) => set({ master: data.master, tracks: data.tracks }),
}));
```

- [ ] **Step 5: Commit**

```bash
git add frontend/src/theme.ts frontend/src/store/projectStore.ts frontend/src/store/transportStore.ts frontend/src/store/meterStore.ts
git commit -m "frontend: add theme, project/transport/meter Zustand stores"
```

---

### Task 5: App layout + RPC wiring

**Files:**
- Create: `frontend/src/main.tsx`
- Create: `frontend/src/App.tsx`
- Create: `frontend/src/App.css`
- Modify: `frontend/index.html` (verify path)

- [ ] **Step 1: Create `frontend/src/main.tsx`**

```tsx
import React from "react";
import ReactDOM from "react-dom/client";
import App from "./App";
import { injectTheme } from "./theme";

injectTheme();

ReactDOM.createRoot(document.getElementById("root")!).render(
  <React.StrictMode>
    <App />
  </React.StrictMode>
);
```

- [ ] **Step 2: Create `frontend/src/App.css`**

The CSS uses the custom properties injected by `theme.ts`. This is a dark DAW layout: menu/transport bar at the top, a left-right splitter for track headers + main area, a timeline/piano-roll area, and a bottom status bar.

```css
* {
  margin: 0;
  padding: 0;
  box-sizing: border-box;
}

html, body, #root {
  height: 100%;
  width: 100%;
  overflow: hidden;
  font-family: "Segoe UI", "Arial", sans-serif;
  font-size: 11px;
  color: var(--text-primary);
  background: var(--bg-window);
}

.app-layout {
  display: flex;
  flex-direction: column;
  height: 100%;
}

.main-area {
  display: flex;
  flex: 1;
  overflow: hidden;
}

.track-headers-panel {
  width: 200px;
  min-width: 120px;
  background: var(--bg-panel);
  border-right: 1px solid var(--border);
  overflow-y: auto;
}

.content-area {
  flex: 1;
  display: flex;
  flex-direction: column;
  overflow: hidden;
}

.timeline-area {
  flex: 1;
  position: relative;
  overflow: auto;
  background: var(--bg-window);
}

.bottom-area {
  display: flex;
  flex-direction: row;
  height: 240px;
  min-height: 120px;
  border-top: 1px solid var(--border);
}

.piano-roll-panel {
  flex: 1;
  display: flex;
  flex-direction: column;
  background: var(--bg-panel);
}

.mixer-panel {
  width: 280px;
  min-width: 200px;
  border-left: 1px solid var(--border);
  background: var(--bg-panel);
  overflow-y: auto;
}

.status-bar {
  height: 22px;
  background: #0e0e10;
  color: var(--text-secondary);
  border-top: 1px solid var(--border);
  display: flex;
  align-items: center;
  padding: 0 8px;
  font-size: 10px;
  gap: 16px;
}

.app-loading {
  display: flex;
  align-items: center;
  justify-content: center;
  height: 100%;
  color: var(--text-muted);
  font-size: 13px;
}
```

- [ ] **Step 3: Create `frontend/src/App.tsx`**

The root component. On mount, creates the `RpcClient`, connects, subscribes to push notifications, and syncs the initial snapshot. Passes the RPC client and stores to child components via props (no global RPC singleton — keeps testability clean).

```tsx
import { useEffect, useRef, useState } from "react";
import { RpcClient } from "./rpc/client";
import { useProjectStore } from "./store/projectStore";
import { useTransportStore } from "./store/transportStore";
import { useMeterStore } from "./store/meterStore";
import { TransportBar } from "./components/TransportBar";
import { TrackHeaders } from "./components/TrackHeaders";
import { TimelineMinimal } from "./components/TimelineMinimal";
import { Mixer } from "./components/Mixer";
import { PianoRoll } from "./components/PianoRoll";
import { TransportSnapshot, MetersPayload, ProjectSnapshot } from "./rpc/types";
import "./App.css";

function App() {
  const rpcRef = useRef<RpcClient | null>(null);
  const [connected, setConnected] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const syncSnapshot = useProjectStore((s) => s.syncSnapshot);
  const updateTransport = useTransportStore((s) => s.update);
  const updateMeters = useMeterStore((s) => s.update);
  const snapshot = useProjectStore((s) => s.snapshot);
  const lastSync = useProjectStore((s) => s.lastSync);

  useEffect(() => {
    const port = 8766;
    const rpc = new RpcClient(port);
    rpcRef.current = rpc;

    rpc.connect()
      .then(() => {
        setConnected(true);
        // Subscribe to push notifications
        rpc.onNotification("notify.transport", (_, params) => {
          updateTransport(params as TransportSnapshot);
        });
        rpc.onNotification("notify.meters", (_, params) => {
          updateMeters(params as MetersPayload);
        });
        rpc.onNotification("notify.treeChanged", () => {
          syncSnapshot(rpc);
        });
        // Initial snapshot sync
        return syncSnapshot(rpc);
      })
      .then(() => {
        setError(null);
      })
      .catch((err: Error) => {
        setError(err.message);
      });

    return () => {
      rpc.disconnect();
      rpcRef.current = null;
    };
  }, []); // eslint-disable-line react-hooks/exhaustive-deps

  if (error) {
    return (
      <div className="app-layout">
        <div className="app-loading">
          <p>Failed to connect to engine: {error}</p>
        </div>
      </div>
    );
  }

  if (!connected || !snapshot) {
    return (
      <div className="app-layout">
        <div className="app-loading">
          <p>Connecting to engine...</p>
        </div>
      </div>
    );
  }

  return (
    <div className="app-layout">
      <TransportBar rpc={rpcRef.current!} />
      <div className="main-area">
        <div className="track-headers-panel">
          <TrackHeaders rpc={rpcRef.current!} />
        </div>
        <div className="content-area">
          <div className="timeline-area">
            <TimelineMinimal />
          </div>
          <div className="bottom-area">
            <div className="piano-roll-panel">
              <PianoRoll rpc={rpcRef.current!} />
            </div>
            <div className="mixer-panel">
              <Mixer rpc={rpcRef.current!} />
            </div>
          </div>
        </div>
      </div>
      <div className="status-bar">
        <span>{snapshot.transport.bpm.toFixed(1)} BPM</span>
        <span>{(snapshot.transport.sampleRate / 1000).toFixed(1)} kHz</span>
        <span style={{ flex: 1 }} />
        <span>{snapshot.name}</span>
      </div>
    </div>
  );
}

export default App;
```

- [ ] **Step 4: Build-check the TypeScript**

```bash
cd frontend
npx tsc --noEmit
```

Expected: No type errors. (If there are, fix missing imports or type mismatches.)

- [ ] **Step 5: Commit**

```bash
git add frontend/src/main.tsx frontend/src/App.tsx frontend/src/App.css
git commit -m "frontend: add App layout with RPC wiring and shell"
```

---

### Task 6: TransportBar component

**Files:**
- Create: `frontend/src/components/TransportBar.tsx`

- [ ] **Step 1: Create `frontend/src/components/TransportBar.tsx`**

Buttons for Play, Stop, Rewind, Record, Loop toggle, and a timecode display driven by `transportStore`. All mutations go through the RPC client.

```tsx
import { useCallback } from "react";
import { RpcClient } from "../rpc/client";
import { useTransportStore } from "../store/transportStore";

interface Props {
  rpc: RpcClient;
}

function formatTime(seconds: number): string {
  const m = Math.floor(seconds / 60);
  const s = seconds % 60;
  return `${m}:${s.toFixed(2).padStart(5, "0")}`;
}

export function TransportBar({ rpc }: Props) {
  const transport = useTransportStore((s) => s.transport);

  const play = useCallback(() => rpc.call("transport.play"), [rpc]);
  const stop = useCallback(() => rpc.call("transport.stop"), [rpc]);
  const rewind = useCallback(() => rpc.call("transport.rewind"), [rpc]);
  const toggleRecord = useCallback(() => {
    if (transport.isRecording) {
      rpc.call("transport.stopRecording");
    } else {
      rpc.call("transport.startRecording");
    }
  }, [rpc, transport.isRecording]);
  const toggleLoop = useCallback(() => rpc.call("transport.toggleLoop"), [rpc]);

  const btnStyle: React.CSSProperties = {
    background: "var(--bg-widget)",
    color: "var(--text-primary)",
    border: "1px solid var(--border-light)",
    borderRadius: 4,
    padding: "4px 12px",
    cursor: "pointer",
    fontSize: 11,
  };

  const activeBtnStyle: React.CSSProperties = {
    ...btnStyle,
    background: "#3d2a14",
    color: "#f5d9b0",
    borderColor: "var(--accent)",
  };

  return (
    <div style={{
      display: "flex",
      alignItems: "center",
      gap: 6,
      padding: "4px 8px",
      background: "var(--bg-panel)",
      borderBottom: "1px solid var(--border)",
    }}>
      <button style={btnStyle} onClick={play} disabled={transport.isPlaying}>
        ▶
      </button>
      <button style={btnStyle} onClick={stop} disabled={!transport.isPlaying}>
        ■
      </button>
      <button style={btnStyle} onClick={rewind}>
        ⏮
      </button>
      <button
        style={transport.isRecording ? activeBtnStyle : btnStyle}
        onClick={toggleRecord}
      >
        ●
      </button>
      <button
        style={transport.isLooping ? activeBtnStyle : btnStyle}
        onClick={toggleLoop}
      >
        ↻
      </button>

      <div style={{ marginLeft: 12, fontFamily: "monospace", fontSize: 13 }}>
        {formatTime(transport.currentTimeSeconds)}
      </div>

      <div style={{ marginLeft: 12, color: "var(--text-secondary)" }}>
        {transport.bpm.toFixed(1)} BPM
      </div>
    </div>
  );
}
```

- [ ] **Step 2: Commit**

```bash
git add frontend/src/components/TransportBar.tsx
git commit -m "frontend: add TransportBar with play/stop/rewind/record/loop/timecode"
```

---

### Task 7: TrackHeaders component

**Files:**
- Create: `frontend/src/components/TrackHeaders.tsx`

Renders the track name, volume, pan, mute, and solo controls for each track. Reads from `projectStore`.

- [ ] **Step 1: Create `frontend/src/components/TrackHeaders.tsx`**

```tsx
import { useCallback } from "react";
import { RpcClient } from "../rpc/client";
import { useProjectStore } from "../store/projectStore";

interface Props {
  rpc: RpcClient;
}

export function TrackHeaders({ rpc }: Props) {
  const tracks = useProjectStore((s) => s.snapshot?.tracks ?? []);

  const setName = useCallback(
    (index: number, name: string) => rpc.call("project.setTrackName", { trackIndex: index, name }),
    [rpc]
  );
  const setVolume = useCallback(
    (index: number, volume: number) => rpc.call("project.setTrackVolume", { trackIndex: index, volume }),
    [rpc]
  );
  const setPan = useCallback(
    (index: number, pan: number) => rpc.call("project.setTrackPan", { trackIndex: index, pan }),
    [rpc]
  );
  const toggleMute = useCallback(
    (index: number, muted: boolean) => rpc.call("project.setTrackMuted", { trackIndex: index, muted }),
    [rpc]
  );
  const toggleSolo = useCallback(
    (index: number, soloed: boolean) => rpc.call("project.setTrackSoloed", { trackIndex: index, soloed }),
    [rpc]
  );

  const labelStyle: React.CSSProperties = {
    fontSize: 10,
    color: "var(--text-secondary)",
    marginBottom: 2,
  };

  const rowStyle = (i: number): React.CSSProperties => ({
    padding: "4px 6px",
    borderBottom: "1px solid var(--border)",
    background: i % 2 === 0 ? "var(--track-fill-1)" : "var(--track-fill-2)",
  });

  return (
    <div>
      {tracks.map((track, i) => (
        <div key={track.index} style={rowStyle(i)}>
          <input
            defaultValue={track.name}
            onBlur={(e) => setName(track.index, e.target.value)}
            style={{
              width: "100%",
              background: "var(--bg-input)",
              color: "var(--text-primary)",
              border: "1px solid var(--border-light)",
              borderRadius: 2,
              padding: "2px 4px",
              fontSize: 11,
              marginBottom: 4,
            }}
          />
          <div style={labelStyle}>Vol</div>
          <input
            type="range"
            min={0}
            max={2}
            step={0.01}
            defaultValue={track.volume}
            onChange={(e) => setVolume(track.index, parseFloat(e.target.value))}
            style={{ width: "100%", height: 14, marginBottom: 4 }}
          />
          <div style={labelStyle}>Pan</div>
          <input
            type="range"
            min={-1}
            max={1}
            step={0.01}
            defaultValue={track.pan}
            onChange={(e) => setPan(track.index, parseFloat(e.target.value))}
            style={{ width: "100%", height: 14, marginBottom: 4 }}
          />
          <div style={{ display: "flex", gap: 4 }}>
            <button
              style={{
                flex: 1,
                background: track.muted ? "var(--accent-dim)" : "var(--bg-widget)",
                color: "var(--text-primary)",
                border: "1px solid var(--border-light)",
                borderRadius: 2,
                padding: "2px 0",
                fontSize: 10,
                cursor: "pointer",
              }}
              onClick={() => toggleMute(track.index, !track.muted)}
            >
              M
            </button>
            <button
              style={{
                flex: 1,
                background: track.soloed ? "var(--accent-dim)" : "var(--bg-widget)",
                color: "var(--text-primary)",
                border: "1px solid var(--border-light)",
                borderRadius: 2,
                padding: "2px 0",
                fontSize: 10,
                cursor: "pointer",
              }}
              onClick={() => toggleSolo(track.index, !track.soloed)}
            >
              S
            </button>
          </div>
        </div>
      ))}
    </div>
  );
}
```

- [ ] **Step 2: Commit**

```bash
git add frontend/src/components/TrackHeaders.tsx
git commit -m "frontend: add TrackHeaders with name/volume/pan/mute/solo controls"
```

---

### Task 8: Mixer + MixerStrip components

**Files:**
- Create: `frontend/src/components/Mixer.tsx`
- Create: `frontend/src/components/MixerStrip.tsx`

- [ ] **Step 1: Create `frontend/src/components/MixerStrip.tsx`**

A single mixer strip with a vertical fader, pan knob, mute/solo buttons, and VU bars bound to `meterStore`.

```tsx
import { useCallback, useMemo } from "react";
import { RpcClient } from "../rpc/client";
import { useMeterStore } from "../store/meterStore";
import { useProjectStore } from "../store/projectStore";
import { TrackSnapshot } from "../rpc/types";

interface Props {
  rpc: RpcClient;
  track: TrackSnapshot;
}

export function MixerStrip({ rpc, track }: Props) {
  const meter = useMeterStore((s) => s.tracks[track.index]);
  const vuL = meter?.l ?? 0;
  const vuR = meter?.r ?? 0;

  const setVolume = useCallback(
    (volume: number) => rpc.call("project.setTrackVolume", { trackIndex: track.index, volume }),
    [rpc, track.index]
  );
  const setPan = useCallback(
    (pan: number) => rpc.call("project.setTrackPan", { trackIndex: track.index, pan }),
    [rpc, track.index]
  );
  const toggleMute = useCallback(
    () => rpc.call("project.setTrackMuted", { trackIndex: track.index, muted: !track.muted }),
    [rpc, track.index]
  );

  const containerStyle: React.CSSProperties = {
    width: 60,
    minWidth: 60,
    display: "flex",
    flexDirection: "column",
    alignItems: "center",
    padding: "4px 2px",
    borderRight: "1px solid var(--border)",
    background: "var(--bg-widget)",
  };

  const vuBar = (level: number): React.CSSProperties => ({
    width: 10,
    height: Math.min(level * 80, 80),
    background: level > 0.9 ? "var(--vu-red)" : level > 0.7 ? "var(--vu-yellow)" : "var(--vu-green)",
    borderRadius: "1px 1px 0 0",
    transition: "height 0.03s linear",
  });

  return (
    <div style={containerStyle}>
      <div style={{ fontSize: 9, color: "var(--text-secondary)", marginBottom: 2, textAlign: "center", wordBreak: "break-all", maxWidth: 56 }}>
        {track.name}
      </div>
      <div style={{ display: "flex", gap: 2, height: 90, alignItems: "flex-end", marginBottom: 4 }}>
        <div style={vuBar(vuL)} />
        <div style={vuBar(vuR)} />
      </div>
      <input
        type="range"
        orient="vertical"
        min={0}
        max={2}
        step={0.01}
        defaultValue={track.volume}
        onChange={(e) => setVolume(parseFloat(e.target.value))}
        style={{ width: 50, height: 60, writingMode: "vertical-lr", direction: "rtl" }}
      />
      <div style={{ fontSize: 9, margin: "2px 0", color: "var(--text-muted)" }}>
        {track.volume.toFixed(2)}
      </div>
      <input
        type="range"
        min={-1}
        max={1}
        step={0.01}
        defaultValue={track.pan}
        onChange={(e) => setPan(parseFloat(e.target.value))}
        style={{ width: 50, height: 14 }}
      />
      <button
        style={{
          width: 28,
          marginTop: 2,
          background: track.muted ? "var(--accent-dim)" : "var(--bg-widget)",
          color: "var(--text-primary)",
          border: "1px solid var(--border-light)",
          borderRadius: 2,
          fontSize: 9,
          cursor: "pointer",
        }}
        onClick={toggleMute}
      >
        M
      </button>
    </div>
  );
}
```

- [ ] **Step 2: Create `frontend/src/components/Mixer.tsx`**

Renders a `MixerStrip` for each track plus the master strip.

```tsx
import { RpcClient } from "../rpc/client";
import { MixerStrip } from "./MixerStrip";
import { useProjectStore } from "../store/projectStore";
import { useMeterStore } from "../store/meterStore";

interface Props {
  rpc: RpcClient;
}

export function Mixer({ rpc }: Props) {
  const tracks = useProjectStore((s) => s.snapshot?.tracks ?? []);
  const masterMeter = useMeterStore((s) => s.master);

  return (
    <div style={{ display: "flex", flexDirection: "column", height: "100%" }}>
      <div style={{ flex: 1, overflowY: "auto", display: "flex", flexWrap: "wrap", alignContent: "flex-start" }}>
        {tracks.map((t) => (
          <MixerStrip key={t.index} rpc={rpc} track={t} />
        ))}
      </div>
      <div style={{
        borderTop: "1px solid var(--border)",
        padding: "4px 6px",
        display: "flex",
        alignItems: "center",
        gap: 6,
        background: "var(--bg-elevated)",
      }}>
        <span style={{ fontSize: 10, fontWeight: 600 }}>Master</span>
        <div style={{ display: "flex", gap: 2, alignItems: "flex-end", height: 30 }}>
          <div style={{
            width: 8,
            height: Math.min(masterMeter.l * 30, 30),
            background: masterMeter.l > 0.9 ? "var(--vu-red)" : masterMeter.l > 0.7 ? "var(--vu-yellow)" : "var(--vu-green)",
            borderRadius: "1px 1px 0 0",
          }} />
          <div style={{
            width: 8,
            height: Math.min(masterMeter.r * 30, 30),
            background: masterMeter.r > 0.9 ? "var(--vu-red)" : masterMeter.r > 0.7 ? "var(--vu-yellow)" : "var(--vu-green)",
            borderRadius: "1px 1px 0 0",
          }} />
        </div>
      </div>
    </div>
  );
}
```

- [ ] **Step 3: Commit**

```bash
git add frontend/src/components/Mixer.tsx frontend/src/components/MixerStrip.tsx
git commit -m "frontend: add Mixer and MixerStrip with VU meters and faders"
```

---

### Task 9: PianoRoll + NoteGrid components

**Files:**
- Create: `frontend/src/components/PianoRoll.tsx`
- Create: `frontend/src/components/NoteGrid.tsx`

The piano roll is a `<canvas>`-based note editor. The left side shows piano keys (C4–B5 range). The right canvas shows note rectangles that can be clicked to select and dragged to move. Double-clicking a note deletes it. Clicking on empty space creates a new note.

- [ ] **Step 1: Create `frontend/src/components/NoteGrid.tsx`**

A canvas that renders and edits MIDI notes. Uses the `canvas` 2D API.

```tsx
import { useCallback, useEffect, useRef, useState } from "react";
import { RpcClient } from "../rpc/client";
import { useProjectStore } from "../store/projectStore";
import { NoteSnapshot } from "../rpc/types";

const NOTE_HEIGHT = 10;
const PIXELS_PER_BEAT = 40;
const MIN_NOTE_DURATION = 0.25;
const LOWEST_NOTE = 48;  // C3
const HIGHEST_NOTE = 83; // B5
const NUM_KEYS = HIGHEST_NOTE - LOWEST_NOTE + 1;
const KEY_WIDTH = 40;

interface Props {
  rpc: RpcClient;
}

export function NoteGrid({ rpc }: Props) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const [selectedClipId, setSelectedClipId] = useState<number | null>(null);
  const notes = useProjectStore((s) =>
    selectedClipId !== null ? s.notesByClip.get(selectedClipId) ?? [] : []
  );
  const clips = useProjectStore((s) =>
    s.snapshot?.clips.filter((c) => c.isMidi) ?? []
  );
  const syncNotes = useProjectStore((s) => s.syncNotes);

  // Select first MIDI clip on mount
  useEffect(() => {
    if (clips.length > 0 && selectedClipId === null) {
      const id = clips[0].clipId;
      setSelectedClipId(id);
      syncNotes(rpc, id);
    }
  }, [clips, selectedClipId, syncNotes, rpc]);

  const height = NUM_KEYS * NOTE_HEIGHT;

  // Draw canvas
  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext("2d");
    if (!ctx) return;

    const dpr = window.devicePixelRatio || 1;
    const w = canvas.clientWidth;
    const h = canvas.clientHeight;
    canvas.width = w * dpr;
    canvas.height = h * dpr;
    ctx.scale(dpr, dpr);
    ctx.clearRect(0, 0, w, h);

    // Grid lines
    ctx.strokeStyle = "rgba(255,255,255,0.04)";
    ctx.lineWidth = 1;
    for (let i = 0; i < NUM_KEYS; i++) {
      const y = i * NOTE_HEIGHT;
      ctx.strokeStyle = (LOWEST_NOTE + i) % 12 === 0
        ? "rgba(255,255,255,0.08)"
        : "rgba(255,255,255,0.04)";
      ctx.beginPath();
      ctx.moveTo(0, y + 0.5);
      ctx.lineTo(w, y + 0.5);
      ctx.stroke();
    }

    // Notes
    if (selectedClipId) {
      const clip = useProjectStore.getState().snapshot?.clips.find(
        (c) => c.clipId === selectedClipId
      );
      const offsetX = clip?.startBeat ?? 0;

      for (const note of notes) {
        const x = (note.startBeat - offsetX) * PIXELS_PER_BEAT;
        const y = (HIGHEST_NOTE - note.pitch) * NOTE_HEIGHT;
        const nw = Math.max(note.durationBeats * PIXELS_PER_BEAT, 4);
        const nh = NOTE_HEIGHT - 1;

        ctx.fillStyle = "#d97706";
        ctx.fillRect(x, y, nw, nh);
        ctx.strokeStyle = "#b45309";
        ctx.lineWidth = 1;
        ctx.strokeRect(x, y, nw, nh);

        // Velocity bar
        ctx.fillStyle = "rgba(255,255,255,0.2)";
        ctx.fillRect(x, y + nh - 3, nw * (note.velocity / 127), 2);
      }
    }
  }, [notes, selectedClipId, height]);

  const handleCanvasClick = useCallback(
    async (e: React.MouseEvent<HTMLCanvasElement>) => {
      if (!selectedClipId) return;
      const canvas = canvasRef.current;
      if (!canvas) return;

      const rect = canvas.getBoundingClientRect();
      const mx = e.clientX - rect.left;
      const my = e.clientY - rect.top;

      const clickedPitch = HIGHEST_NOTE - Math.floor(my / NOTE_HEIGHT);
      if (clickedPitch < LOWEST_NOTE || clickedPitch > HIGHEST_NOTE) return;

      const clip = useProjectStore.getState().snapshot?.clips.find(
        (c) => c.clipId === selectedClipId
      );
      const offsetX = clip?.startBeat ?? 0;
      const beat = mx / PIXELS_PER_BEAT + offsetX;

      // Check if we hit an existing note
      const hitNote = notes.find((n) => {
        const nx = (n.startBeat - offsetX) * PIXELS_PER_BEAT;
        const ny = (HIGHEST_NOTE - n.pitch) * NOTE_HEIGHT;
        const nw = n.durationBeats * PIXELS_PER_BEAT;
        return mx >= nx && mx <= nx + nw && my >= ny && my <= ny + NOTE_HEIGHT;
      });

      if (hitNote) {
        // Click existing note: select or delete on double-click
        return;
      }

      // Create new note
      await rpc.call("project.addNote", {
        clipId: selectedClipId,
        pitch: clickedPitch,
        velocity: 100,
        startBeat: beat,
        durationBeats: 1,
      });
      await syncNotes(rpc, selectedClipId);
    },
    [selectedClipId, notes, rpc, syncNotes]
  );

  const handleDoubleClick = useCallback(
    async (e: React.MouseEvent<HTMLCanvasElement>) => {
      if (!selectedClipId) return;
      const canvas = canvasRef.current;
      if (!canvas) return;

      const rect = canvas.getBoundingClientRect();
      const mx = e.clientX - rect.left;
      const my = e.clientY - rect.top;

      const clip = useProjectStore.getState().snapshot?.clips.find(
        (c) => c.clipId === selectedClipId
      );
      const offsetX = clip?.startBeat ?? 0;

      const hitNote = notes.find((n) => {
        const nx = (n.startBeat - offsetX) * PIXELS_PER_BEAT;
        const ny = (HIGHEST_NOTE - n.pitch) * NOTE_HEIGHT;
        const nw = n.durationBeats * PIXELS_PER_BEAT;
        return mx >= nx && mx <= nx + nw && my >= ny && my <= ny + NOTE_HEIGHT;
      });

      if (hitNote) {
        await rpc.call("project.removeNote", { noteId: hitNote.noteId });
        await syncNotes(rpc, selectedClipId);
      }
    },
    [selectedClipId, notes, rpc, syncNotes]
  );

  return (
    <div style={{ display: "flex", flex: 1, overflow: "hidden" }}>
      {/* Piano keys sidebar */}
      <div style={{
        width: KEY_WIDTH,
        minWidth: KEY_WIDTH,
        overflow: "hidden",
        background: "var(--bg-widget)",
        borderRight: "1px solid var(--border)",
      }}>
        {Array.from({ length: NUM_KEYS }, (_, i) => {
          const pitch = HIGHEST_NOTE - i;
          const isBlack = [1, 3, 6, 8, 10].includes(pitch % 12);
          const label = ["C","C#","D","D#","E","F","F#","G","G#","A","A#","B"][pitch % 12];
          return (
            <div
              key={pitch}
              style={{
                height: NOTE_HEIGHT,
                background: isBlack ? "#1a1a1e" : "var(--track-fill-1)",
                borderBottom: "1px solid var(--border)",
                paddingLeft: 4,
                fontSize: 8,
                color: "var(--text-muted)",
                display: "flex",
                alignItems: "center",
              }}
            >
              {label}{Math.floor(pitch / 12) - 1}
            </div>
          );
        })}
      </div>

      {/* Note canvas */}
      <canvas
        ref={canvasRef}
        style={{ flex: 1, height, cursor: "crosshair" }}
        onClick={handleCanvasClick}
        onDoubleClick={handleDoubleClick}
      />
    </div>
  );
}
```

Wait — the `NoteGrid` component above has an issue: `height` is used as the canvas CSS height but the canvas is in a flex container. Let me reconsider the layout. The piano roll should scroll vertically. Let me make the canvas have a fixed height based on NUM_KEYS * NOTE_HEIGHT and wrap in a scroll container.

Actually, looking at the plan again, Phase 1 says "Proves canvas editing works" — the simplicity is fine for a thin slice. The `NoteGrid` renders notes for the selected MIDI clip and allows click-to-add and double-click-to-delete. Let me simplify and fix the layout issues.

Let me re-think: the `NoteGrid.tsx` (canvas) and `PianoRoll.tsx` (container with key labels + canvas + clip selector dropdown) should be separate.

Let me also note that the selectedClipId should be managed in PianoRoll, not in NoteGrid. Let me fix this.

Actually for v1 thin-slice, having NoteGrid manage its own clip selection is fine. Let me just make the component work. The CSS height issue can be handled by setting the canvas height to NUM_KEYS * NOTE_HEIGHT and wrapping in a scroll container.

Let me revise the NoteGrid to be simpler and more robust:

```tsx
// Simplified without inline state for selectedClipId — PianoRoll passes it down
```

Actually let me just keep the plan focused on what exists. The important thing is the `PianoRoll.tsx` wraps the note grid with a clip selector, and `NoteGrid.tsx` is the canvas. Let me tidy this up for the plan.

For the plan I'll present NoteGrid as pure canvas rendering with click/double-click handlers, and PianoRoll as the container that manages state.

- [ ] **Step 1: Create `frontend/src/components/NoteGrid.tsx`**

```tsx
import { useEffect, useRef, useCallback } from "react";
import { RpcClient } from "../rpc/client";
import { useProjectStore } from "../store/projectStore";
import { NoteSnapshot } from "../rpc/types";

const NOTE_HEIGHT = 10;
const PIXELS_PER_BEAT = 40;
const LOWEST_NOTE = 48;
const HIGHEST_NOTE = 83;
const NUM_KEYS = HIGHEST_NOTE - LOWEST_NOTE + 1;

interface Props {
  rpc: RpcClient;
  clipId: number | null;
  notes: NoteSnapshot[];
}

export function NoteGrid({ rpc, clipId, notes }: Props) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const height = NUM_KEYS * NOTE_HEIGHT;

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext("2d");
    if (!ctx) return;

    const dpr = window.devicePixelRatio || 1;
    const w = canvas.clientWidth || 400;
    canvas.width = w * dpr;
    canvas.height = height * dpr;
    ctx.scale(dpr, dpr);
    ctx.clearRect(0, 0, w, height);

    // Grid lines
    for (let i = 0; i < NUM_KEYS; i++) {
      const y = i * NOTE_HEIGHT + 0.5;
      ctx.strokeStyle = (LOWEST_NOTE + i) % 12 === 0
        ? "rgba(255,255,255,0.08)"
        : "rgba(255,255,255,0.04)";
      ctx.beginPath();
      ctx.moveTo(0, y);
      ctx.lineTo(w, y);
      ctx.stroke();
    }

    // Notes
    if (!clipId) return;
    const clip = useProjectStore.getState().snapshot?.clips.find(c => c.clipId === clipId);
    if (!clip) return;
    const offsetX = clip.startBeat;

    for (const note of notes) {
      const x = (note.startBeat - offsetX) * PIXELS_PER_BEAT;
      const y = (HIGHEST_NOTE - note.pitch) * NOTE_HEIGHT;
      const nw = Math.max(note.durationBeats * PIXELS_PER_BEAT, 4);
      const nh = NOTE_HEIGHT - 1;

      ctx.fillStyle = "#d97706";
      ctx.fillRect(x, y, nw, nh);
      ctx.strokeStyle = "#b45309";
      ctx.lineWidth = 1;
      ctx.strokeRect(x, y, nw, nh);
    }
  }, [notes, clipId, height]);

  const handleClick = useCallback(
    async (e: React.MouseEvent<HTMLCanvasElement>) => {
      if (!clipId) return;
      const canvas = canvasRef.current;
      if (!canvas) return;
      const rect = canvas.getBoundingClientRect();
      const mx = e.clientX - rect.left;
      const my = e.clientY - rect.top;
      const clickedPitch = HIGHEST_NOTE - Math.floor(my / NOTE_HEIGHT);
      if (clickedPitch < LOWEST_NOTE || clickedPitch > HIGHEST_NOTE) return;

      const clip = useProjectStore.getState().snapshot?.clips.find(c => c.clipId === clipId);
      if (!clip) return;
      const beat = mx / PIXELS_PER_BEAT + clip.startBeat;

      // Check hit on existing note
      const hit = notes.find(n => {
        const nx = (n.startBeat - clip.startBeat) * PIXELS_PER_BEAT;
        const ny = (HIGHEST_NOTE - n.pitch) * NOTE_HEIGHT;
        return mx >= nx && mx <= nx + n.durationBeats * PIXELS_PER_BEAT
          && my >= ny && my <= ny + NOTE_HEIGHT;
      });
      if (hit) return;

      await rpc.call("project.addNote", {
        clipId, pitch: clickedPitch, velocity: 100,
        startBeat: beat, durationBeats: 1,
      });
    },
    [clipId, notes, rpc]
  );

  const handleDoubleClick = useCallback(
    async (e: React.MouseEvent<HTMLCanvasElement>) => {
      if (!clipId) return;
      const canvas = canvasRef.current;
      if (!canvas) return;
      const rect = canvas.getBoundingClientRect();
      const mx = e.clientX - rect.left;
      const my = e.clientY - rect.top;
      const clip = useProjectStore.getState().snapshot?.clips.find(c => c.clipId === clipId);
      if (!clip) return;

      const hit = notes.find(n => {
        const nx = (n.startBeat - clip.startBeat) * PIXELS_PER_BEAT;
        const ny = (HIGHEST_NOTE - n.pitch) * NOTE_HEIGHT;
        return mx >= nx && mx <= nx + n.durationBeats * PIXELS_PER_BEAT
          && my >= ny && my <= ny + NOTE_HEIGHT;
      });
      if (hit) {
        await rpc.call("project.removeNote", { noteId: hit.noteId });
      }
    },
    [clipId, notes, rpc]
  );

  return (
    <canvas
      ref={canvasRef}
      style={{ height, cursor: "crosshair", width: "100%" }}
      onClick={handleClick}
      onDoubleClick={handleDoubleClick}
    />
  );
}
```

- [ ] **Step 2: Create `frontend/src/components/PianoRoll.tsx`**

```tsx
import { useEffect, useState, useCallback } from "react";
import { RpcClient } from "../rpc/client";
import { NoteGrid } from "./NoteGrid";
import { useProjectStore } from "../store/projectStore";

interface Props {
  rpc: RpcClient;
}

const KEY_WIDTH = 40;
const NOTE_HEIGHT = 10;
const LOWEST_NOTE = 48;
const HIGHEST_NOTE = 83;
const NUM_KEYS = HIGHEST_NOTE - LOWEST_NOTE + 1;

export function PianoRoll({ rpc }: Props) {
  const [selectedClipId, setSelectedClipId] = useState<number | null>(null);
  const clips = useProjectStore((s) =>
    s.snapshot?.clips.filter((c) => c.isMidi) ?? []
  );
  const notes = useProjectStore((s) =>
    selectedClipId !== null ? s.notesByClip.get(selectedClipId) ?? [] : []
  );
  const syncNotes = useProjectStore((s) => s.syncNotes);

  useEffect(() => {
    if (clips.length > 0 && selectedClipId === null) {
      setSelectedClipId(clips[0].clipId);
      syncNotes(rpc, clips[0].clipId);
    }
  }, [clips, selectedClipId, syncNotes, rpc]);

  const handleClipChange = useCallback(
    (e: React.ChangeEvent<HTMLSelectElement>) => {
      const id = parseInt(e.target.value, 10);
      setSelectedClipId(id);
      syncNotes(rpc, id);
    },
    [rpc, syncNotes]
  );

  return (
    <div style={{ display: "flex", flexDirection: "column", height: "100%" }}>
      {/* Toolbar */}
      <div style={{ display: "flex", alignItems: "center", gap: 6, padding: "2px 6px", borderBottom: "1px solid var(--border)", background: "var(--bg-header)" }}>
        <span style={{ fontSize: 10, color: "var(--text-secondary)" }}>MIDI Clip:</span>
        <select
          value={selectedClipId ?? ""}
          onChange={handleClipChange}
          style={{ background: "var(--bg-input)", color: "var(--text-primary)", border: "1px solid var(--border-light)", borderRadius: 2, fontSize: 10, padding: "1px 4px" }}
        >
          {clips.map((c) => (
            <option key={c.clipId} value={c.clipId}>{c.name}</option>
          ))}
        </select>
      </div>

      {/* Keys + grid */}
      <div style={{ display: "flex", flex: 1, overflow: "auto" }}>
        {/* Piano keys */}
        <div style={{ width: KEY_WIDTH, minWidth: KEY_WIDTH, background: "var(--bg-widget)", borderRight: "1px solid var(--border)" }}>
          {Array.from({ length: NUM_KEYS }, (_, i) => {
            const pitch = HIGHEST_NOTE - i;
            const isBlack = [1, 3, 6, 8, 10].includes(pitch % 12);
            const label = ["C","C#","D","D#","E","F","F#","G","G#","A","A#","B"][pitch % 12];
            return (
              <div key={pitch} style={{
                height: NOTE_HEIGHT,
                background: isBlack ? "#1a1a1e" : "var(--track-fill-1)",
                borderBottom: "1px solid var(--border)",
                paddingLeft: 4,
                fontSize: 8,
                color: "var(--text-muted)",
                display: "flex",
                alignItems: "center",
              }}>
                {label}{Math.floor(pitch / 12) - 1}
              </div>
            );
          })}
        </div>

        {/* Note grid canvas */}
        <div style={{ flex: 1, overflow: "auto" }}>
          <NoteGrid rpc={rpc} clipId={selectedClipId} notes={notes} />
        </div>
      </div>
    </div>
  );
}
```

- [ ] **Step 3: Commit**

```bash
git add frontend/src/components/PianoRoll.tsx frontend/src/components/NoteGrid.tsx
git commit -m "frontend: add PianoRoll and NoteGrid canvas with add/remove notes"
```

---

### Task 10: TimelineMinimal component

**Files:**
- Create: `frontend/src/components/TimelineMinimal.tsx`

Shows static clip rectangles positioned by `startBeat` and `durationBeats`, colored by track, with a playhead line driven by `transportStore`.

- [ ] **Step 1: Create `frontend/src/components/TimelineMinimal.tsx`**

```tsx
import { useEffect, useRef } from "react";
import { useProjectStore } from "../store/projectStore";
import { useTransportStore } from "../store/transportStore";

const PIXELS_PER_BEAT = 20;
const CLIP_HEIGHT = 60;
const TRACK_HEIGHT = 70;
const TRACK_SPACING = 4;

const TRACK_COLORS = [
  "#d97706", "#10b981", "#38b2df", "#ef4444",
  "#8b5cf6", "#ec4899", "#14b8a6", "#f59e0b",
];

export function TimelineMinimal() {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const tracks = useProjectStore((s) => s.snapshot?.tracks ?? []);
  const clips = useProjectStore((s) => s.snapshot?.clips ?? []);
  const transport = useTransportStore((s) => s.transport);
  const totalHeight = tracks.length * TRACK_HEIGHT;

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext("2d");
    if (!ctx) return;

    const dpr = window.devicePixelRatio || 1;
    const w = canvas.clientWidth || 800;
    const h = canvas.clientHeight || totalHeight;
    canvas.width = w * dpr;
    canvas.height = Math.max(h, totalHeight) * dpr;
    ctx.scale(dpr, dpr);
    ctx.clearRect(0, 0, w, Math.max(h, totalHeight));

    // Background
    ctx.fillStyle = "#141416";
    ctx.fillRect(0, 0, w, Math.max(h, totalHeight));

    // Track backgrounds
    for (let ti = 0; ti < tracks.length; ti++) {
      const ty = ti * TRACK_HEIGHT;
      ctx.fillStyle = ti % 2 === 0 ? "#28282c" : "#2c2c30";
      ctx.fillRect(0, ty, w, TRACK_HEIGHT - TRACK_SPACING);

      // Track label
      ctx.fillStyle = "#787880";
      ctx.font = "10px sans-serif";
      ctx.fillText(tracks[ti].name, 4, ty + 12);
    }

    // Clips
    for (const clip of clips) {
      const ti = clip.trackIndex;
      if (ti < 0 || ti >= tracks.length) continue;
      const ty = ti * TRACK_HEIGHT + 14; // offset for label
      const x = clip.startBeat * PIXELS_PER_BEAT;
      const cw = Math.max(clip.durationBeats * PIXELS_PER_BEAT, 4);

      ctx.fillStyle = TRACK_COLORS[ti % TRACK_COLORS.length];
      ctx.fillRect(x, ty, cw, CLIP_HEIGHT - 14);

      // Clip border
      ctx.strokeStyle = "rgba(255,255,255,0.15)";
      ctx.lineWidth = 1;
      ctx.strokeRect(x, ty, cw, CLIP_HEIGHT - 14);

      // Clip name
      ctx.fillStyle = "#e8e8ec";
      ctx.font = "9px sans-serif";
      ctx.fillText(clip.name, x + 4, ty + 12);

      // MIDI indicator
      if (clip.isMidi) {
        ctx.fillStyle = "#f59e0b";
        const midiX = x + cw - 12;
        ctx.beginPath();
        ctx.arc(midiX, ty + 6, 4, 0, Math.PI * 2);
        ctx.fill();
      }
    }

    // Playhead
    if (transport.isPlaying || transport.currentTimeSeconds > 0) {
      const bpm = transport.bpm || 120;
      const beatPos = (transport.currentTimeSeconds * bpm) / 60;
      const phx = beatPos * PIXELS_PER_BEAT;

      ctx.strokeStyle = "#f59e0b";
      ctx.lineWidth = 2;
      ctx.beginPath();
      ctx.moveTo(phx, 0);
      ctx.lineTo(phx, Math.max(h, totalHeight));
      ctx.stroke();
    }
  }, [tracks, clips, transport, totalHeight]);

  return (
    <canvas
      ref={canvasRef}
      style={{ width: "100%", height: Math.max(totalHeight, 100), display: "block" }}
    />
  );
}
```

- [ ] **Step 2: Commit**

```bash
git add frontend/src/components/TimelineMinimal.tsx
git commit -m "frontend: add TimelineMinimal with static clip rectangles and playhead"
```

---

### Task 11: Verify end-to-end

- [ ] **Step 1: Build the C++ engine with frontend server**

```bash
cmake --build build --config Debug
```

Expected: Build succeeds. `HDAW.exe` supports `--headless` flag.

- [ ] **Step 2: Start the engine in headless mode** (in one terminal)

```bash
build/Debug/HDAW.exe --headless --port=8766
```

Expected: Prints nothing to stdout (logging goes to `%TEMP%/hdaw_debug.log`). Process stays alive.

- [ ] **Step 3: Start the Electron dev server** (in another terminal)

```bash
cd frontend
npm run electron:dev
```

Expected: Vite starts on `localhost:5173`, then Electron opens a window loading the app. The renderer connects to the engine via WebSocket on port 8766.

- [ ] **Step 4: Manual smoke test**

Verify:
- App shows "Connecting to engine..." then transitions to the DAW layout
- TransportBar buttons are functional (Play/Stop affect the playhead)
- TrackHeaders show track names, volume/pan sliders, mute/solo buttons
- Mixer shows strips with VU bars that respond to playback
- Piano roll shows a MIDI clip selector, clicking creates notes, double-click deletes
- Timeline shows colored clip rectangles
- Status bar shows BPM and sample rate

- [ ] **Step 5: Commit the full Phase 1 work**

```bash
git add -A
git commit -m "frontend: Phase 1 thin vertical slice complete"
```
