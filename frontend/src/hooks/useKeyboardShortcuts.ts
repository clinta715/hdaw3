import { useEffect } from "react";
import { rpc } from "../rpc";

export function useKeyboardShortcuts() {
  useEffect(() => {
    const handler = (e: KeyboardEvent) => {
      const target = e.target as HTMLElement;
      if (target.tagName === "INPUT" || target.tagName === "TEXTAREA" || target.tagName === "SELECT") return;

      const ctrl = e.ctrlKey || e.metaKey;
      const shift = e.shiftKey;

      // Transport — Shift+Space: stop
      if (e.code === "Space" && shift) {
        e.preventDefault();
        rpc.call("transport.stop");
      }

      // Transport — R: record
      if (e.code === "KeyR" && !ctrl && !shift) {
        e.preventDefault();
        rpc.call("transport.record");
      }

      // Transport — Home: rewind
      if (e.code === "Home" && !ctrl) {
        e.preventDefault();
        rpc.call("transport.rewind");
      }

      // Project — Ctrl+N: new project
      if (ctrl && e.key === "n") {
        e.preventDefault();
        rpc.call("project.newProject");
      }

      // Transport — Ctrl+L: toggle loop
      if (ctrl && e.key === "l") {
        e.preventDefault();
        rpc.call("transport.toggleLoop");
      }

      // Track — Ctrl+Shift+T: add track
      if (ctrl && shift && e.key === "T") {
        e.preventDefault();
        rpc.call("project.addTrack");
      }
    };

    window.addEventListener("keydown", handler);
    return () => window.removeEventListener("keydown", handler);
  }, []);
}
