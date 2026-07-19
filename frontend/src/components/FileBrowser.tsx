import { useState, useEffect, useCallback, useMemo } from "react";
import { useBrowserStore } from "../store/browserStore";
import { useUiStore } from "../store/uiStore";
import { useProjectStore } from "../store/projectStore";
import { useTransportStore } from "../store/transportStore";
import { rpc } from "../rpc";
import "./FileBrowser.css";

interface DirEntry {
  name: string;
  isDir: boolean;
  path: string;
}

const AUDIO_EXTS = [".wav", ".aiff", ".aif", ".mp3", ".flac", ".ogg"];
const MIDI_EXTS = [".mid", ".midi"];
const ALL_EXTS = [...AUDIO_EXTS, ...MIDI_EXTS];

function isAudio(name: string) {
  const ext = "." + name.split(".").pop()?.toLowerCase();
  return AUDIO_EXTS.includes(ext);
}

function isMidi(name: string) {
  const ext = "." + name.split(".").pop()?.toLowerCase();
  return MIDI_EXTS.includes(ext);
}

function isSupported(name: string) {
  const ext = "." + name.split(".").pop()?.toLowerCase();
  return ALL_EXTS.includes(ext);
}

function FileIcon({ name }: { name: string }) {
  if (isMidi(name)) return <span className="fb-icon fb-icon--midi">♫</span>;
  if (isAudio(name)) return <span className="fb-icon fb-icon--audio">♪</span>;
  return <span className="fb-icon">📄</span>;
}

function FolderNode({ entry, depth }: { entry: DirEntry; depth: number }) {
  const expandedPaths = useBrowserStore((s) => s.expandedPaths);
  const toggleExpanded = useBrowserStore((s) => s.toggleExpanded);
  const searchQuery = useBrowserStore((s) => s.searchQuery);
  const selectedFile = useBrowserStore((s) => s.selectedFile);
  const setSelectedFile = useBrowserStore((s) => s.setSelectedFile);
  const removeFolder = useBrowserStore((s) => s.removeFolder);
  const [children, setChildren] = useState<DirEntry[]>([]);
  const [loaded, setLoaded] = useState(false);
  const [contextMenu, setContextMenu] = useState<{ x: number; y: number } | null>(null);

  const isExpanded = expandedPaths.has(entry.path);
  const isRoot = depth === 0;
  const query = searchQuery.toLowerCase();

  useEffect(() => {
    if (isExpanded && !loaded && window.hdaw) {
      window.hdaw.readDirectory(entry.path).then((entries) => {
        const sorted = entries.sort((a, b) => {
          if (a.isDir !== b.isDir) return a.isDir ? -1 : 1;
          return a.name.localeCompare(b.name);
        });
        setChildren(sorted);
        setLoaded(true);
      });
    }
  }, [isExpanded, loaded, entry.path]);

  const filteredChildren = useMemo(() => {
    if (!query) return children.filter((c) => c.isDir || isSupported(c.name));
    return children.filter((c) => {
      if (c.isDir) return true;
      if (!isSupported(c.name)) return false;
      return c.name.toLowerCase().includes(query);
    });
  }, [children, query]);

  const handleToggle = useCallback(() => {
    toggleExpanded(entry.path);
    if (!loaded && !isExpanded && window.hdaw) {
      window.hdaw.readDirectory(entry.path).then((entries) => {
        const sorted = entries.sort((a, b) => {
          if (a.isDir !== b.isDir) return a.isDir ? -1 : 1;
          return a.name.localeCompare(b.name);
        });
        setChildren(sorted);
        setLoaded(true);
      });
    }
  }, [entry.path, loaded, isExpanded, toggleExpanded]);

  const handleDoubleClick = useCallback(async () => {
    if (!isRoot) return;
    // Root folders don't double-click to import
  }, [isRoot]);

  const handleFileDoubleClick = useCallback(async (filePath: string, fileName: string) => {
    const tr = useTransportStore.getState().transport;
    const selectedTrack = useUiStore.getState().selectedTrackIndex ?? 0;
    const startBeat = tr.currentTimeSeconds * (tr.bpm / 60);

    if (isAudio(fileName)) {
      await rpc.call("project.addAudioClip", {
        trackIndex: selectedTrack,
        start: startBeat,
        duration: 4,
        sourceFile: filePath,
        name: fileName,
      }).catch(() => {});
    } else if (isMidi(fileName)) {
      await rpc.call("project.addMidiClip", {
        trackIndex: selectedTrack,
        start: startBeat,
        duration: 4,
        name: fileName,
      }).catch(() => {});
    }
    await useProjectStore.getState().syncSnapshot(rpc);
  }, []);

  const handleDragStart = useCallback((e: React.DragEvent, filePath: string, fileName: string) => {
    e.dataTransfer.setData("application/hdaw-file", JSON.stringify({ path: filePath, name: fileName }));
    e.dataTransfer.effectAllowed = "copy";
  }, []);

  const handleContextMenuClose = useCallback(() => setContextMenu(null), []);

  return (
    <div className="fb-tree-node">
      <div
        className={`fb-tree-row fb-tree-row--dir${isRoot ? " fb-tree-row--root" : ""}`}
        style={{ paddingLeft: depth * 16 }}
        onClick={handleToggle}
        onContextMenu={(e) => {
          if (isRoot) {
            e.preventDefault();
            setContextMenu({ x: e.clientX, y: e.clientY });
          }
        }}
      >
        <span className={`fb-arrow ${isExpanded ? "fb-arrow--expanded" : ""}`}>▶</span>
        <span className="fb-folder-icon">📁</span>
        <span className="fb-tree-name">{isRoot ? entry.name : entry.name}</span>
      </div>
      {contextMenu && isRoot && (
        <>
          <div className="fb-context-overlay" onClick={handleContextMenuClose} onContextMenu={handleContextMenuClose} />
          <div className="fb-context-menu" style={{ left: contextMenu.x, top: contextMenu.y }}>
            <button onClick={() => { removeFolder(entry.path); setContextMenu(null); }}>Remove Folder</button>
          </div>
        </>
      )}
      {isExpanded && filteredChildren.map((child) => (
        child.isDir ? (
          <FolderNode key={child.path} entry={child} depth={depth + 1} />
        ) : (
          <div
            key={child.path}
            className={`fb-tree-row fb-tree-row--file${selectedFile === child.path ? " fb-tree-row--selected" : ""}`}
            style={{ paddingLeft: (depth + 1) * 16 }}
            onClick={() => setSelectedFile(child.path)}
            onDoubleClick={() => handleFileDoubleClick(child.path, child.name)}
            draggable
            onDragStart={(e) => handleDragStart(e, child.path, child.name)}
          >
            <FileIcon name={child.name} />
            <span className="fb-tree-name">{child.name}</span>
          </div>
        )
      ))}
    </div>
  );
}

