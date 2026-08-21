import { useState, useEffect, useCallback } from "react";
import { rpc } from "../rpc";
import { useLibraryStore } from "../store/libraryStore";
import "./PreferencesDialog.css";

function LibrarySettings() {
  const libraries = useLibraryStore((s) => s.libraries);
  const loading = useLibraryStore((s) => s.loading);
  const loadLibraries = useLibraryStore((s) => s.loadLibraries);
  const addLibrary = useLibraryStore((s) => s.addLibrary);
  const removeLibrary = useLibraryStore((s) => s.removeLibrary);
  const setAutoScan = useLibraryStore((s) => s.setAutoScan);

  const [addName, setAddName] = useState("");
  const [addPath, setAddPath] = useState("");
  const [addType, setAddType] = useState<"midi" | "audio">("midi");

  useEffect(() => {
    loadLibraries(rpc);
  }, [loadLibraries]);

  const handleAdd = async () => {
    if (!addName.trim() || !addPath.trim()) return;
    const ok = await addLibrary(addName.trim(), addPath.trim(), addType, rpc);
    if (ok) {
      setAddName("");
      setAddPath("");
    }
  };

  return (
    <section className="pref-section">
      <h3>Libraries</h3>
      {loading && <p className="pref-note">Loading...</p>}
      <div className="pref-libraries-list">
        {libraries.map((lib) => (
          <div key={lib.id} className="pref-library-row">
            <span className="pref-library-name">{lib.name}</span>
            <span className="pref-library-path" title={lib.path}>{lib.path}</span>
            <span className="pref-library-type">{lib.type}</span>
            <span className="pref-library-count">{lib.fileCount} files</span>
            <label className="pref-library-autoscan">
              <input
                type="checkbox"
                checked={lib.autoScan}
                onChange={(e) => setAutoScan(lib.id, e.target.checked, rpc)}
              />
              Auto-scan
            </label>
            <button
              className="pref-btn-danger"
              onClick={() => removeLibrary(lib.id, rpc)}
            >
              Remove
            </button>
          </div>
        ))}
      </div>
      <div className="pref-library-add">
        <input
          type="text"
          placeholder="Name"
          value={addName}
          onChange={(e) => setAddName(e.target.value)}
        />
        <input
          type="text"
          placeholder="Path"
          value={addPath}
          onChange={(e) => setAddPath(e.target.value)}
        />
        <select
          value={addType}
          onChange={(e) => setAddType(e.target.value as "midi" | "audio")}
        >
          <option value="midi">MIDI</option>
          <option value="audio">Audio</option>
        </select>
        <button onClick={handleAdd}>Add</button>
      </div>
    </section>
  );
}

interface Props {
  onClose: () => void;
}

