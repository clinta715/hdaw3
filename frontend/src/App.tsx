import { useEffect, useRef, useState, useCallback, useMemo } from "react";
import { useKeyboardShortcuts } from "./hooks/useKeyboardShortcuts";
import { reportRpcError } from "./store/notifyStore";
import { ErrorBoundary } from "./components/ErrorBoundary";
import { withHookSentinel } from "./dev/hookSentinel";
import "./App.css";
import TransportBar from "./components/TransportBar";
import TrackHeaders from "./components/TrackHeaders";
import Mixer from "./components/Mixer";
import PianoRoll from "./components/PianoRoll";
import TimelineMinimal from "./components/TimelineMinimal";
import SessionView from "./components/SessionView";
import ClipEditor from "./components/ClipEditor";
import AudioClipEditor from "./components/AudioClipEditor";
import StepSequencer from "./components/StepSequencer";
import AutomationPanel from "./components/AutomationPanel";
import FXChain from "./components/FXChain";
import MidiFxChain from "./components/MidiFxChain";
import ModulationPanel from "./components/ModulationPanel";
import SamplerEditor from "./components/SamplerEditor";
import FmAnalysisPanel from "./components/FmAnalysisPanel";
import UndoHistory from "./components/UndoHistory";
import Inspector from "./components/Inspector";
import { ArrangerChainEditor } from "./components/ArrangerChainEditor";
import BottomTabs from "./components/BottomTabs";
import StatusBar from "./components/StatusBar";
import FileBrowser from "./components/FileBrowser";
import Toaster from "./components/Toaster";
import { useUiStore, MIN_BOTTOM_PANEL_H, BOTTOM_TAB_IDS, TAB_DEFAULT_HEIGHTS, type BottomTabId } from "./store/uiStore";
import { useProjectStore } from "./store/projectStore";
import { useBrowserStore } from "./store/browserStore";
import { rpc } from "./rpc";

const SClipEditor = withHookSentinel(ClipEditor, "ClipEditor");
const SAudioClipEditor = withHookSentinel(AudioClipEditor, "AudioClipEditor");
const SPianoRoll = withHookSentinel(PianoRoll, "PianoRoll");
const SMixer = withHookSentinel(Mixer, "Mixer");
const SBottomTabs = withHookSentinel(BottomTabs, "BottomTabs");
const SStepSequencer = withHookSentinel(StepSequencer, "StepSequencer");
const SAutomationPanel = withHookSentinel(AutomationPanel, "AutomationPanel");
const SFXChain = withHookSentinel(FXChain, "FXChain");
const SMidiFxChain = withHookSentinel(MidiFxChain, "MidiFxChain");
const SModulationPanel = withHookSentinel(ModulationPanel, "ModulationPanel");
const SSamplerEditor = withHookSentinel(SamplerEditor, "SamplerEditor");
const SFmAnalysisPanel = withHookSentinel(FmAnalysisPanel, "FmAnalysisPanel");
const SArrangerChainEditor = withHookSentinel(ArrangerChainEditor, "ArrangerChainEditor");