export default function FileBrowser() {
  const folders = useBrowserStore((s) => s.folders);
  const searchQuery = useBrowserStore((s) => s.searchQuery);
  const setSearchQuery = useBrowserStore((s) => s.setSearchQuery);
  const addFolder = useBrowserStore((s) => s.addFolder);
  const visible = useBrowserStore((s) => s.visible);

  const handleAddFolder = useCallback(async () => {
    if (window.hdaw) {
      const result = await window.hdaw.showOpenDialog({
        title: "Add Folder to Browser",
        properties: ["openDirectory"],
      });
      if (!result.canceled && result.filePaths.length > 0) {
        addFolder(result.filePaths[0]);
      }
    }
  }, [addFolder]);

  if (!visible) return null;

  const rootEntries: DirEntry[] = folders.map((f) => ({
    name: f.split(/[\\/]/).pop() ?? f,
    isDir: true,
    path: f,
  }));

  return (
    <div className="file-browser">
      <div className="fb-header">
        <span className="fb-title">Browser</span>
        <button className="fb-add-btn" onClick={handleAddFolder} title="Add Folder">+</button>
      </div>
      <div className="fb-search">
        <input
          type="text"
          placeholder="Search files..."
          value={searchQuery}
          onChange={(e) => setSearchQuery(e.target.value)}
          className="fb-search-input"
        />
      </div>
      <div className="fb-tree">
        {rootEntries.length === 0 && (
          <div className="fb-empty">
            <p>No folders added</p>
            <button className="fb-add-first-btn" onClick={handleAddFolder}>Add a folder</button>
          </div>
        )}
        {rootEntries.map((entry) => (
          <FolderNode key={entry.path} entry={entry} depth={0} />
        ))}
      </div>
    </div>
  );
}
