import { useState, useEffect, useCallback } from "react";
import { rpc } from "../rpc";
import "./PreferencesDialog.css";

interface Props {
  onClose: () => void;
}

interface AudioSetup {
  driver: string;
  output: string;
  input: string;
  sampleRate: number;
  bufferSize: number;
  latencyMs: number;
}

export default function PreferencesDialog({ onClose }: Props) {
  const [midiDevices, setMidiDevices] = useState<string[]>([]);
  const [activeDevice, setActiveDevice] = useState<string>("");

  const [deviceTypes, setDeviceTypes] = useState<string[]>([]);
  const [outputDevices, setOutputDevices] = useState<string[]>([]);
  const [inputDevices, setInputDevices] = useState<string[]>([]);
  const [sampleRates, setSampleRates] = useState<number[]>([]);
  const [bufferSizes, setBufferSizes] = useState<number[]>([]);
  const [setup, setSetup] = useState<AudioSetup | null>(null);

  const loadAudioSetup = useCallback(async () => {
    const [types, s, outputs, inputs, rates, bufs] = await Promise.all([
      rpc.call("audio.getDeviceTypes").catch(() => []),
      rpc.call("audio.getCurrentSetup").catch(() => null),
      rpc.call("audio.getOutputDevices").catch(() => []),
      rpc.call("audio.getInputDevices").catch(() => []),
      rpc.call("audio.getSampleRates").catch(() => []),
      rpc.call("audio.getBufferSizes").catch(() => []),
    ]);
    setDeviceTypes(types as string[]);
    setSetup(s as AudioSetup);
    setOutputDevices(outputs as string[]);
    setInputDevices(inputs as string[]);
    setSampleRates(rates as number[]);
    setBufferSizes(bufs as number[]);
  }, []);

  useEffect(() => {
    rpc.call("midi.getAvailableDevices").then((d: any) => setMidiDevices(d)).catch(() => {});
    loadAudioSetup();
  }, [loadAudioSetup]);

  const handleOpenDevice = async (device: string) => {
    await rpc.call("midi.openDevice", { identifier: device });
    setActiveDevice(device);
  };

  const handleSetDeviceType = async (type: string) => {
    await rpc.call("audio.setDeviceType", { type }).catch(() => {});
    await loadAudioSetup();
  };

  const handleSetOutputDevice = async (name: string) => {
    await rpc.call("audio.setOutputDevice", { name }).catch(() => {});
    await loadAudioSetup();
  };

  const handleSetInputDevice = async (name: string) => {
    await rpc.call("audio.setInputDevice", { name }).catch(() => {});
    await loadAudioSetup();
  };

  const handleSetSampleRate = async (rate: number) => {
    await rpc.call("audio.setSampleRate", { rate }).catch(() => {});
    await loadAudioSetup();
  };

  const handleSetBufferSize = async (size: number) => {
    await rpc.call("audio.setBufferSize", { size }).catch(() => {});
    await loadAudioSetup();
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
            <h3>Audio</h3>
            <label>
              Driver
              <select
                value={setup?.driver ?? ""}
                onChange={(e) => handleSetDeviceType(e.target.value)}
              >
                {deviceTypes.map((t) => (
                  <option key={t} value={t}>{t}</option>
                ))}
              </select>
            </label>
            <label>
              Output Device
              <select
                value={setup?.output ?? ""}
                onChange={(e) => handleSetOutputDevice(e.target.value)}
              >
                {outputDevices.map((d) => (
                  <option key={d} value={d}>{d}</option>
                ))}
              </select>
            </label>
            <label>
              Input Device
              <select
                value={setup?.input ?? ""}
                onChange={(e) => handleSetInputDevice(e.target.value)}
              >
                {inputDevices.map((d) => (
                  <option key={d} value={d}>{d}</option>
                ))}
              </select>
            </label>
            <label>
              Sample Rate
              <select
                value={setup?.sampleRate ?? ""}
                onChange={(e) => handleSetSampleRate(Number(e.target.value))}
              >
                {sampleRates.map((r) => (
                  <option key={r} value={r}>{r} Hz</option>
                ))}
              </select>
            </label>
            <label>
              Buffer Size
              <select
                value={setup?.bufferSize ?? ""}
                onChange={(e) => handleSetBufferSize(Number(e.target.value))}
              >
                {bufferSizes.map((b) => (
                  <option key={b} value={b}>{b} samples</option>
                ))}
              </select>
            </label>
            {setup != null && setup.latencyMs > 0 && (
              <p className="pref-note">Latency: {setup.latencyMs.toFixed(1)} ms</p>
            )}
          </section>
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
            <h3>MCP Server</h3>
            <p className="pref-note">MCP server settings are configured via the C++ backend.</p>
          </section>
        </div>
      </div>
    </div>
  );
}
