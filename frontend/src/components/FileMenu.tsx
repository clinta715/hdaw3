import { useState, useRef, useEffect } from "react";
import { rpc } from "../rpc";
import { useProjectStore } from "../store/projectStore";
import { reportRpcError } from "../store/notifyStore";
import ImportDialog from "./ImportDialog";
import ExportDialog from "./ExportDialog";
import "./FileMenu.css";

const checkUnsaved = () => {
  if (useProjectStore.getState().isDirty) {
    return confirm("Project has unsaved changes. Continue?");
  }
  return true;
};

export default function FileMenu() {
  const [open, setOpen] = useState(false);
  const [recentOpen, setRecentOpen] = useState(false);
  const [importMode, setImportMode] = useState<"audio" | "midi" | null>(null);
  const [showExport, setShowExport] = useState(false);
  const ref = useRef<HTMLDivElement>(null);
  const recentRef = useRef<HTMLDivElement>(null);

  const filePath = useProjectStore((s) => s.filePath);
  const recentProjects = useProjectStore((s) => s.recentProjects);

  useEffect(() => {
    const handler = (e: MouseEvent) => {
      if (ref.current && !ref.current.contains(e.target as Node)) {
        setOpen(false);
        setRecentOpen(false);
      }
    };
    document.addEventListener("mousedown", handler);
    return () => document.removeEventListener("mousedown", handler);
  }, []);

  useEffect(() => {
    if (!open) setRecentOpen(false);
  }, [open]);

  const doAction = async (fn: () => Promise<void>) => {
    setOpen(false);
    setRecentOpen(false);
    await fn();
    await useProjectStore.getState().syncDirtyFlag(rpc);
    await useProjectStore.getState().syncSnapshot(rpc);
  };

  const handleNew = () => doAction(async () => {
    if (!checkUnsaved()) return;
    await rpc.call("project.newProject").catch((err) => reportRpcError("project.newProject", err));
    useProjectStore.getState().setFilePath(null);
  });

  const handleOpen = async () => {
    setOpen(false);
    if (!checkUnsaved()) return;
    const { setLoadingProject } = useProjectStore.getState();
    setLoadingProject(true);
    try {
      if (window.hdaw) {
        const result = await window.hdaw.showOpenDialog({
          title: "Open Project",
          filters: [
            { name: "HDAW Projects", extensions: ["hdaw"] },
            { name: "All Files", extensions: ["*"] },
          ],
          properties: ["openFile"],
        });
        if (!result.canceled && result.filePaths.length > 0) {
          const path = result.filePaths[0];
          await rpc.call("project.loadProject", { filePath: path }).catch((err) => reportRpcError("project.loadProject", err));
          useProjectStore.getState().addRecentProject(path);
          await useProjectStore.getState().syncDirtyFlag(rpc);
          await useProjectStore.getState().syncSnapshot(rpc);
        }
      } else {
        const path = prompt("Open project:", filePath ?? "project.hdaw");
        if (!path) { setLoadingProject(false); return; }
        await rpc.call("project.loadProject", { filePath: path }).catch((err) => reportRpcError("project.loadProject", err));
        useProjectStore.getState().addRecentProject(path);
        await useProjectStore.getState().syncDirtyFlag(rpc);
        await useProjectStore.getState().syncSnapshot(rpc);
      }
    } finally {
      setLoadingProject(false);
    }
  };

  const handleOpenRecent = (path: string) => doAction(async () => {
    if (!checkUnsaved()) return;
    const { setLoadingProject } = useProjectStore.getState();
    setLoadingProject(true);
    try {
      await rpc.call("project.loadProject", { filePath: path }).catch((err) => reportRpcError("project.loadProject", err));
      useProjectStore.getState().addRecentProject(path);
    } finally {
      setLoadingProject(false);
    }
  });

  const handleClearRecent = () => {
    localStorage.removeItem("hdaw_recent_projects");
    useProjectStore.getState().loadRecentProjects();
    setRecentOpen(false);
  };

  const handleSave = async () => {
    setOpen(false);
    const fp = useProjectStore.getState().filePath;
    if (fp) {
      await rpc.call("project.saveProject", { filePath: fp }).catch((err) => reportRpcError("project.saveProject", err));
    } else {
      if (window.hdaw) {
        const lastDir = localStorage.getItem("hdaw_last_save_dir") || "";
        const defaultPath = lastDir ? lastDir + "/project.hdaw" : "project.hdaw";
        const result = await window.hdaw.showSaveDialog({
          title: "Save Project",
          defaultPath,
          filters: [
            { name: "HDAW Projects", extensions: ["hdaw"] },
            { name: "All Files", extensions: ["*"] },
          ],
        });
        if (!result.canceled && result.filePath) {
          await rpc.call("project.saveProject", { filePath: result.filePath }).catch((err) => reportRpcError("project.saveProject", err));
          useProjectStore.getState().addRecentProject(result.filePath);
          const dir = result.filePath.replace(/[\\/][^\\/]*$/, "");
          localStorage.setItem("hdaw_last_save_dir", dir);
        }
      } else {
        const path = prompt("Save path:", "project.hdaw");
        if (!path) return;
        await rpc.call("project.saveProject", { filePath: path }).catch((err) => reportRpcError("project.saveProject", err));
        useProjectStore.getState().addRecentProject(path);
      }
    }
    await useProjectStore.getState().syncDirtyFlag(rpc);
    await useProjectStore.getState().syncSnapshot(rpc);
  };

  const handleSaveAs = async () => {
    setOpen(false);
    if (window.hdaw) {
      const result = await window.hdaw.showSaveDialog({
        title: "Save Project As",
        defaultPath: filePath ?? "project.hdaw",
        filters: [
          { name: "HDAW Projects", extensions: ["hdaw"] },
          { name: "All Files", extensions: ["*"] },
        ],
      });
      if (!result.canceled && result.filePath) {
        await rpc.call("project.saveProject", { filePath: result.filePath }).catch((err) => reportRpcError("project.saveProject", err));
        useProjectStore.getState().addRecentProject(result.filePath);
        const dir = result.filePath.replace(/[\\/][^\\/]*$/, "");
        localStorage.setItem("hdaw_last_save_dir", dir);
        await useProjectStore.getState().syncDirtyFlag(rpc);
        await useProjectStore.getState().syncSnapshot(rpc);
      }
    } else {
      const path = prompt("Save as:", filePath ?? "project.hdaw");
      if (!path) return;
      await rpc.call("project.saveProject", { filePath: path }).catch((err) => reportRpcError("project.saveProject", err));
      useProjectStore.getState().addRecentProject(path);
      await useProjectStore.getState().syncDirtyFlag(rpc);
      await useProjectStore.getState().syncSnapshot(rpc);
    }
  };

  const handleImportAudio = () => {
    setOpen(false);
    setImportMode("audio");
  };

  const handleImportMidi = () => {
    setOpen(false);
    setImportMode("midi");
  };

  const handleExportAudio = () => {
    setOpen(false);
    setShowExport(true);
  };

  useEffect(() => {
    const handler = (e: KeyboardEvent) => {
      if (e.target instanceof HTMLInputElement || e.target instanceof HTMLTextAreaElement) return;
      const ctrl = e.ctrlKey || e.metaKey;
      if (ctrl && e.code === "KeyN" && !e.shiftKey) { e.preventDefault(); handleNew(); }
      else if (ctrl && e.code === "KeyO" && !e.shiftKey) { e.preventDefault(); handleOpen(); }
      else if (ctrl && e.code === "KeyS" && !e.shiftKey) { e.preventDefault(); handleSave(); }
      else if (ctrl && e.shiftKey && e.code === "KeyS") { e.preventDefault(); handleSaveAs(); }
      else if (ctrl && e.shiftKey && e.code === "KeyI") { e.preventDefault(); handleImportAudio(); }
      else if (ctrl && e.shiftKey && e.code === "KeyM") { e.preventDefault(); handleImportMidi(); }
      else if (ctrl && e.code === "KeyE" && !e.shiftKey) { e.preventDefault(); handleExportAudio(); }
    };
    window.addEventListener("keydown", handler);
    return () => window.removeEventListener("keydown", handler);
  }, [filePath]);

  return (
    <div className="file-menu" ref={ref}>
      <button className="fm-trigger" onClick={() => setOpen(!open)}>File</button>
      {open && (
        <div className="fm-dropdown">
          <button onClick={handleNew}>
            <span>New Project</span>
            <span className="fm-shortcut">Ctrl+N</span>
          </button>
          <button onClick={handleOpen}>
            <span>Open...</span>
            <span className="fm-shortcut">Ctrl+O</span>
          </button>
          <div
            className="fm-submenu-item"
            ref={recentRef}
            onMouseEnter={() => setRecentOpen(true)}
            onMouseLeave={() => setRecentOpen(false)}
          >
            <button>
              <span>Open Recent</span>
              <span className="fm-arrow">&#9654;</span>
            </button>
            {recentOpen && (
              <div className="fm-submenu">
                {recentProjects.length === 0 && (
                  <button className="fm-disabled" disabled>No recent projects</button>
                )}
                {recentProjects.map((p) => (
                  <button key={p} onClick={() => handleOpenRecent(p)} title={p}>
                    {p.split(/[/\\]/).pop() ?? p}
                  </button>
                ))}
                {recentProjects.length > 0 && (
                  <button className="fm-clear" onClick={handleClearRecent}>Clear Recent</button>
                )}
              </div>
            )}
          </div>
          <div className="fm-separator" />
          <button onClick={handleSave}>
            <span>Save</span>
            <span className="fm-shortcut">Ctrl+S</span>
          </button>
          <button onClick={handleSaveAs}>
            <span>Save As...</span>
            <span className="fm-shortcut">Ctrl+Shift+S</span>
          </button>
          <div className="fm-separator" />
          <button onClick={handleImportAudio}>
            <span>Import Audio...</span>
            <span className="fm-shortcut">Ctrl+Shift+I</span>
          </button>
          <button onClick={handleImportMidi}>
            <span>Import MIDI...</span>
            <span className="fm-shortcut">Ctrl+Shift+M</span>
          </button>
          <button onClick={handleExportAudio}>
            <span>Export Audio...</span>
            <span className="fm-shortcut">Ctrl+E</span>
          </button>
        </div>
      )}
      {importMode && (
        <ImportDialog
          mode={importMode}
          onClose={() => setImportMode(null)}
          onImport={() => {
            doAction(async () => {});
          }}
        />
      )}
      {showExport && (
        <ExportDialog onClose={() => setShowExport(false)} />
      )}
    </div>
  );
}
