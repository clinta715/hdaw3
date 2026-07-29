import { useEffect, useState, useCallback } from "react";
import { useProjectStore } from "../store/projectStore";
import { rpc } from "../rpc";
import "./UndoHistory.css";

export default function UndoHistory() {
  const snapshot = useProjectStore((s) => s.snapshot);
  const [undoDescs, setUndoDescs] = useState<string[]>([]);
  const [redoDescs, setRedoDescs] = useState<string[]>([]);

  const refresh = useCallback(async () => {
    try {
      const [u, r] = await Promise.all([
        rpc.call("project.getUndoDescriptions"),
        rpc.call("project.getRedoDescriptions"),
      ]);
      setUndoDescs(Array.isArray(u) ? (u as string[]) : []);
      setRedoDescs(Array.isArray(r) ? (r as string[]) : []);
    } catch {
      // ignore transient RPC failures
    }
  }, []);

  // Re-fetch whenever the snapshot changes (which happens after undo/redo
  // or any project mutation).
  useEffect(() => { refresh(); }, [snapshot, refresh]);

  const handleUndo = async () => {
    await rpc.call("project.undo").catch(() => {});
    useProjectStore.setState({ isDirty: true });
  };

  const handleRedo = async () => {
    await rpc.call("project.redo").catch(() => {});
    useProjectStore.setState({ isDirty: true });
  };

  // Jump to a specific undo depth: undo N times to reach that point.
  const handleJumpTo = async (depth: number) => {
    for (let i = 0; i < depth; i++) {
      await rpc.call("project.undo").catch(() => {});
    }
    useProjectStore.setState({ isDirty: true });
  };

  return (
    <div className="undo-history">
      <div className="uh-header">
        <span className="uh-title">History</span>
        <div className="uh-actions">
          <button
            className="uh-btn"
            disabled={undoDescs.length === 0}
            onClick={handleUndo}
            title="Undo (Ctrl+Z)"
          >
            Undo
          </button>
          <button
            className="uh-btn"
            disabled={redoDescs.length === 0}
            onClick={handleRedo}
            title="Redo (Ctrl+Shift+Z)"
          >
            Redo
          </button>
        </div>
      </div>
      <div className="uh-list">
        {/* Redo entries (future), newest first */}
        {[...redoDescs].reverse().map((desc, i) => {
          const depth = redoDescs.length - i; // how many undos to reach here from current
          return (
            <div
              key={`redo-${i}`}
              className="uh-entry uh-entry--redo"
              onClick={() => { /* redo N times */ for (let j = 0; j < depth; j++) rpc.call("project.redo").then(() => useProjectStore.setState({ isDirty: true })).catch(() => {}); }}
              title="Click to redo to this point"
            >
              <span className="uh-dot" />
              <span className="uh-label">{desc || "(unnamed)"}</span>
            </div>
          );
        })}
        {/* Current state marker */}
        <div className="uh-entry uh-entry--current">
          <span className="uh-dot" />
          <span className="uh-label">Current</span>
        </div>
        {/* Undo entries (past), newest first */}
        {undoDescs.map((desc, i) => (
          <div
            key={`undo-${i}`}
            className="uh-entry uh-entry--undo"
            onClick={() => handleJumpTo(i + 1)}
            title="Click to undo to this point"
          >
            <span className="uh-dot" />
            <span className="uh-label">{desc || "(unnamed)"}</span>
          </div>
        ))}
        {undoDescs.length === 0 && redoDescs.length === 0 && (
          <div className="uh-empty">No actions yet</div>
        )}
      </div>
    </div>
  );
}
