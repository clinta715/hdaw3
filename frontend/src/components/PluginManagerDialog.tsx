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

interface Props {
  onClose: () => void;
}

export default function PluginManagerDialog({ onClose }: Props) {
  const [plugins, setPlugins] = useState<PluginInfo[]>([]);
  const [loading, setLoading] = useState(false);
  const [filter, setFilter] = useState("");
  const [blacklisted, setBlacklisted] = useState<Set<string>>(new Set());

  const fetchPlugins = useCallback(async () => {
    try {
      const result = await rpc.call("plugin.getPlugins") as PluginInfo[];
      setPlugins(result);
      const bl = new Set<string>();
      for (const p of result) {
        const isBl = await rpc.call("plugin.isBlacklisted", { pluginID: p.fileOrIdentifier }) as boolean;
        if (isBl) bl.add(p.fileOrIdentifier);
      }
      setBlacklisted(bl);
    } catch (e) { console.error(e); }
  }, []);

  useEffect(() => { fetchPlugins(); }, [fetchPlugins]);

  const handleScan = async () => {
    setLoading(true);
    try { await rpc.call("plugin.scanAll"); } catch (e) { console.error(e); }
    setLoading(false);
    fetchPlugins();
  };

  const handleToggleBlacklist = async (pluginID: string) => {
    if (blacklisted.has(pluginID)) {
      await rpc.call("plugin.unblacklistPlugin", { pluginID });
    } else {
      await rpc.call("plugin.blacklistPlugin", { pluginID });
    }
    fetchPlugins();
  };

  const filtered = plugins.filter((p) =>
    p.name.toLowerCase().includes(filter.toLowerCase()) ||
    p.manufacturer.toLowerCase().includes(filter.toLowerCase())
  );

  return (
    <div className="modal-overlay" onClick={onClose}>
      <div className="plugin-manager" onClick={(e) => e.stopPropagation()}>
        <div className="pm-header">
          <h2>Plugin Manager</h2>
          <button className="pm-close" onClick={onClose}>×</button>
        </div>
        <div className="pm-toolbar">
          <input
            className="pm-filter"
            placeholder="Filter plugins..."
            value={filter}
            onChange={(e) => setFilter(e.target.value)}
          />
          <button className="pm-scan-btn" onClick={handleScan} disabled={loading}>
            {loading ? "Scanning..." : "Rescan"}
          </button>
        </div>
        <div className="pm-list">
          {filtered.map((p) => (
            <div key={p.fileOrIdentifier} className={`pm-item${blacklisted.has(p.fileOrIdentifier) ? " blacklisted" : ""}`}>
              <div className="pm-item-info">
                <span className="pm-item-name">{p.name}</span>
                <span className="pm-item-meta">{p.manufacturer} — {p.format}</span>
              </div>
              <button
                className="pm-bl-btn"
                onClick={() => handleToggleBlacklist(p.fileOrIdentifier)}
              >
                {blacklisted.has(p.fileOrIdentifier) ? "Unblacklist" : "Blacklist"}
              </button>
            </div>
          ))}
        </div>
      </div>
    </div>
  );
}
