import { useState, useEffect, useCallback, useMemo, useRef } from "react";
import { useBrowserStore, type FileKindFilter } from "../store/browserStore";
import { rpc } from "../rpc";
import "./PopUpBrowser.css";

export interface ContentItem {
  path: string;
  name: string;
  kind: FileKindFilter;
}

export interface PopUpBrowserProps {
  onSelect: (item: ContentItem) => void;
  onClose: () => void;
  context?: "clip" | "track" | "device";
}

const AUDIO_EXTS = [".wav", ".aiff", ".aif", ".mp3", ".flac", ".ogg"];
const MIDI_EXTS = [".mid", ".midi"];
const DEVICE_EXTS = [".vst3", ".clap", ".dll"];
const PRESET_EXTS = [".fxp", ".fxb", ".vstpreset"];

function isAudio(name: string) {
  const ext = "." + name.split(".").pop()?.toLowerCase();
  return AUDIO_EXTS.includes(ext);
}

function getKind(name: string): FileKindFilter {
  if (isAudio(name)) return "samples";
  const ext = "." + name.split(".").pop()?.toLowerCase();
  if (MIDI_EXTS.includes(ext)) return "midi";
  if (DEVICE_EXTS.includes(ext)) return "devices";
  if (PRESET_EXTS.includes(ext)) return "presets";
  if (ext === ".clips") return "clips";
  return "samples";
}

function isSupported(name: string) {
  const ext = "." + name.split(".").pop()?.toLowerCase();
  return [...AUDIO_EXTS, ...MIDI_EXTS, ...DEVICE_EXTS, ...PRESET_EXTS].includes(ext);
}

function KindIcon({ kind }: { kind: FileKindFilter }) {
  switch (kind) {
    case "samples": return <span className="pb-icon pb-icon--audio">&#9834;</span>;
    case "midi": return <span className="pb-icon pb-icon--midi">&#9835;</span>;
    case "devices": return <span className="pb-icon pb-icon--device">&#9881;</span>;
    case "presets": return <span className="pb-icon pb-icon--preset">&#9733;</span>;
    case "clips": return <span className="pb-icon pb-icon--clips">&#9654;</span>;
    default: return <span className="pb-icon">&#128196;</span>;
  }
}

const CONTEXT_KINDS: Record<string, FileKindFilter[]> = {
  clip: ["samples", "clips"],
  track: ["samples", "midi", "clips"],
  device: ["devices", "presets"],
};

const KIND_CHIPS: { label: string; value: FileKindFilter }[] = [
  { label: "All", value: "all" },
  { label: "Samples", value: "samples" },
  { label: "MIDI", value: "midi" },
  { label: "Devices", value: "devices" },
  { label: "Presets", value: "presets" },
  { label: "Clips", value: "clips" },
];