function App() {
  useKeyboardShortcuts();
  const selectedClipIds = useUiStore((s) => s.selectedClipIds);
  const activeBottomTab = useUiStore((s) => s.activeBottomTab);
  const setActiveBottomTab = useUiStore((s) => s.setActiveBottomTab);
  const snapshot = useProjectStore((s) => s.snapshot);
  const isDirty = useProjectStore((s) => s.isDirty);
  const prevTabRef = useRef(activeBottomTab);
  const browserVisible = useBrowserStore((s) => s.visible);
  const bottomPanelHeight = useUiStore((s) => s.bottomPanelHeight);
  const bottomPanelHeights = useUiStore((s) => s.bottomPanelHeights);
  const setBottomPanelHeightForTab = useUiStore((s) => s.setBottomPanelHeightForTab);
  const viewMode = useUiStore((s) => s.viewMode);
  const [panelResizing, setPanelResizing] = useState(false);

  const panelHeight =
    bottomPanelHeights[activeBottomTab] ??
    ((BOTTOM_TAB_IDS as readonly string[]).includes(activeBottomTab)
      ? TAB_DEFAULT_HEIGHTS[activeBottomTab as BottomTabId]
      : undefined) ??
    bottomPanelHeight;

  // Drag the divider above the bottom panel to resize it. The height feeds
  // the --bottom-h CSS variable that sizes the panel's grid row; it persists
  // across sessions via the uiStore setter.
  const startPanelResize = useCallback((e: React.MouseEvent) => {
    e.preventDefault();
    const startY = e.clientY;
    const activeTab = useUiStore.getState().activeBottomTab;
    const startH = useUiStore.getState().effectiveBottomPanelHeight(activeTab);
    setPanelResizing(true);
    const onMove = (ev: MouseEvent) => {
      const max = Math.max(MIN_BOTTOM_PANEL_H, window.innerHeight - 48 - 24 - 120);
      const next = Math.min(max, Math.max(MIN_BOTTOM_PANEL_H, startH + (startY - ev.clientY)));
      setBottomPanelHeightForTab(activeTab, Math.round(next));
    };
    const onUp = () => {
      window.removeEventListener("mousemove", onMove);
      window.removeEventListener("mouseup", onUp);
      setPanelResizing(false);
    };
    window.addEventListener("mousemove", onMove);
    window.addEventListener("mouseup", onUp);
  }, [setBottomPanelHeightForTab]);

  const handleTabChange = useCallback((t: string) => {
    useUiStore.getState().selectBottomTab(t);
  }, []);

  // Tab toggles between arrange and session view
  useEffect(() => {
    const handler = (e: KeyboardEvent) => {
      if (e.key === "Tab" && !e.ctrlKey && !e.altKey && !e.metaKey) {
        e.preventDefault();
        useUiStore.getState().setViewMode(
          useUiStore.getState().viewMode === "arrange" ? "session" : "arrange"
        );
      }
    };
    window.addEventListener("keydown", handler);
    return () => window.removeEventListener("keydown", handler);
  }, []);

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
                    { name: "HDAW Projects", extensions: ["hdaw", "hdaw3"] },
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

  // Auto-switch bottom tab when a single clip is selected. The guard ref
  // ensures we only auto-switch ONCE per clip selection — without it, every
  // snapshot refresh (e.g. toggling a note in the Step Sequencer, which
  // triggers a fullSync) would re-fire this effect and yank the user back to
  // the piano-roll/audio-editor tab even though the selection never changed.
  const lastAutoSwitchedClipRef = useRef<number | null>(null);
  useEffect(() => {
    if (selectedClipIds.size !== 1) {
      lastAutoSwitchedClipRef.current = null;
      return;
    }
    const id = selectedClipIds.values().next().value;
    if (id == null) return;
    const clip = snapshot?.clips.find((c) => c.clipId === id);
    if (!clip) return; // clip data not in snapshot yet — wait for the next update
    if (lastAutoSwitchedClipRef.current === id) return; // already switched for this clip
    lastAutoSwitchedClipRef.current = id;
    prevTabRef.current = useUiStore.getState().activeBottomTab;
    setActiveBottomTab(clip.isMidi ? "piano-roll" : "audio-editor");
  }, [selectedClipIds, snapshot, setActiveBottomTab]);

  // When selection clears and we're on a clip-specific tab, restore previous tab.
  // Only restore if an auto-switch actually happened (lastAutoSwitchedClipRef was set),
  // otherwise manual tab switches get immediately reverted.
  useEffect(() => {
    const isClipTab = activeBottomTab === "audio-editor" || activeBottomTab === "piano-roll";
    if (isClipTab && selectedClipIds.size !== 1 && lastAutoSwitchedClipRef.current != null) {
      const restored = prevTabRef.current === "audio-editor" || prevTabRef.current === "piano-roll"
        ? "mixer" : prevTabRef.current;
      setActiveBottomTab(restored);
    }
  }, [selectedClipIds, activeBottomTab, setActiveBottomTab]);

  const bottomTabs = useMemo(() => [
    { id: "mixer", label: "Mixer", content: <SMixer /> },
    { id: "piano-roll", label: "Piano Roll", content: <SPianoRoll /> },
    { id: "automation", label: "Automation", content: <SAutomationPanel rpc={rpc} /> },
    { id: "fx", label: "FX Chain", content: <SFXChain /> },
    { id: "sampler", label: "Sampler", content: <SSamplerEditor /> },
    { id: "midi-fx", label: "MIDI FX", content: <SMidiFxChain /> },
    { id: "audio-editor", label: "Audio Editor", content: <SAudioClipEditor /> },
    { id: "modulation", label: "Modulation", content: <SModulationPanel /> },
    { id: "step-seq", label: "Step Seq", content: <SStepSequencer /> },
    { id: "undo-history", label: "History", content: <UndoHistory /> },
    { id: "inspector", label: "Inspector", content: <Inspector /> },
    { id: "arranger", label: "Arranger", content: <SArrangerChainEditor /> },
    { id: "fm-analysis", label: "FM Analysis", content: <SFmAnalysisPanel /> },
  ], []);

  return (
    <div
      className="app-shell"
      style={{ "--bottom-h": `${panelHeight}px` } as React.CSSProperties}
    >
      <header className="transport-bar">
        <TransportBar />
      </header>
      <aside className="track-headers">
        <TrackHeaders />
      </aside>
      <main className="timeline">
        <ErrorBoundary fallback={<div style={{ display: "flex", alignItems: "center", justifyContent: "center", height: "100%", color: "#e8a040", fontFamily: "system-ui, sans-serif", fontSize: 13, background: "#1e1e1e" }}>Timeline failed to render. Select a clip or reload.</div>}>
          {viewMode === "session" ? <SessionView /> : <TimelineMinimal />}
        </ErrorBoundary>
      </main>
      {browserVisible && (
        <aside className="file-browser">
          <FileBrowser />
        </aside>
      )}
      {useUiStore((s) => s.selectedClipIds.size === 1) && (
        <div className="clip-editor-container">
          <SClipEditor />
        </div>
      )}
      <div
        className={`panel-resize-handle${panelResizing ? " panel-resize-handle--active" : ""}`}
        onMouseDown={startPanelResize}
        title="Drag to resize the bottom panel"
      />
      <footer className="bottom-panel">
        <SBottomTabs
          tabs={bottomTabs}
          defaultTab="mixer"
          activeTab={activeBottomTab}
          onTabChange={handleTabChange}
        />
      </footer>
      <StatusBar />
      <Toaster />
    </div>
  );
}

export default App;
