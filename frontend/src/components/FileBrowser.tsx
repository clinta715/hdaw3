import { useState, useEffect, useCallback, useMemo, useRef } from "react";
import { useBrowserStore, type FileKindFilter } from "../store/browserStore";
import { useLibraryStore, type LibraryEntry } from "../store/libraryStore";
import { useUiStore } from "../store/uiStore";
import { useProjectStore } from "../store/projectStore";
import { useTransportStore } from "../store/transportStore";
import { reportRpcError } from "../store/notifyStore";
import { rpc } from "../rpc";
import PoolView from "./PoolView";
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

const DEVICE_EXTS = [".vst3", ".clap", ".dll"];
const PRESET_EXTS = [".fxp", ".fxb", ".vstpreset", ".syx"];

function fileKind(name: string): FileKindFilter | null {
  if (isAudio(name)) return "samples";
  if (isMidi(name)) return "midi";
  const ext = "." + name.split(".").pop()?.toLowerCase();
  if (DEVICE_EXTS.includes(ext)) return "devices";
  if (PRESET_EXTS.includes(ext)) return "presets";
  if (ext === ".clips") return "clips";
  return null;
}

function formatDuration(seconds: number): string {
  if (!seconds || seconds <= 0) return "—";
  const m = Math.floor(seconds / 60);
  const s = Math.floor(seconds % 60);
  return `${m}:${s.toString().padStart(2, "0")}`;
}

function formatSize(bytes: number): string {
  if (!bytes || bytes <= 0) return "—";
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
}

const KIND_CHIPS: { label: string; value: FileKindFilter }[] = [
  { label: "All", value: "all" },
  { label: "Samples", value: "samples" },
  { label: "MIDI", value: "midi" },
  { label: "Devices", value: "devices" },
  { label: "Presets", value: "presets" },
  { label: "Clips", value: "clips" },
  { label: "Library", value: "library" },
  { label: "Pool", value: "pool" },
];