export default function PopUpBrowser({ onSelect, onClose, context }: PopUpBrowserProps) {
  const folders = useBrowserStore((s) => s.folders);
  const [searchQuery, setSearchQuery] = useState("");
  const [kindFilter, setKindFilter] = useState<FileKindFilter>("all");
  const [items, setItems] = useState<ContentItem[]>([]);
  const [loading, setLoading] = useState(false);
  const [selectedIndex, setSelectedIndex] = useState(0);
  const searchRef = useRef<HTMLInputElement>(null);
  const listRef = useRef<HTMLDivElement>(null);

  // Scan all folders and collect supported files
  const scanFolders = useCallback(async () => {
    if (!window.hdaw) return;
    setLoading(true);
    const collected: ContentItem[] = [];

    const walk = async (dirPath: string) => {
      try {
        const entries = await window.hdaw.readDirectory(dirPath);
        for (const entry of entries) {
          if (entry.isDir) {
            await walk(entry.path);
          } else if (isSupported(entry.name)) {
            collected.push({ path: entry.path, name: entry.name, kind: getKind(entry.name) });
          }
        }
      } catch {
        // Directory may not be accessible
      }
    };

    for (const folder of folders) {
      await walk(folder);
    }

    setItems(collected);
    setLoading(false);
  }, [folders]);

  useEffect(() => {
    scanFolders();
  }, [scanFolders]);

  // Focus search on mount
  useEffect(() => {
    searchRef.current?.focus();
  }, []);

  // Close on Escape
  useEffect(() => {
    const handler = (e: KeyboardEvent) => {
      if (e.key === "Escape") {
        e.preventDefault();
        onClose();
      }
    };
    window.addEventListener("keydown", handler);
    return () => window.removeEventListener("keydown", handler);
  }, [onClose]);

  // Determine which kinds are relevant for the context
  const allowedKinds = context ? CONTEXT_KINDS[context] : null;

  const filteredItems = useMemo(() => {
    let result = items;

    // Context pre-filter
    if (allowedKinds) {
      result = result.filter((item) => allowedKinds.includes(item.kind));
    }

    // Kind chip filter
    if (kindFilter !== "all") {
      result = result.filter((item) => item.kind === kindFilter);
    }

    // Search filter
    if (searchQuery) {
      const q = searchQuery.toLowerCase();
      result = result.filter((item) => item.name.toLowerCase().includes(q));
    }

    return result;
  }, [items, allowedKinds, kindFilter, searchQuery]);

  // Reset selected index when filter changes
  useEffect(() => {
    setSelectedIndex(0);
  }, [filteredItems.length]);

  const handleKeyDown = useCallback((e: React.KeyboardEvent) => {
    if (e.key === "ArrowDown") {
      e.preventDefault();
      setSelectedIndex((prev) => Math.min(prev + 1, filteredItems.length - 1));
    } else if (e.key === "ArrowUp") {
      e.preventDefault();
      setSelectedIndex((prev) => Math.max(prev - 1, 0));
    } else if (e.key === "Enter") {
      e.preventDefault();
      const item = filteredItems[selectedIndex];
      if (item) onSelect(item);
    }
  }, [filteredItems, selectedIndex, onSelect]);

  // Scroll selected item into view
  useEffect(() => {
    const el = listRef.current?.children[selectedIndex] as HTMLElement | undefined;
    if (el && typeof el.scrollIntoView === "function") {
      el.scrollIntoView({ block: "nearest" });
    }
  }, [selectedIndex]);

  const handleBackdropClick = useCallback((e: React.MouseEvent) => {
    if (e.target === e.currentTarget) onClose();
  }, [onClose]);

  // Available chips for the context
  const visibleChips = allowedKinds
    ? KIND_CHIPS.filter((c) => c.value === "all" || allowedKinds.includes(c.value))
    : KIND_CHIPS;

  const handlePreview = useCallback(async (filePath: string) => {
    try {
      await rpc.call("preview.load", { filePath });
      await rpc.call("preview.play");
    } catch {
      // Preview may fail for non-audio files
    }
  }, []);

  return (
    <div className="pb-backdrop" onMouseDown={handleBackdropClick}>
      <div className="pb-dialog" onKeyDown={handleKeyDown}>
        <div className="pb-header">
          <span className="pb-title">
            {context === "clip" ? "Add Clip" : context === "device" ? "Add Device" : "Browse Files"}
          </span>
          <button className="pb-close" onClick={onClose} title="Close">&#10005;</button>
        </div>
        <div className="pb-search-row">
          <input
            ref={searchRef}
            type="text"
            className="pb-search"
            placeholder="Search..."
            value={searchQuery}
            onChange={(e) => setSearchQuery(e.target.value)}
          />
        </div>
        <div className="pb-chips">
          {visibleChips.map((chip) => (
            <button
              key={chip.value}
              className={`pb-chip${kindFilter === chip.value ? " pb-chip--active" : ""}`}
              onClick={() => setKindFilter(kindFilter === chip.value ? "all" : chip.value)}
            >
              {chip.label}
            </button>
          ))}
        </div>
        <div className="pb-list" ref={listRef}>
          {loading && <div className="pb-loading">Scanning...</div>}
          {!loading && filteredItems.length === 0 && (
            <div className="pb-empty">
              {items.length === 0
                ? "No files found. Add folders in the Browser panel."
                : "No matching files."}
            </div>
          )}
          {!loading && filteredItems.map((item, idx) => (
            <div
              key={item.path}
              className={`pb-item${idx === selectedIndex ? " pb-item--selected" : ""}`}
              onClick={() => onSelect(item)}
              onMouseEnter={() => setSelectedIndex(idx)}
            >
              <KindIcon kind={item.kind} />
              <span className="pb-item-name">{item.name}</span>
              <span className="pb-item-kind">{item.kind}</span>
              {item.kind === "samples" && (
                <button
                  className="pb-preview-btn"
                  title="Preview"
                  onClick={(e) => {
                    e.stopPropagation();
                    handlePreview(item.path);
                  }}
                >
                  &#9654;
                </button>
              )}
            </div>
          ))}
        </div>
        <div className="pb-footer">
          <span className="pb-hint">{filteredItems.length} items</span>
          <span className="pb-hint">Enter to select &middot; Esc to close</span>
        </div>
      </div>
    </div>
  );
}
