import { useState, useEffect } from "react";
import { rpc } from "../rpc";
import "./PreferencesDialog.css";

interface Props {
  onClose: () => void;
}

export default function PreferencesDialog({ onClose }: Props) {
  const [midiDevices, setMidiDevices] = useState<string[]>([]);
  const [activeDevice, setActiveDevice] = useState<string>("");

  useEffect(() => {
    rpc.call("midi.getAvailableDevices").then((d: any) => setMidiDevices(d)).catch(() => {});
  }, []);

  const handleOpenDevice = async (device: string) => {
    await rpc.call("midi.openDevice", { identifier: device });
    setActiveDevice(device);
  };

  return (
    <div className="modal-overlay" onClick={onClose}>
      <div className="preferences-dialog" onClick={(e) => e.stopPropagation()}>
        <div className="pref-header">
          <h2>Preferences</h2>
          <button className="pref-close" onClick={onClose}>×</button>
        </div>
        <div className="pref-body">
          <section className="pref-section">
            <h3>MIDI</h3>
            <label>
              Input Device
              <select
                value={activeDevice}
                onChange={(e) => handleOpenDevice(e.target.value)}
              >
                <option value="">None</option>
                {midiDevices.map((d) => (
                  <option key={d} value={d}>{d}</option>
                ))}
              </select>
            </label>
          </section>
          <section className="pref-section">
            <h3>Audio</h3>
            <p className="pref-note">Audio device settings are configured in the C++ backend preferences.</p>
          </section>
          <section className="pref-section">
            <h3>MCP Server</h3>
            <p className="pref-note">MCP server settings are configured via the C++ backend.</p>
          </section>
        </div>
      </div>
    </div>
  );
}