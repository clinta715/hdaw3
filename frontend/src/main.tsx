import { useState, useEffect } from "react";
import React from "react";
import ReactDOM from "react-dom/client";
import { injectTheme } from "./theme";
import { rpc } from "./rpc";
import { useProjectStore } from "./store/projectStore";
import { useAutomationStore } from "./store/automationStore";
import { useTransportStore } from "./store/transportStore";
import { useMeterStore } from "./store/meterStore";
import { useUiStore } from "./store/uiStore";
import { useLibraryStore } from "./store/libraryStore";
import { TransportSnapshot, MetersPayload, TreeDelta } from "./rpc/types";
import App from "./App";
import { ErrorBoundary } from "./components/ErrorBoundary";
import StartupDialog from "./components/StartupDialog";
import { LoadingOverlay } from "./components/LoadingOverlay";

window.addEventListener("unhandledrejection", (event) => {
  console.error("[HDAW] Unhandled promise rejection:", event.reason);
  try {
    const { useNotifyStore } = require("./store/notifyStore");
    useNotifyStore.getState().push({
      level: "error",
      message: `Unhandled error: ${event.reason instanceof Error ? event.reason.message : String(event.reason)}`,
    });
  } catch {
    // notifyStore may not be initialized yet — console log is sufficient
  }
});

injectTheme();

const cleanups: (() => void)[] = [];

function setupSubscriptions() {
  for (const c of cleanups) c();
  cleanups.length = 0;

  cleanups.push(rpc.onNotification("notify.transport", (_, params) => {
    useTransportStore.getState().update(params as TransportSnapshot);
  }));

  cleanups.push(rpc.onNotification("notify.meters", (_, params) => {
    useMeterStore.getState().update(params as MetersPayload);
  }));

  cleanups.push(rpc.onNotification("notify.pluginCrashed", (_method, params) => {
    const p = params as { trackIndex?: number; pluginName?: string; pluginId?: string } | undefined;
    if (p && typeof p.trackIndex === "number" && p.pluginId) {
      useUiStore.getState().setSlotCrashed(p.trackIndex, p.pluginId, p.pluginName ?? p.pluginId);
    }
  }));

  cleanups.push(rpc.onNotification("notify.pluginRecovered", (_method, params) => {
    const p = params as { trackIndex?: number; pluginId?: string } | undefined;
    if (p && typeof p.trackIndex === "number" && p.pluginId) {
      useUiStore.getState().clearSlotCrashed(p.trackIndex, p.pluginId);
    }
  }));

  cleanups.push(rpc.onNotification("notify.loadProgress", (_method, params) => {
    const p = params as { message?: string; progress?: number } | undefined;
    if (p) {
      useProjectStore.getState().updateLoadProgress(
        p.message ?? "Loading...",
        typeof p.progress === "number" ? p.progress : 0
      );
    }
  }));

  cleanups.push(rpc.onNotification("notify.treeChanged", (_, params) => {
    const d = params as TreeDelta | undefined;
    if (d && !d.fullSync && (d.clipsUpserted || d.clipsRemoved || d.tracksUpserted)) {
      useProjectStore.getState().applyDelta(d);
    } else {
      useProjectStore.getState().syncSnapshot(rpc).catch(() => {});
      const activeTrack = useAutomationStore.getState().activeTrackIndex;
      if (activeTrack !== null) {
        useAutomationStore.getState().fetchForTrack(activeTrack, rpc);
      }
    }
  }));

  cleanups.push(rpc.onNotification("notify.libraryScanProgress", (_method, params) => {
    const p = params as { libraryId?: string; scanned?: number; total?: number; phase?: string } | undefined;
    if (p && typeof p.libraryId === "string" && typeof p.scanned === "number" && typeof p.total === "number") {
      useLibraryStore.getState().updateScanProgress({
        libraryId: p.libraryId,
        scanned: p.scanned,
        total: p.total,
        phase: p.phase ?? "",
      });
    }
  }));

  cleanups.push(rpc.onNotification("notify.libraryScanComplete", (_method, params) => {
    const p = params as { libraryId?: string; success?: boolean } | undefined;
    if (p?.success) {
      useLibraryStore.getState().loadLibraries(rpc);
    }
  }));
}

function Root() {
  const [showStartup, setShowStartup] = useState(true);

  // Handle .hdaw file open from the Electron shell (double-click file
  // association). Loads the project and skips the startup dialog.
  useEffect(() => {
    const hdaw = (window as any).hdaw;
    if (!hdaw?.on) return;
    return hdaw.on("open-project-file", async (filePath: string) => {
      try {
        await rpc.call("project.loadProject", { filePath });
        await useProjectStore.getState().syncSnapshot(rpc);
        useProjectStore.getState().addRecentProject(filePath);
        setShowStartup(false);
      } catch (err) {
        console.error("Failed to open project from file association:", err);
      }
    });
  }, []);

  return (
    <React.StrictMode>
      <ErrorBoundary>
        <LoadingOverlay />
        {showStartup && <StartupDialog onClose={() => setShowStartup(false)} />}
        {!showStartup && <App />}
      </ErrorBoundary>
    </React.StrictMode>
  );
}

async function init() {
  await rpc.connect();
  setupSubscriptions();

  await useProjectStore.getState().syncSnapshot(rpc);

  ReactDOM.createRoot(document.getElementById("root")!).render(<Root />);
}

init().catch(console.error);
