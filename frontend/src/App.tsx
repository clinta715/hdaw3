import { useEffect, useRef } from "react";
import { useKeyboardShortcuts } from "./hooks/useKeyboardShortcuts";
import { reportRpcError } from "./store/notifyStore";
import "./App.css";
import TransportBar from "./components/TransportBar";
import TrackHeaders from "./components/TrackHeaders";
import Mixer from "./components/Mixer";
import PianoRoll from "./components/PianoRoll";
import TimelineMinimal from "./components/TimelineMinimal";
import ClipEditor from "./components/ClipEditor";
import AudioClipEditor from "./components/AudioClipEditor";
import StepSequencer from "./components/StepSequencer";
import AutomationPanel from "./components/AutomationPanel";
import FXChain from "./components/FXChain";
import ModulationPanel from "./components/ModulationPanel";
import BottomTabs from "./components/BottomTabs";
import StatusBar from "./components/StatusBar";
import FileBrowser from "./components/FileBrowser";
import Toaster from "./components/Toaster";
import { useUiStore } from "./store/uiStore";
import { useProjectStore } from "./store/projectStore";
import { useBrowserStore } from "./store/browserStore";
import { rpc } from "./rpc";

function App() {
  useKeyboardShortcuts();
  const selectedClipIds = useUiStore((s) => s.selectedClipIds);
  const activeBottomTab = useUiStore((s) => s.activeBottomTab);
  const setActiveBottomTab = useUiStore((s) => s.setActiveBottomTab);
  const snapshot = useProjectStore((s) => s.snapshot);
  const isDirty = useProjectStore((s) => s.isDirty);
  const prevTabRef = useRef(activeBottomTab);
  const browserVisible = useBrowserStore((s) => s.visible);
  const toggleBrowser = useBrowserStore((s) => s.toggleVisible);

  // Warn before closing if there are unsaved changes
  useEffect(() => {
    const handler = (e: BeforeUnloadEvent) => {
      if (useProjectStore.getState().isDirty) {
        e.preventDefault();
      }
    };
    window.addEventListener("beforeunload", handler);

    // Electron: the main process intercepts window close and sends
    // "app-close-requested" instead. We handle the dirty check + save dialog
    // here, then call requestClose() to actually close. If the user cancels,
    // we do NOT call requestClose() — the main process already preventDefault'd
    // the close, so the window stays open.
    const hdaw = (window as any).hdaw;
    let unsub: (() => void) | undefined;
    if (hdaw?.on) {
      const onCloseRequested = async () => {
        const dirty = useProjectStore.getState().isDirty;
        if (dirty) {
          const result = await hdaw.showCloseConfirm();
          if (result === "cancel") return;
          if (result === "save") {
            const fp = useProjectStore.getState().filePath;
            try {
              if (fp) {
                await rpc.call("project.saveProject", { filePath: fp });
              } else {
                if (!hdaw.showSaveDialog) return;
                const lastDir = localStorage.getItem("hdaw_last_save_dir") || "";
                const defaultPath = lastDir
                  ? lastDir + "/project.hdaw"
                  : "project.hdaw";
                const saveResult = await hdaw.showSaveDialog({
                  title: "Save Project",
                  defaultPath,
                  filters: [
                    { name: "HDAW Projects", extensions: ["hdaw"] },
                    { name: "All Files", extensions: ["*"] },
                  ],
                });
                if (saveResult.canceled || !saveResult.filePath)
                  return;
                await rpc.call("project.saveProject", { filePath: saveResult.filePath });
                useProjectStore.getState().setFilePath(saveResult.filePath);
                const dir = saveResult.filePath.replace(/[\\/][^\\/]*$/, "");
                localStorage.setItem("hdaw_last_save_dir", dir);
              }
            } catch (err) {
              reportRpcError("project.saveProject", err);
              return;
            }
          }
          // "dont-save" falls through to close
        }
        hdaw.requestClose();
      };
      unsub = hdaw.on("app-close-requested", onCloseRequested);
    }

    return () => {
      window.removeEventListener("beforeunload", handler);
      if (unsub) unsub();
    };
  }, []);

  // Auto-switch bottom tab when a single clip is selected
  useEffect(() => {
    if (selectedClipIds.size === 1) {
      const id = selectedClipIds.values().next().value;
      const clip = snapshot?.clips.find((c) => c.clipId === id);
      if (clip) {
        prevTabRef.current = useUiStore.getState().activeBottomTab;
        setActiveBottomTab(clip.isMidi ? "piano-roll" : "audio-editor");
      }
    }
  }, [selectedClipIds, snapshot, setActiveBottomTab]);

  // When selection clears and we're on a clip-specific tab, restore previous tab
  useEffect(() => {
    const isClipTab = activeBottomTab === "audio-editor" || activeBottomTab === "piano-roll";
    if (isClipTab && selectedClipIds.size !== 1) {
      const restored = prevTabRef.current === "audio-editor" || prevTabRef.current === "piano-roll"
        ? "mixer" : prevTabRef.current;
      setActiveBottomTab(restored);
    }
  }, [selectedClipIds, activeBottomTab, setActiveBottomTab]);

  const bottomTabs = [
    { id: "mixer", label: "Mixer", content: <Mixer /> },
    { id: "piano-roll", label: "Piano Roll", content: <PianoRoll /> },
    { id: "automation", label: "Automation", content: <AutomationPanel rpc={rpc} /> },
    { id: "fx", label: "FX Chain", content: <FXChain /> },
    { id: "audio-editor", label: "Audio Editor", content: <AudioClipEditor /> },
    { id: "modulation", label: "Modulation", content: <ModulationPanel /> },
    { id: "step-seq", label: "Step Seq", content: <StepSequencer /> },
  ];

  return (
    <div className="app-shell">
      <header className="transport-bar">
        <TransportBar />
      </header>
      <button
        className={`browser-toggle-btn${browserVisible ? " browser-toggle-btn--active" : ""}`}
        onClick={toggleBrowser}
        title="Toggle File Browser (Ctrl+B)"
      >
        📁
      </button>
      <aside className="track-headers">
        <TrackHeaders />
      </aside>
      <main className="timeline">
        <TimelineMinimal />
      </main>
      {browserVisible && (
        <aside className="file-browser">
          <FileBrowser />
        </aside>
      )}
      {useUiStore((s) => s.selectedClipIds.size === 1) && (
        <div className="clip-editor-container">
          <ClipEditor />
        </div>
      )}
      <footer className="bottom-panel">
        <BottomTabs
          tabs={bottomTabs}
          defaultTab="mixer"
          activeTab={activeBottomTab}
          onTabChange={setActiveBottomTab}
        />
      </footer>
      <StatusBar />
      <Toaster />
    </div>
  );
}

export default App;