function engineRpcUrl(): string {
  const api = (window as any).__HDAW_ELECTRON_API__ as { rpcPort?: number } | undefined;
  const injected = (window as any).__HDAW_WS_PORT__ as number | undefined;
  const port = api?.rpcPort ?? injected ?? 8766;
  return `ws://127.0.0.1:${port}`;
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

  const [defaultTempo, setDefaultTempo] = useState(120);
  const [defaultTimeSigNum, setDefaultTimeSigNum] = useState(4);
  const [defaultTimeSigDen, setDefaultTimeSigDen] = useState(4);
  const [maxBackups, setMaxBackups] = useState(10);
  const [pluginIsolation, setPluginIsolation] = useState(true);
  const [watchPlugins, setWatchPlugins] = useState(true);

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

  const loadSettings = useCallback(async () => {
    const [tempo, ts, backups, isolation, watch] = await Promise.all([
      rpc.call("settings.getDefaultTempo").catch(() => 120),
      rpc.call("settings.getDefaultTimeSignature").catch(() => ({ numerator: 4, denominator: 4 })),
      rpc.call("settings.getMaxBackups").catch(() => 10),
      rpc.call("plugin.getIsolationEnabled").catch(() => true),
      rpc.call("plugin.getWatchPlugins").catch(() => true),
    ]);
    setDefaultTempo(tempo as number);
    const tsObj = ts as { numerator: number; denominator: number };
    setDefaultTimeSigNum(tsObj.numerator);
    setDefaultTimeSigDen(tsObj.denominator);
    setMaxBackups(backups as number);
    setPluginIsolation(isolation as boolean);
    setWatchPlugins(watch as boolean);
  }, []);

  useEffect(() => {
    Promise.all([
      rpc.call("midi.getAvailableDevices").catch(() => []),
      rpc.call("midi.getOpenDevice").catch(() => ""),
    ]).then(([devices, current]) => {
      setMidiDevices(devices as string[]);
      setActiveDevice(current as string);
    });
    loadAudioSetup();
    loadSettings();
  }, [loadAudioSetup, loadSettings]);

  const handleOpenDevice = async (device: string) => {
    if (device === "") {
      await rpc.call("midi.closeDevice", {});
      setActiveDevice("");
      return;
    }
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

  const handleSetDefaultTempo = async (v: number) => {
    setDefaultTempo(v);
    await rpc.call("settings.setDefaultTempo", { value: v }).catch(() => {});
  };

  const handleSetTimeSigNum = async (v: number) => {
    setDefaultTimeSigNum(v);
    await rpc.call("settings.setDefaultTimeSignature", { numerator: v, denominator: defaultTimeSigDen }).catch(() => {});
  };

  const handleSetTimeSigDen = async (v: number) => {
    setDefaultTimeSigDen(v);
    await rpc.call("settings.setDefaultTimeSignature", { numerator: defaultTimeSigNum, denominator: v }).catch(() => {});
  };

  const handleSetMaxBackups = async (v: number) => {
    setMaxBackups(v);
    await rpc.call("settings.setMaxBackups", { value: v }).catch(() => {});
  };

  const handleSetPluginIsolation = async (v: boolean) => {
    setPluginIsolation(v);
    await rpc.call("plugin.setIsolationEnabled", { value: v }).catch(() => {});
  };

  const handleSetWatchPlugins = async (v: boolean) => {
    setWatchPlugins(v);
    await rpc.call("plugin.setWatchPlugins", { value: v }).catch(() => {});
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
            <h3>General</h3>
            <label>
              Default Tempo (BPM)
              <input type="number" min={20} max={999} value={defaultTempo}
                onChange={(e) => handleSetDefaultTempo(Number(e.target.value))} />
            </label>
            <label>
              Default Time Signature
              <select value={defaultTimeSigNum} onChange={(e) => handleSetTimeSigNum(Number(e.target.value))}>
                {[1,2,3,4,5,6,7,8,9,10,11,12].map(n => <option key={n} value={n}>{n}</option>)}
              </select>
              /
              <select value={defaultTimeSigDen} onChange={(e) => handleSetTimeSigDen(Number(e.target.value))}>
                {[1,2,4,8,16,32].map(d => <option key={d} value={d}>{d}</option>)}
              </select>
            </label>
            <label>
              Max Backups
              <input type="number" min={0} max={100} value={maxBackups}
                onChange={(e) => handleSetMaxBackups(Number(e.target.value))} />
            </label>
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
            <h3>Plugins</h3>
            <label>
              <input type="checkbox" checked={pluginIsolation}
                onChange={(e) => handleSetPluginIsolation(e.target.checked)} />
              Plugin Isolation (requires restart)
            </label>
            <label>
              <input type="checkbox" checked={watchPlugins}
                onChange={(e) => handleSetWatchPlugins(e.target.checked)} />
              Watch Plugin Directories
            </label>
          </section>
          <section className="pref-section">
            <h3>Engine Connection</h3>
            <p className="pref-conn-value">{engineRpcUrl()}</p>
            <p className="pref-note">WebSocket RPC endpoint this session is connected to. Ports are set via engine command-line flags.</p>
          </section>
          <LibrarySettings />
        </div>
      </div>
    </div>
  );
}
