import { useState, useRef, useEffect } from "react";
import { rpc } from "../rpc";
import { useProjectStore } from "../store/projectStore";
import "./FileMenu.css";

export default function FileMenu() {
  const [open, setOpen] = useState(false);
  const ref = useRef<HTMLDivElement>(null);

  useEffect(() => {
    const handler = (e: MouseEvent) => {
      if (ref.current && !ref.current.contains(e.target as Node)) setOpen(false);
    };
    document.addEventListener("mousedown", handler);
    return () => document.removeEventListener("mousedown", handler);
  }, []);

  const doAction = async (fn: () => Promise<void>) => {
    setOpen(false);
    await fn();
    await useProjectStore.getState().syncDirtyFlag(rpc);
    await useProjectStore.getState().syncSnapshot(rpc);
  };

  const handleNew = () => doAction(async () => {
    if (!confirm("Create new project? Unsaved changes will be lost.")) return;
    await rpc.call("project.newProject").catch(() => {});
  });

  const handleSave = () => doAction(async () => {
    const filePath = prompt("Save path:", "project.hdaw");
    if (!filePath) return;
    await rpc.call("project.saveProject", { filePath }).catch(() => {});
  });

  const handleLoad = () => doAction(async () => {
    const filePath = prompt("Load path:", "project.hdaw");
    if (!filePath) return;
    await rpc.call("project.loadProject", { filePath }).catch(() => {});
  });

  return (
    <div className="file-menu" ref={ref}>
      <button className="fm-trigger" onClick={() => setOpen(!open)}>File</button>
      {open && (
        <div className="fm-dropdown">
          <button onClick={handleNew}>New Project</button>
          <button onClick={handleSave}>Save</button>
          <button onClick={handleLoad}>Load</button>
        </div>
      )}
    </div>
  );
}
