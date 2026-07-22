import { useEffect } from "react";
import { rpc } from "../rpc";
import { useBrowserStore } from "../store/browserStore";
import { useProjectStore } from "../store/projectStore";
import { useUiStore } from "../store/uiStore";

// Global keyboard shortcuts. Scope rules:
//   - We ignore keys while the user is typing in an INPUT/TEXTAREA/SELECT so
//     that, e.g. typing "R" into the BPM field doesn't trigger record.
//   - FILE-RELATED SHORTCUTS (Ctrl+N/O/S/Shift+S/Shift+I/Shift+M/Ctrl+E)
//     live in FileMenu.tsx, NOT here. They need access to the unsaved-changes
//     confirm flow and the import/export dialogs, both of which are owned by
//     FileMenu. Defining them here would double-fire Ctrl+N (this hook and
//     FileMenu both register a window keydown listener) and bypass the
//     unsaved-changes prompt.
//   - This hook owns only transport and UI-shell shortcuts that don't need
//     FileMenu state.
export function useKeyboardShortcuts() {
  useEffect(() => {
    const handler = async (e: KeyboardEvent) => {
      const target = e.target as HTMLElement;
      if (target.tagName === "INPUT" || target.tagName === "TEXTAREA" || target.tagName === "SELECT") return;

      const ctrl = e.ctrlKey || e.metaKey;
      const shift = e.shiftKey;

      // Transport — Shift+Space: stop
      if (e.code === "Space" && shift) {
        e.preventDefault();
        rpc.call("transport.stop").catch(console.error);
      }

      // Transport — R: record (toggle via transport.record alias)
      if (e.code === "KeyR" && !ctrl && !shift) {
        e.preventDefault();
        rpc.call("transport.record").catch(console.error);
      }

      // Transport — Home: rewind
      if (e.code === "Home" && !ctrl) {
        e.preventDefault();
        rpc.call("transport.rewind").catch(console.error);
      }

      // Transport — Ctrl+L: toggle loop
      if (ctrl && e.code === "KeyL" && !shift) {
        e.preventDefault();
        rpc.call("transport.toggleLoop").catch(console.error);
      }

      // Track — Ctrl+Shift+T: add track
      if (ctrl && shift && e.code === "KeyT") {
        e.preventDefault();
        rpc.call("project.addTrack").catch(console.error);
      }

      // UI — Ctrl+B: toggle file browser
      if (ctrl && e.code === "KeyB" && !shift) {
        e.preventDefault();
        useBrowserStore.getState().toggleVisible();
      }

      // UI — Ctrl+Shift+G: phrase generator
      if (ctrl && shift && e.code === "KeyG") {
        e.preventDefault();
        useUiStore.getState().setShowPhraseGenerator(true);
      }

      // Edit — Ctrl+Z undo, Ctrl+Shift+Z / Ctrl+Y redo.
      // Registered globally (not in TimelineMinimal) so it works regardless of
      // which panel has focus. preventDefault() also stops Chromium/Electron's
      // built-in edit-role accelerator from competing for the keypress.
      if (ctrl && (e.code === "KeyZ" || e.code === "KeyY")) {
        const isRedo = shift || e.code === "KeyY";
        e.preventDefault();
        try {
          await rpc.call(isRedo ? "project.redo" : "project.undo");
          await useProjectStore.getState().syncDirtyFlag(rpc);
          await useProjectStore.getState().syncSnapshot(rpc);
        } catch (err) {
          console.error("undo/redo failed:", err);
        }
      }
    };

    window.addEventListener("keydown", handler);
    return () => window.removeEventListener("keydown", handler);
  }, []);
}
