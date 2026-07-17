# HDAW Electron/React Frontend — Phase 2 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Replace the Phase 1 polling loop with push notifications for transport and tree-changed events. Transport timecode updates at 30 Hz (from the engine's existing push timer). Tree mutations trigger an immediate `syncSnapshot` call instead of waiting up to 500ms.

**Architecture:** The engine already sends `notify.transport` (30 Hz), `notify.meters` (30 Hz), and `notify.treeChanged` (16ms debounced) via `FrontendServer`. Phase 1 already subscribes to `notify.meters`. Phase 2 adds subscriptions for the other two and removes the polling loop.

---

### Task 0: Write spec + plan (this doc)

- [ ] **Done** — docs/superpowers/specs/2026-07-17-hdaw-electron-frontend-phase2-design.md
- [ ] **Done** — docs/superpowers/plans/2026-07-17-hdaw-electron-frontend-phase2.md

---

### Task 1: Replace poll loop with push notifications

**Files:**
- Modify: `frontend/src/main.tsx`

**Changes:**
1. Subscribe to `notify.transport` on connect — update `transportStore` directly with the payload
2. Subscribe to `notify.treeChanged` on connect — call `syncSnapshot(rpc)` to re-fetch state
3. Remove the 500ms `setTimeout` poll loop entirely
4. Move the initial `syncSnapshot(rpc)` to run once after connect
5. Ensure subscriptions survive reconnect by wiring inside the `onopen` handler (create a `setupSubscriptions(rpc)` function called both on initial connect and on reconnect)

Read the current `frontend/src/main.tsx` first, then apply these changes.

**New `main.tsx` structure:**

```tsx
import React from "react";
import ReactDOM from "react-dom/client";
import { injectTheme } from "./theme";
import { rpc } from "./rpc";
import { useProjectStore } from "./store/projectStore";
import { useTransportStore } from "./store/transportStore";
import { useMeterStore } from "./store/meterStore";
import { TransportSnapshot, MetersPayload } from "./rpc/types";
import App from "./App";

injectTheme();

function setupSubscriptions() {
  // Remove old subscriptions if any
  rpc.onNotification("notify.transport", (_, params) => {
    useTransportStore.getState().update(params as TransportSnapshot);
  });
  rpc.onNotification("notify.meters", (_, params) => {
    useMeterStore.getState().update(params as MetersPayload);
  });
  rpc.onNotification("notify.treeChanged", () => {
    useProjectStore.getState().syncSnapshot(rpc).catch(() => {});
  });
}

async function init() {
  await rpc.connect();
  setupSubscriptions();

  // Initial snapshot fetch
  await useProjectStore.getState().syncSnapshot(rpc);

  ReactDOM.createRoot(document.getElementById("root")!).render(
    <React.StrictMode>
      <App />
    </React.StrictMode>
  );
}

init().catch(console.error);
```

Note: `syncSnapshot` now returns a `Promise<void>`. Ensure the store's `syncSnapshot` signature matches (it already does — the store uses `async`).

Also note: `onNotification` returns a cleanup function. For Phase 2, we don't call the cleanup — the old handlers are harmless because they replace state atomically. If we want to avoid handler pile-up on reconnect, we can store the cleanup functions and call them before re-subscribing. Let's add that:

```typescript
const cleanups: (() => void)[] = [];

function setupSubscriptions() {
  // Clean up old handlers before re-subscribing
  for (const c of cleanups) c();
  cleanups.length = 0;

  cleanups.push(rpc.onNotification("notify.transport", (_, params) => {
    useTransportStore.getState().update(params as TransportSnapshot);
  }));
  cleanups.push(rpc.onNotification("notify.meters", (_, params) => {
    useMeterStore.getState().update(params as MetersPayload);
  }));
  cleanups.push(rpc.onNotification("notify.treeChanged", () => {
    useProjectStore.getState().syncSnapshot(rpc).catch(() => {});
  }));
}
```

**Verify:**

```bash
cd frontend && npx tsc --noEmit
```

**Commit:**

```bash
git add frontend/src/main.tsx
git commit -m "frontend: replace 500ms poll loop with push notifications for transport, tree, and meters"
```

---

### Task 2: Build-check

- [ ] **Verify full build:**

```bash
cd frontend
npx tsc --noEmit
npx vite build
```

---

### Task 3: Update AGENTS.md (optional)

If the frontend section needs a note about notification-driven state architecture, add it.
