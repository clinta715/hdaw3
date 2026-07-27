import { useState, useEffect } from "react";
import React from "react";
import ReactDOM from "react-dom/client";
import { injectTheme } from "./theme";
import { rpc } from "./rpc";
import { useProjectStore } from "./store/projectStore";
import { useAutomationStore } from "./store/automationStore";
import { useTransportStore } from "./store/transportStore";
import { useMeterStore } from "./store/meterStore";
import { TransportSnapshot, MetersPayload, TreeDelta } from "./rpc/types";
import App from "./App";
import { ErrorBoundary } from "./components/ErrorBoundary";
import StartupDialog from "./components/StartupDialog";
import { LoadingOverlay } from "./components/LoadingOverlay";

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
