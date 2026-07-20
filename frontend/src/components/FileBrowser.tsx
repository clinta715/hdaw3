import { useState, useEffect, useCallback, useMemo, useRef } from "react";
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
  if (isMidi(name)) return <span className="fb-icon fb-icon--midi">&#9835;</span>;
  if (isAudio(name)) return <span className="fb-icon fb-icon--audio">&#9834;</span>;
  return <span className="fb-icon">&#128196;</span>;
}

function FolderNode({ entry, depth, onPreviewFile }: { entry: DirEntry; depth: number; onPreviewFile: (path: string, name: string) => void }) {
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

  const loadChildren = useCallback(async () => {
    if (!window.hdaw) return;
    const entries = await window.hdaw.readDirectory(entry.path);
    const sorted = entries.sort((a, b) => {
      if (a.isDir !== b.isDir) return a.isDir ? -1 : 1;
      return a.name.localeCompare(b.name);
    });
    setChildren(sorted);
    setLoaded(true);
  }, [entry.path]);

  useEffect(() => {
    if (isExpanded && !loaded) {
      loadChildren().catch(console.error);
    }
  }, [isExpanded, loaded, loadChildren]);

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
    if (!loaded && !isExpanded) {
      loadChildren().catch(console.error);
    }
  }, [entry.path, loaded, isExpanded, toggleExpanded, loadChildren]);

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
        <span className={`fb-arrow ${isExpanded ? "fb-arrow--expanded" : ""}`}>&#9654;</span>
        <span className="fb-folder-icon">&#128193;</span>
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
          <FolderNode key={child.path} entry={child} depth={depth + 1} onPreviewFile={onPreviewFile} />
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
            {isAudio(child.name) && (
              <button
                className="fb-preview-btn"
                title="Preview at project tempo"
                onClick={(e) => {
                  e.stopPropagation();
                  onPreviewFile(child.path, child.name);
                }}
              >
                &#9654;
              </button>
            )}
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
  const selectedFile = useBrowserStore((s) => s.selectedFile);
  const bpm = useTransportStore((s) => s.transport.bpm);

  const [isPlaying, setIsPlaying] = useState(false);
  const [volume, setVolume] = useState(0.8);
  const [tempoMatch, setTempoMatch] = useState(true);
  const [sourceBpm, setSourceBpm] = useState(120);
  const [previewFile, setPreviewFile] = useState<{ path: string; name: string } | null>(null);
  const pollRef = useRef<ReturnType<typeof setInterval> | null>(null);

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

  const stopPreview = useCallback(async () => {
    await rpc.call("preview.stop").catch(() => {});
    setIsPlaying(false);
    if (pollRef.current) {
      clearInterval(pollRef.current);
      pollRef.current = null;
    }
  }, []);

  const playPreview = useCallback(async (filePath: string, fileName: string) => {
    await rpc.call("preview.load", { filePath });
    await rpc.call("preview.setVolume", { volume });
    if (tempoMatch) {
      await rpc.call("preview.setTempoMatch", { enabled: true, fileBpm: sourceBpm });
      await rpc.call("preview.setProjectBpm", { bpm });
    } else {
      await rpc.call("preview.setTempoMatch", { enabled: false });
    }
    await rpc.call("preview.play");
    setIsPlaying(true);
    setPreviewFile({ path: filePath, name: fileName });

    // Poll for playback state to auto-stop
    if (pollRef.current) clearInterval(pollRef.current);
    pollRef.current = setInterval(async () => {
      const playing = await rpc.call("preview.isPlaying") as boolean;
      if (!playing) {
        setIsPlaying(false);
        if (pollRef.current) {
          clearInterval(pollRef.current);
          pollRef.current = null;
        }
      }
    }, 500);
  }, [volume, tempoMatch, sourceBpm, bpm]);

  const handlePreviewFile = useCallback(async (filePath: string, fileName: string) => {
    if (isPlaying && previewFile?.path === filePath) {
      await stopPreview();
    } else {
      await playPreview(filePath, fileName);
    }
  }, [isPlaying, previewFile, playPreview, stopPreview]);

  // Sync tempo match settings when they change
  useEffect(() => {
    if (isPlaying) {
      rpc.call("preview.setVolume", { volume }).catch(() => {});
      if (tempoMatch) {
        rpc.call("preview.setTempoMatch", { enabled: true, fileBpm: sourceBpm }).catch(() => {});
        rpc.call("preview.setProjectBpm", { bpm }).catch(() => {});
      } else {
        rpc.call("preview.setTempoMatch", { enabled: false }).catch(() => {});
      }
    }
  }, [volume, tempoMatch, sourceBpm, bpm, isPlaying]);

  // Cleanup on unmount
  useEffect(() => {
    return () => {
      if (pollRef.current) clearInterval(pollRef.current);
      rpc.call("preview.stop").catch(() => {});
    };
  }, []);

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
          <FolderNode key={entry.path} entry={entry} depth={0} onPreviewFile={handlePreviewFile} />
        ))}
      </div>
      <div className="fb-preview-bar">
        <div className="fb-preview-controls">
          <button
            className={`fb-preview-play${isPlaying ? " fb-preview-play--active" : ""}`}
            onClick={() => {
              if (isPlaying) {
                stopPreview();
              } else if (previewFile) {
                playPreview(previewFile.path, previewFile.name);
              }
            }}
            disabled={!previewFile && !isPlaying}
            title={isPlaying ? "Stop" : "Play"}
          >
            {isPlaying ? "&#9632;" : "&#9654;"}
          </button>
          <input
            type="range"
            className="fb-volume-slider"
            min="0"
            max="1"
            step="0.01"
            value={volume}
            onChange={(e) => setVolume(parseFloat(e.target.value))}
            title={`Volume: ${Math.round(volume * 100)}%`}
          />
        </div>
        <div className="fb-preview-options">
          <label className="fb-tempo-match-label">
            <input
              type="checkbox"
              checked={tempoMatch}
              onChange={(e) => setTempoMatch(e.target.checked)}
            />
            <span>Tempo Match</span>
          </label>
          {tempoMatch && (
            <div className="fb-source-bpm">
              <label>Src BPM:</label>
              <input
                type="number"
                className="fb-bpm-input"
                min="20"
                max="300"
                value={sourceBpm}
                onChange={(e) => setSourceBpm(parseFloat(e.target.value) || 120)}
              />
            </div>
          )}
        </div>
        {previewFile && (
          <div className="fb-preview-info" title={previewFile.path}>
            {previewFile.name}
          </div>
        )}
      </div>
    </div>
  );
}