function FilterChips() {
  const kindFilter = useBrowserStore((s) => s.kindFilter);
  const setKindFilter = useBrowserStore((s) => s.setKindFilter);

  return (
    <div className="fb-filter-chips">
      {KIND_CHIPS.map((chip) => (
        <button
          key={chip.value}
          className={`fb-chip${kindFilter === chip.value ? " fb-chip--active" : ""}`}
          onClick={() => setKindFilter(kindFilter === chip.value ? "all" : chip.value)}
        >
          {chip.label}
        </button>
      ))}
    </div>
  );
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
  const kindFilter = useBrowserStore((s) => s.kindFilter);
  const selectedFile = useBrowserStore((s) => s.selectedFile);
  const setSelectedFile = useBrowserStore((s) => s.setSelectedFile);
  const removeFolder = useBrowserStore((s) => s.removeFolder);
  const addFavorite = useBrowserStore((s) => s.addFavorite);
  const favorites = useBrowserStore((s) => s.favorites);
  const autoPreview = useBrowserStore((s) => s.autoPreview);
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
    if (!query && kindFilter === "all") return children.filter((c) => c.isDir || isSupported(c.name));
    return children.filter((c) => {
      if (c.isDir) return true;
      if (!isSupported(c.name)) return false;
      if (query && !c.name.toLowerCase().includes(query)) return false;
      if (kindFilter !== "all") {
        const kind = fileKind(c.name);
        if (kind !== kindFilter) return false;
      }
      return true;
    });
  }, [children, query, kindFilter]);

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
    // The new clip is reconciled by the debounced notify.treeChanged push.
    useProjectStore.setState({ isDirty: true });
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
        <span className="fb-tree-name">{entry.name}</span>
      </div>
      {contextMenu && isRoot && (
        <>
          <div className="fb-context-overlay" onClick={handleContextMenuClose} onContextMenu={handleContextMenuClose} />
          <div className="fb-context-menu" style={{ left: contextMenu.x, top: contextMenu.y }}>
            {!favorites.some((f) => f.path === entry.path) && (
              <button onClick={() => { addFavorite(entry.path); setContextMenu(null); }}>&#9733; Add to Favorites</button>
            )}
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
            onClick={() => {
              setSelectedFile(child.path);
              if (autoPreview && isAudio(child.name)) {
                onPreviewFile(child.path, child.name);
              }
            }}
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

function LibraryView() {
  const libraries = useLibraryStore((s) => s.libraries);
  const searchResults = useLibraryStore((s) => s.searchResults);
  const scanProgress = useLibraryStore((s) => s.scanProgress);
  const loadLibraries = useLibraryStore((s) => s.loadLibraries);
  const scanLibrary = useLibraryStore((s) => s.scanLibrary);
  const scanAll = useLibraryStore((s) => s.scanAll);
  const search = useLibraryStore((s) => s.search);
  const setSearchQuery = useLibraryStore((s) => s.setSearchQuery);

  const [input, setInput] = useState("");
  const [selectedPath, setSelectedPath] = useState<string | null>(null);
  const [autoPlay, setAutoPlay] = useState(false);
  const debounceRef = useRef<ReturnType<typeof setTimeout> | null>(null);

  // Load configured libraries on mount. loadLibraries is a stable action ref.
  useEffect(() => {
    loadLibraries(rpc).catch(() => {});
  }, [loadLibraries]);

  // Debounce the search input so typing doesn't fire an RPC per keystroke —
  // mirrors the volume-debounce pattern above.
  useEffect(() => {
    if (debounceRef.current) clearTimeout(debounceRef.current);
    debounceRef.current = setTimeout(() => {
      setSearchQuery(input);
      search(input, {}, rpc).catch(() => {});
    }, 200);
    return () => {
      if (debounceRef.current) clearTimeout(debounceRef.current);
    };
  }, [input, setSearchQuery, search]);

  const handleDragStart = useCallback((e: React.DragEvent, entry: LibraryEntry) => {
    // Match the FileBrowser drag contract ({ path, name }) so the timeline
    // drop handler works unchanged.
    e.dataTransfer.setData("application/hdaw-file", JSON.stringify({ path: entry.path, name: entry.name }));
    e.dataTransfer.effectAllowed = "copy";
  }, []);

  if (libraries.length === 0) {
    return (
      <div className="fb-library-empty">
        No libraries configured. Add one in Preferences.
      </div>
    );
  }

  return (
    <div className="fb-library">
      <div className="fb-library-header">
        <button
          className="fb-library-scan-btn"
          onClick={() => { scanAll(rpc); }}
        >
          Rescan All
        </button>
        <label className="fb-library-autoplay">
          <input type="checkbox" checked={autoPlay} onChange={(e) => setAutoPlay(e.target.checked)} />
          Auto-play
        </label>
        <div className="fb-library-search">
          <input
            type="text"
            placeholder="Search library..."
            value={input}
            onChange={(e) => setInput(e.target.value)}
            className="fb-search-input"
          />
        </div>
      </div>
      <div className="fb-library-list">
        {libraries.map((lib) => {
          const prog = scanProgress[lib.id];
          return (
            <div key={lib.id} className="fb-library-node">
              <span className="fb-library-name" title={lib.path}>{lib.name}</span>
              <span className="fb-library-count">{lib.fileCount} files</span>
              {prog ? (
                <span className="fb-scan-progress">{prog.scanned}/{prog.total}</span>
              ) : null}
              <button
                className="fb-library-scan-btn"
                onClick={() => { scanLibrary(lib.id, rpc); }}
              >
                Scan
              </button>
            </div>
          );
        })}
      </div>
      {searchResults.length > 0 && (
        <div className="fb-library-results">
          <div className="fb-library-columns">
            <span>Name</span>
            <span>Duration</span>
            <span>BPM</span>
            <span>Key</span>
            <span>Size</span>
          </div>
          {searchResults.map((entry) => (
            <div
              key={entry.path}
              className={`fb-library-entry${selectedPath === entry.path ? " fb-library-entry--selected" : ""}`}
              draggable
              onDragStart={(e) => handleDragStart(e, entry)}
              onClick={() => {
                setSelectedPath(entry.path);
                if (autoPlay && entry.format && ["wav", "aiff", "aif", "mp3", "flac", "ogg"].includes(entry.format)) {
                  rpc.call("preview.load", { filePath: entry.path }).then(() => {
                    return rpc.call("preview.play");
                  }).catch(() => {});
                }
              }}
            >
              <span className="fb-library-name">{entry.name}</span>
              <span>{formatDuration(entry.durationSeconds)}</span>
              <span>{entry.bpm ? String(entry.bpm) : "—"}</span>
              <span>{entry.key || "—"}</span>
              <span>{formatSize(entry.size)}</span>
            </div>
          ))}
        </div>
      )}
    </div>
  );
}

export default function FileBrowser() {
  const folders = useBrowserStore((s) => s.folders);
  const favorites = useBrowserStore((s) => s.favorites);
  const kindFilter = useBrowserStore((s) => s.kindFilter);
  const searchQuery = useBrowserStore((s) => s.searchQuery);
  const setSearchQuery = useBrowserStore((s) => s.setSearchQuery);
  const addFolder = useBrowserStore((s) => s.addFolder);
  const removeFavorite = useBrowserStore((s) => s.removeFavorite);
  const moveFavorite = useBrowserStore((s) => s.moveFavorite);
  const visible = useBrowserStore((s) => s.visible);
  const selectedFile = useBrowserStore((s) => s.selectedFile);
  const autoPreview = useBrowserStore((s) => s.autoPreview);
  const setAutoPreview = useBrowserStore((s) => s.setAutoPreview);
  const tempoMatch = useBrowserStore((s) => s.tempoMatch);
  const setTempoMatch = useBrowserStore((s) => s.setTempoMatch);
  const sourceBpm = useBrowserStore((s) => s.sourceBpm);
  const setSourceBpm = useBrowserStore((s) => s.setSourceBpm);
  const bpm = useTransportStore((s) => s.transport.bpm);

  const [isPlaying, setIsPlaying] = useState(false);
  const [volume, setVolume] = useState(0.8);
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
    // Stop any in-flight preview first. Without this, clicking a second
    // file's ▶ while the first load is still resolving interleaves two
    // load/play sequences on the engine, leaving isPlaying/previewFile and
    // the engine's player in an inconsistent state. (stopPreview is safe
    // to call even if nothing is loaded — preview.stop is best-effort.)
    await stopPreview();
    try {
      await rpc.call("preview.load", { filePath });
      await rpc.call("preview.setVolume", { volume });
      // When tempo match is enabled, time-stretch the preview to the project tempo.
      if (tempoMatch) {
        await rpc.call("preview.setTempoMatch", { enabled: true, fileBpm: sourceBpm });
        await rpc.call("preview.setProjectBpm", { bpm });
      } else {
        await rpc.call("preview.setTempoMatch", { enabled: false });
      }
      await rpc.call("preview.play");
    } catch (err) {
      // A rejected preview.load (e.g. unsupported format) used to throw an
      // unhandled rejection and leave the engine in an unknown state with
      // no user-visible signal. Surface it as a toast instead.
      reportRpcError("preview.load", err);
      setIsPlaying(false);
      return;
    }
    setIsPlaying(true);
    setPreviewFile({ path: filePath, name: fileName });

    // Poll for playback state to auto-stop
    if (pollRef.current) clearInterval(pollRef.current);
    pollRef.current = setInterval(async () => {
      try {
        const playing = await rpc.call("preview.isPlaying") as boolean;
        if (!playing) {
          setIsPlaying(false);
          if (pollRef.current) {
            clearInterval(pollRef.current);
            pollRef.current = null;
          }
        }
      } catch {
        // Engine gone away or call failed — stop polling to avoid a tight error loop.
        if (pollRef.current) {
          clearInterval(pollRef.current);
          pollRef.current = null;
        }
        setIsPlaying(false);
      }
    }, 500);
  }, [volume, tempoMatch, sourceBpm, bpm, stopPreview]);

  const handlePreviewFile = useCallback(async (filePath: string, fileName: string) => {
    if (isPlaying && previewFile?.path === filePath) {
      await stopPreview();
    } else {
      await playPreview(filePath, fileName);
    }
  }, [isPlaying, previewFile, playPreview, stopPreview]);

  // Sync volume while dragging — debounce so a continuous slider drag doesn't
  // fire dozens of preview.setVolume RPCs per second into the engine.
  useEffect(() => {
    if (!isPlaying) return;
    const handle = setTimeout(() => {
      rpc.call("preview.setVolume", { volume }).catch(() => {});
    }, 60);
    return () => clearTimeout(handle);
  }, [volume, isPlaying]);

  // Sync tempo-match settings. These change discretely (checkbox/spin), so no
  // debounce needed.
  useEffect(() => {
    if (!isPlaying) return;
    if (tempoMatch) {
      rpc.call("preview.setTempoMatch", { enabled: true, fileBpm: sourceBpm }).catch(() => {});
      rpc.call("preview.setProjectBpm", { bpm }).catch(() => {});
    } else {
      rpc.call("preview.setTempoMatch", { enabled: false }).catch(() => {});
    }
  }, [tempoMatch, sourceBpm, bpm, isPlaying]);

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
      <FilterChips />
      {kindFilter === "library" ? (
        <LibraryView />
      ) : kindFilter === "pool" ? (
        <PoolView />
      ) : (
        <>
          {favorites.length > 0 && (
            <div className="fb-favorites">
              <div className="fb-favorites-header">
                <span className="fb-favorites-title">&#9733; Favorites</span>
              </div>
              <div className="fb-favorites-list">
                {favorites.map((fav, idx) => (
                  <div key={fav.path} className="fb-favorite-item">
                    <button
                      className="fb-favorite-btn"
                      title={fav.path}
                      onClick={() => {
                        // Expand the folder in the tree
                        useBrowserStore.getState().toggleExpanded(fav.path);
                      }}
                    >
                      <span className="fb-favorite-icon">&#128193;</span>
                      <span className="fb-favorite-label">{fav.label}</span>
                    </button>
                    <div className="fb-favorite-actions">
                      {idx > 0 && (
                        <button
                          className="fb-favorite-move"
                          title="Move up"
                          onClick={() => moveFavorite(idx, idx - 1)}
                        >
                          &#9650;
                        </button>
                      )}
                      {idx < favorites.length - 1 && (
                        <button
                          className="fb-favorite-move"
                          title="Move down"
                          onClick={() => moveFavorite(idx, idx + 1)}
                        >
                          &#9660;
                        </button>
                      )}
                      <button
                        className="fb-favorite-remove"
                        title="Remove from favorites"
                        onClick={() => removeFavorite(fav.path)}
                      >
                        &#10005;
                      </button>
                    </div>
                  </div>
                ))}
              </div>
            </div>
          )}
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
        </>
      )}
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
              checked={autoPreview}
              onChange={(e) => setAutoPreview(e.target.checked)}
            />
            <span>Auto-play</span>
          </label>
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
