import { useState, useEffect } from "react";
import { rpc } from "../rpc";
import { useProjectStore } from "../store/projectStore";
import { VERSION } from "../version";
import "./StartupDialog.css";

interface Props {
  onClose: () => void;
}

export default function StartupDialog({ onClose }: Props) {
  const [recentProjects, setRecentProjects] = useState<string[]>([]);

  useEffect(() => {
    const stored = localStorage.getItem("hdaw_recent_projects");
    if (stored) setRecentProjects(JSON.parse(stored));
  }, []);

  const handleNew = async () => {
    await rpc.call("project.newProject");
    onClose();
  };

  const handleOpen = async () => {
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
        await rpc.call("project.loadProject", { filePath: path });
        useProjectStore.getState().addRecentProject(path);
        onClose();
      }
    } else {
      // Fallback for browser dev mode
      const path = prompt("Project file path:");
      if (path) {
        await rpc.call("project.loadProject", { filePath: path });
        useProjectStore.getState().addRecentProject(path);
        onClose();
      }
    }
  };

  const handleOpenRecent = async (path: string) => {
    await rpc.call("project.loadProject", { filePath: path });
    useProjectStore.getState().addRecentProject(path);
    onClose();
  };

  return (
    <div className="modal-overlay">
      <div className="startup-dialog">
        <h1>HDAW</h1>
        <p className="startup-version">v{VERSION}</p>
        <div className="startup-actions">
          <button className="startup-btn primary" onClick={handleNew}>New Project</button>
          <button className="startup-btn" onClick={handleOpen}>Open Project...</button>
        </div>
        {recentProjects.length > 0 && (
          <div className="startup-recent">
            <h3>Recent Projects</h3>
            {recentProjects.slice(0, 8).map((p) => (
              <button key={p} className="startup-recent-item" onClick={() => handleOpenRecent(p)}>
                {p.split(/[\\/]/).pop()}
              </button>
            ))}
          </div>
        )}
      </div>
    </div>
  );
}
