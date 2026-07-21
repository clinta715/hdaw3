import { useState, useEffect, useCallback } from "react";
import { rpc } from "../rpc";
import "./PluginManagerDialog.css";

interface PluginInfo {
  name: string;
  format: string;
  manufacturer: string;
  fileOrIdentifier: string;
  isInstrument: boolean;
}

type FilterTab = "all" | "instruments" | "effects" | "blacklisted";

interface Props {
  onClose: () => void;
}

export default function PluginManagerDialog({ onClose }: Props) {
  const [plugins, setPlugins] = useState<PluginInfo[]>([]);
  const [loading, setLoading] = useState(false);
  const [filter, setFilter] = useState("");
  const [blacklisted, setBlacklisted] = useState<Set<string>>(new Set());
  const [reasons, setReasons] = useState<Map<string, string>>(new Map());
  const [activeTab, setActiveTab] = useState<FilterTab>("all");
  const [showBlacklisted, setShowBlacklisted] = useState(true);

  // Scan progress state
  const [scanCompleted, setScanCompleted] = useState(0);
  const [scanTotal, setScanTotal] = useState(0);
  const [scanCurrentFile, setScanCurrentFile] = useState("");
  const [scanDone, setScanDone] = useState(false);

  const fetchPlugins = useCallback(async () => {
    try {
      const result = await rpc.call("plugin.getPlugins") as PluginInfo[];
      setPlugins(result);
      if (result.length === 0) {
        setBlacklisted(new Set());
        setReasons(new Map());
        return;
      }
      const flags = await Promise.all(
        result.map((p) =>
          rpc.call("plugin.isBlacklisted", { pluginID: p.fileOrIdentifier })
            .then((v) => [p.fileOrIdentifier, !!v] as const)
            .catch(() => [p.fileOrIdentifier, false] as const)
        )
      );
      const bl = new Set<string>();
      const blIds: string[] = [];
      for (const [id, isBl] of flags) {
        if (isBl) { bl.add(id); blIds.push(id); }
      }
      setBlacklisted(bl);

      // Fetch blacklist reasons in parallel
      if (blIds.length > 0) {
        const reasonEntries = await Promise.all(
          blIds.map((id) =>
            rpc.call("plugin.getBlacklistReason", { pluginID: id })
              .then((r) => [id, String(r || "")] as const)
              .catch(() => [id, ""] as const)
          )
        );
        const rm = new Map<string, string>();
        for (const [id, reason] of reasonEntries) rm.set(id, reason);
        setReasons(rm);
      }
    } catch (e) { console.error(e); }
  }, []);

  useEffect(() => { fetchPlugins(); }, [fetchPlugins]);

  // Subscribe to scan progress notifications
  useEffect(() => {
    const unsub = rpc.onNotification("notify.scanProgress", (_method, params) => {
      const p = params as Record<string, unknown>;
      const completed = p.completed as number;
      const total = p.total as number;
      const fileName = (p.fileName as string) || "";
      const done = p.done as boolean | undefined;

      if (done) {
        setScanDone(true);
        setScanCompleted(0);
        setScanTotal(0);
        setScanCurrentFile("");
        setLoading(false);
        fetchPlugins();
        return;
      }

      setLoading(true);
      setScanDone(false);
      if (total > 0) setScanTotal(total);
      if (completed >= 0) setScanCompleted(completed);
      setScanCurrentFile(fileName);
    });
    return unsub;
  }, [fetchPlugins]);

  const handleScan = async () => {
    setLoading(true);
    setScanDone(false);
    setScanCompleted(0);
    setScanTotal(0);
    setScanCurrentFile("");
    try { await rpc.call("plugin.scanAll"); } catch (e) { console.error(e); }
  };

  const handleToggleBlacklist = async (pluginID: string) => {
    if (blacklisted.has(pluginID)) {
      await rpc.call("plugin.unblacklistPlugin", { pluginID });
    } else {
      await rpc.call("plugin.blacklistPlugin", { pluginID });
    }
    fetchPlugins();
  };

  const filtered = plugins.filter((p) => {
    // Tab filter
    if (activeTab === "instruments" && !p.isInstrument) return false;
    if (activeTab === "effects" && p.isInstrument) return false;
    if (activeTab === "blacklisted" && !blacklisted.has(p.fileOrIdentifier)) return false;
    // Show/hide blacklisted
    if (!showBlacklisted && blacklisted.has(p.fileOrIdentifier)) return false;
    // Text filter
    if (filter) {
      const q = filter.toLowerCase();
      return p.name.toLowerCase().includes(q) ||
        p.manufacturer.toLowerCase().includes(q) ||
        p.format.toLowerCase().includes(q);
    }
    return true;
  });

  const instrumentCount = plugins.filter((p) => p.isInstrument).length;
  const effectCount = plugins.filter((p) => !p.isInstrument).length;

  return (
    <div className="modal-overlay" onClick={onClose}>
      <div className="plugin-manager" onClick={(e) => e.stopPropagation()}>
        <div className="pm-header">
          <h2>Plugin Manager</h2>
          <span className="pm-count">{plugins.length} plugins</span>
          <button className="pm-close" onClick={onClose}>×</button>
        </div>

        {/* Filter tabs */}
        <div className="pm-tabs">
          <button
            className={`pm-tab${activeTab === "all" ? " active" : ""}`}
            onClick={() => setActiveTab("all")}
          >
            All ({plugins.length})
          </button>
          <button
            className={`pm-tab${activeTab === "instruments" ? " active" : ""}`}
            onClick={() => setActiveTab("instruments")}
          >
            Instruments ({instrumentCount})
          </button>
          <button
            className={`pm-tab${activeTab === "effects" ? " active" : ""}`}
            onClick={() => setActiveTab("effects")}
          >
            Effects ({effectCount})
          </button>
          <button
            className={`pm-tab pm-tab--bl${activeTab === "blacklisted" ? " active" : ""}`}
            onClick={() => setActiveTab("blacklisted")}
          >
            Blacklisted ({blacklisted.size})
          </button>
        </div>

        {/* Toolbar */}
        <div className="pm-toolbar">
          <input
            className="pm-filter"
            placeholder="Filter by name, manufacturer, format..."
            value={filter}
            onChange={(e) => setFilter(e.target.value)}
          />
          <label className="pm-show-bl">
            <input
              type="checkbox"
              checked={showBlacklisted}
              onChange={(e) => setShowBlacklisted(e.target.checked)}
            />
            Show blacklisted
          </label>
          <button className="pm-scan-btn" onClick={handleScan} disabled={loading}>
            {loading ? (scanTotal > 0 ? `Scanning ${scanCompleted}/${scanTotal}...` : "Scanning...") : "Rescan"}
          </button>
        </div>

        {/* Progress bar */}
        {loading && scanTotal > 0 && (
          <div className="pm-progress-container">
            <div className="pm-progress">
              <div className="pm-progress-bar" style={{ width: `${(scanCompleted / scanTotal) * 100}%` }} />
            </div>
            <div className="pm-progress-text">
              {scanCurrentFile && <span className="pm-progress-file">{scanCurrentFile}</span>}
              <span className="pm-progress-count">{scanCompleted} / {scanTotal}</span>
            </div>
          </div>
        )}

        {/* Plugin list */}
        <div className="pm-list">
          {filtered.length === 0 && (
            <div className="pm-empty">
              {plugins.length === 0 ? "No plugins found" : "No plugins match filter"}
            </div>
          )}
          {filtered.map((p) => {
            const isBl = blacklisted.has(p.fileOrIdentifier);
            const reason = reasons.get(p.fileOrIdentifier) ?? "";
            return (
              <div key={p.fileOrIdentifier} className={`pm-item${isBl ? " blacklisted" : ""}`}>
                <div className="pm-item-info">
                  <span className={`pm-item-name${isBl ? " pm-item-name--bl" : ""}`}>
                    {p.name}
                    {isBl && reason && <span className="pm-item-reason"> — {reason === "crash" ? "crashed during scan" : reason}</span>}
                  </span>
                  <span className="pm-item-meta">
                    {p.manufacturer} — {p.format}
                    {p.isInstrument && <span className="pm-item-tag">Instrument</span>}
                  </span>
                </div>
                <button
                  className={`pm-bl-btn${isBl ? " pm-bl-btn--active" : ""}`}
                  onClick={() => handleToggleBlacklist(p.fileOrIdentifier)}
                >
                  {isBl ? "Unblacklist" : "Blacklist"}
                </button>
              </div>
            );
          })}
        </div>
      </div>
    </div>
  );
}
