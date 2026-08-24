import { useState, useEffect, useCallback, useMemo, useRef } from "react";
import { rpc } from "../rpc";
import "./PresetBrowser.css";

interface PatternEntry {
  id: string;
  name: string;
  style: string;
  category: string;
  tags: string[];
  source: string;
}

interface LoadResult {
  name: string;
  style: string;
  params: Record<string, unknown>;
  styleParams: Record<string, unknown>;
}

interface Props {
  onLoadPreset: (preset: {
    name: string;
    style: string;
    params: Record<string, unknown>;
    styleParams: Record<string, unknown>;
  }) => void;
  currentStyle: string;
}

export default function PresetBrowser({ onLoadPreset }: Props) {
  const [patterns, setPatterns] = useState<PatternEntry[]>([]);
  const [search, setSearch] = useState("");
  const [collapsed, setCollapsed] = useState(false);
  const [activeId, setActiveId] = useState<string | null>(null);
  const fileInputRef = useRef<HTMLInputElement>(null);

  useEffect(() => {
    rpc.call("composition.listPatterns", {}).then((result: unknown) => {
      if (Array.isArray(result)) setPatterns(result as PatternEntry[]);
    });
  }, []);

  const filtered = useMemo(() => {
    if (!search) return patterns;
    const lower = search.toLowerCase();
    return patterns.filter(
      (p) =>
        p.name.toLowerCase().includes(lower) ||
        p.style.toLowerCase().includes(lower) ||
        (p.tags && p.tags.some((t) => t.toLowerCase().includes(lower)))
    );
  }, [patterns, search]);

  const grouped = useMemo(() => {
    const map = new Map<string, PatternEntry[]>();
    for (const p of filtered) {
      const cat = p.category || "other";
      if (!map.has(cat)) map.set(cat, []);
      map.get(cat)!.push(p);
    }
    return map;
  }, [filtered]);

  const handleLoad = useCallback(
    async (id: string) => {
      const result: unknown = await rpc.call("composition.loadPattern", { id });
      if (result && typeof result === "object") {
        const r = result as LoadResult;
        setActiveId(id);
        onLoadPreset({
          name: r.name,
          style: r.style,
          params: r.params,
          styleParams: r.styleParams,
        });
      }
    },
    [onLoadPreset]
  );

  const handleDelete = useCallback(
    async (id: string, e: React.MouseEvent) => {
      e.stopPropagation();
      if (!confirm("Delete this preset?")) return;
      await rpc.call("composition.deletePattern", { id });
      setPatterns((prev) => prev.filter((p) => p.id !== id));
      if (activeId === id) setActiveId(null);
    },
    [activeId]
  );

  const handleImport = useCallback(async () => {
    fileInputRef.current?.click();
  }, []);

  const handleFileChange = useCallback(
    async (e: React.ChangeEvent<HTMLInputElement>) => {
      const file = e.target.files?.[0];
      if (!file) return;
      const text = await file.text();
      const result: unknown = await rpc.call("composition.importPattern", {
        json: text,
      });
      if (result && typeof result === "object" && "success" in (result as Record<string, unknown>)) {
        const updated: unknown = await rpc.call("composition.listPatterns", {});
        if (Array.isArray(updated)) setPatterns(updated as PatternEntry[]);
      }
      e.target.value = "";
    },
    []
  );

  if (collapsed) {
    return (
      <div className="preset-browser collapsed">
        <button
          className="preset-expand-btn"
          onClick={() => setCollapsed(false)}
          title="Show presets"
          style={{
            background: "none",
            border: "none",
            color: "var(--text-secondary)",
            cursor: "pointer",
            padding: "8px 4px",
          }}
        >
          ▶
        </button>
      </div>
    );
  }

  return (
    <div className="preset-browser">
      <input
        ref={fileInputRef}
        type="file"
        accept=".json"
        style={{ display: "none" }}
        onChange={handleFileChange}
      />
      <div className="preset-browser-header">
        <h3>Presets</h3>
        <div className="preset-browser-actions">
          <button onClick={handleImport} title="Import JSON pattern">
            Import
          </button>
          <button onClick={() => setCollapsed(true)} title="Hide presets">
            ◀
          </button>
        </div>
      </div>
      <div className="preset-search">
        <input
          type="text"
          placeholder="Search..."
          value={search}
          onChange={(e) => setSearch(e.target.value)}
        />
      </div>
      <div className="preset-list">
        {Array.from(grouped.entries()).map(([category, items]) => (
          <div key={category}>
            <div className="preset-category">{category}</div>
            {items.map((p) => (
              <div
                key={p.id}
                className={`preset-item ${activeId === p.id ? "active" : ""}`}
                onClick={() => handleLoad(p.id)}
              >
                <span>{p.name}</span>
                <span className="preset-style">{p.style}</span>
                {p.source === "user" && (
                  <button
                    className="preset-delete"
                    onClick={(e) => handleDelete(p.id, e)}
                    title="Delete preset"
                  >
                    ×
                  </button>
                )}
              </div>
            ))}
          </div>
        ))}
      </div>
    </div>
  );
}
