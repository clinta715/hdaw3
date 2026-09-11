import { useCallback, useEffect, useState } from "react";
import { rpc } from "../../rpc";
import { reportRpcError } from "../../store/notifyStore";
import { useProjectStore } from "../../store/projectStore";

// Song Plan panel (plan/cell workflow, Phase D): the deterministic skeleton
// editor + the role×section content matrix, both driving the
// composition.{setSongPlan,getSongPlan,setCellRecipe,getCells,fillCells,
// rerollCells,removeCellRecipe,getClipProvenance} RPCs and the section
// templates. Structure stays pinned; content re-rolls by seed.

const NOTE_NAMES = ["C","C#","D","D#","E","F","F#","G","G#","A","A#","B"];
const SECTION_KINDS = ["intro","build","mainA","mini","mainB","breakdown","finale","other"];
const SOURCES = ["phrase","rhythm","break","pattern","harvest"] as const;

interface SectionDraft { name: string; kind: string; bars: number; }
interface EnergySection { name: string; rms: number; peak: number; }
interface EnergyReport { sections: EnergySection[]; peak: number; rms: number;
                        kickProminence: number; pumpDepth?: number; }
interface CellRow {
  section: string; role: string; trackId: number; source: string;
  paramsJson: string; seed: number; locked: boolean; lastClipId: number; lastSeed: number;
}

const BEATS_PER_BAR = 4;

export default function SongPlanPanel() {
  const tracks = useProjectStore((s) => s.snapshot?.tracks ?? []);
  const [bpm, setBpm] = useState(140);
  const [keyRoot, setKeyRoot] = useState(5);
  const [scaleMode, setScaleMode] = useState(7);
  const [style, setStyle] = useState("full-on");
  const [seed, setSeed] = useState(0);
  const [sections, setSections] = useState<SectionDraft[]>([
    { name: "intro", kind: "intro", bars: 8 },
    { name: "build", kind: "build", bars: 8 },
    { name: "main", kind: "mainA", bars: 16 },
  ]);
  const [hasPlan, setHasPlan] = useState(false);
  const [cells, setCells] = useState<CellRow[]>([]);
  const [templates, setTemplates] = useState<string[]>([]);
  const [templateName, setTemplateName] = useState("");
  const [msg, setMsg] = useState("");
  const [busy, setBusy] = useState(false);
  const [energy, setEnergy] = useState<EnergyReport | null>(null);
  // Picker catalogs (ComposeTab metadata RPCs) so cell params JSON can be
  // built by choosing instead of typing.
  const [styles, setStyles] = useState<string[]>([]);
  const [patterns, setPatterns] = useState<{ id: string; name: string }[]>([]);

  const totalBars = sections.reduce((a, s) => a + (s.bars > 0 ? s.bars : 0), 0);

  const paramsFrom = (json: string): Record<string, unknown> => {
    try {
      const p = JSON.parse(json || "{}");
      if (p && typeof p === "object" && !Array.isArray(p)) return p as Record<string, unknown>;
    } catch { /* invalid manual JSON: pickers start fresh over it */ }
    return {};
  };
  const paramOf = (key: string): string => String(paramsFrom(newCell.paramsJson)[key] ?? "");
  const patchParams = (patch: Record<string, unknown>) =>
    setNewCell((c) => ({ ...c, paramsJson: JSON.stringify({ ...paramsFrom(c.paramsJson), ...patch }) }));
  const changeSource = (source: string) =>
    setNewCell((c) => {
      const obj = paramsFrom(c.paramsJson);
      delete obj.style; delete obj.patternId; delete obj.notes;
      if (source === "phrase") obj.style = "BassLine";
      if (source === "pattern") obj.patternId = "";
      return { ...c, source, paramsJson: JSON.stringify(obj) };
    });

  const planPayload = useCallback(() => ({
    bpm, keyRoot, scaleMode, style, seed, totalBars,
    sections: sections.map((s) => ({ name: s.name, kind: s.kind, bars: s.bars })),
  }), [bpm, keyRoot, scaleMode, style, seed, sections, totalBars]);

  const refresh = useCallback(async () => {
    try {
      const p = await rpc.call("composition.getSongPlan", {}) as {
        hasPlan?: boolean; bpm?: number; keyRoot?: number; scaleMode?: number;
        style?: string; seed?: number;
        sections?: { name: string; kind: string; startBeat: number; endBeat: number }[];
      };
      if (p && p.hasPlan) {
        setHasPlan(true);
        setBpm(p.bpm ?? 140);
        setKeyRoot(p.keyRoot ?? 0);
        setScaleMode(p.scaleMode ?? 1);
        setStyle(p.style ?? "");
        setSeed(p.seed ?? 0);
        if (Array.isArray(p.sections) && p.sections.length > 0) {
          setSections(p.sections.map((s) => ({
            name: s.name, kind: s.kind,
            bars: Math.max(1, Math.round(((s.endBeat ?? 32) - (s.startBeat ?? 0)) / BEATS_PER_BAR)),
          })));
        }
      } else if (p) {
        setHasPlan(false);
      }
      const c = await rpc.call("composition.getCells", {}) as { cells?: CellRow[] };
      setCells(c?.cells ?? []);
      const t = await rpc.call("composition.listSectionTemplates", {}) as { templates?: string[] };
      setTemplates(t?.templates ?? []);
    } catch (e) {
      reportRpcError("composition.getSongPlan", e);
    }
  }, []);

  useEffect(() => { void refresh(); }, [refresh]);

  useEffect(() => {
    rpc.call("composition.getStyleNames")
      .then((r) => setStyles((Array.isArray(r) ? r as { name?: string }[] : [])
        .map((s) => s.name ?? "").filter(Boolean)))
      .catch(() => setStyles([]));
    rpc.call("composition.listPatterns")
      .then((r) => {
        const arr = Array.isArray(r) ? r : (r as { patterns?: unknown[] })?.patterns;
        setPatterns((Array.isArray(arr) ? arr as { id: string; name?: string }[] : [])
          .map((e) => ({ id: e.id, name: e.name || e.id })));
      })
      .catch(() => setPatterns([]));
  }, []);

  const apply = async () => {
    setBusy(true);
    try {
      await rpc.call("composition.setSongPlan", planPayload());
      setMsg("Plan applied — arranger regions synced.");
      await refresh();
    } catch (e) { reportRpcError("composition.setSongPlan", e); }
    finally { setBusy(false); }
  };

  const saveTemplate = async () => {
    if (!templateName.trim()) { setMsg("Enter a template name first."); return; }
    try {
      await rpc.call("composition.saveSectionTemplate", { name: templateName.trim() });
      setMsg("Template saved.");
      await refresh();
    } catch (e) { reportRpcError("composition.saveSectionTemplate", e); }
  };

  const loadTemplate = async (name: string) => {
    try {
      const t = await rpc.call("composition.loadSectionTemplate", { name }) as {
        bpm?: number; keyRoot?: number; scaleMode?: number; style?: string; seed?: number;
        sections?: { name: string; kind: string; bars: number }[];
      };
      if (!t || !Array.isArray(t.sections)) return;
      setBpm(t.bpm ?? bpm); setKeyRoot(t.keyRoot ?? keyRoot); setScaleMode(t.scaleMode ?? scaleMode);
      setStyle(t.style ?? style); setSeed(t.seed ?? seed);
      setSections(t.sections.map((s) => ({ name: s.name, kind: s.kind, bars: s.bars })));
      setMsg("Template loaded into the editor (Apply Plan to set it).");
    } catch (e) { reportRpcError("composition.loadSectionTemplate", e); }
  };

  const setSection = (i: number, patch: Partial<SectionDraft>) =>
    setSections((prev) => prev.map((s, j) => (j === i ? { ...s, ...patch } : s)));

  const addSection = () =>
    setSections((prev) => [...prev, { name: "part" + (prev.length + 1), kind: "mainA", bars: 8 }]);

  const removeSection = (i: number) =>
    setSections((prev) => prev.filter((_, j) => j !== i));

  const setCell = async (cell: Partial<CellRow> & { section: string; role: string; trackId: number; source: string }) => {
    try {
      await rpc.call("composition.setCellRecipe", {
        section: cell.section, role: cell.role, trackId: cell.trackId,
        source: cell.source, params: cell.paramsJson ?? "{}",
        seed: cell.seed ?? 0, locked: cell.locked ?? false,
      });
      await refresh();
    } catch (e) { reportRpcError("composition.setCellRecipe", e); }
  };

  // Add-cell form state
  const [newCell, setNewCell] = useState({
    section: "intro", role: "bass", trackId: 1, source: "phrase" as string,
    paramsJson: JSON.stringify({ style: "BassLine" }), seed: 0,
  });

  const fill = async (mode: "all" | "unfilled") => {
    setBusy(true);
    try {
      const b = await rpc.call("composition.fillCells", { mode }) as {
        filled?: number; skippedLocked?: number; failed?: number;
      };
      setMsg("Fill: " + (b?.filled ?? 0) + " cells, skipped " + (b?.skippedLocked ?? 0) + " locked, " + (b?.failed ?? 0) + " failed.");
      await refresh();
    } catch (e) { reportRpcError("composition.fillCells", e); }
    finally { setBusy(false); }
  };

  const reroll = async (cell: CellRow) => {
    try {
      await rpc.call("composition.rerollCells", { section: cell.section, role: cell.role });
      setMsg("Rerolled " + cell.section + "/" + cell.role + ".");
      await refresh();
    } catch (e) { reportRpcError("composition.rerollCells", e); }
  };

  const remove = async (cell: CellRow) => {
    try {
      await rpc.call("composition.removeCellRecipe", { section: cell.section, role: cell.role });
      await refresh();
    } catch (e) { reportRpcError("composition.removeCellRecipe", e); }
  };

  // Energy arc: render whole project to a temp WAV (progress notifications
  // flow on the existing export channel), analyze it against the plan windows.
  const checkEnergy = async () => {
    setBusy(true);
    setMsg("Rendering + analyzing…");
    try {
      const r = await rpc.call("export.temporaryRender", {}) as { outputPath?: string };
      const path = r?.outputPath ?? "";
      if (!path) throw new Error("temporary render produced no path");
      const rep = await rpc.call("audio.mixReport", { filePath: path, fromPlan: true }) as EnergyReport;
      const sections = Array.isArray(rep?.sections) ? rep.sections : [];
      setEnergy({ sections, peak: rep?.peak ?? 0, rms: rep?.rms ?? 0,
                  kickProminence: rep?.kickProminence ?? 0, pumpDepth: rep?.pumpDepth });
      const f = (n: number) => n.toFixed(3);
      setMsg("Peak " + f(rep?.peak ?? 0) + " · kick prominence " + f(rep?.kickProminence ?? 0)
             + (typeof rep?.pumpDepth === "number" ? " · pump " + f(rep.pumpDepth) : ""));
    } catch (e) {
      setEnergy(null);
      setMsg("Energy check failed.");
      reportRpcError("energy check", e);
    } finally {
      setBusy(false);
    }
  };

  const provenance = async (cell: CellRow) => {
    if (cell.lastClipId < 0) { setMsg("Cell has no generated clip yet."); return; }
    try {
      const p = await rpc.call("composition.getClipProvenance", { clipId: cell.lastClipId }) as {
        found?: boolean; source?: string; seed?: number;
      };
      setMsg(p?.found
        ? "Clip " + cell.lastClipId + ": " + p.source + " seed " + Math.round(p.seed ?? 0)
        : "Clip " + cell.lastClipId + " has no provenance (manual clip?).");
    } catch (e) { reportRpcError("composition.getClipProvenance", e); }
  };

  return (
    <div className="pgd-page song-plan-panel" data-testid="song-plan-panel">
      <div className="pgd-row">
        <label className="pgd-label">BPM</label>
        <input className="pgd-input" type="number" min={60} max={200} value={bpm}
               onChange={(e) => setBpm(Number(e.target.value))} aria-label="Plan BPM" />
        <label className="pgd-label">Key</label>
        <select className="pgd-select" value={keyRoot} aria-label="Plan key"
                onChange={(e) => setKeyRoot(Number(e.target.value))}>
          {NOTE_NAMES.map((n, i) => <option key={n} value={i}>{n}</option>)}
        </select>
        <label className="pgd-label">Scale</label>
        <input className="pgd-input" type="number" min={0} max={12} value={scaleMode}
               onChange={(e) => setScaleMode(Number(e.target.value))} aria-label="Plan scale mode" style={{ width: 52 }} />
        <label className="pgd-label">Style</label>
        <input className="pgd-input" value={style} onChange={(e) => setStyle(e.target.value)} aria-label="Plan style" />
        <label className="pgd-label">Seed</label>
        <input className="pgd-input" type="number" min={0} value={seed}
               onChange={(e) => setSeed(Number(e.target.value))} aria-label="Plan seed" style={{ width: 90 }} />
      </div>

      <div className="pgd-row">
        <label className="pgd-label">Sections ({totalBars} bars)</label>
        <button className="pgd-btn" onClick={addSection} title="Add section">＋</button>
      </div>
      {sections.map((s, i) => (
        <div className="pgd-row" key={i} data-testid={"section-row-" + i}>
          <input className="pgd-input" value={s.name} aria-label={"Section name " + (i + 1)}
                 onChange={(e) => setSection(i, { name: e.target.value })} />
          <select className="pgd-select" value={s.kind} aria-label={"Section kind " + (i + 1)}
                  onChange={(e) => setSection(i, { kind: e.target.value })}>
            {SECTION_KINDS.map((k) => <option key={k} value={k}>{k}</option>)}
          </select>
          <input className="pgd-input" type="number" min={1} max={256} value={s.bars} style={{ width: 60 }}
                 aria-label={"Section bars " + (i + 1)}
                 onChange={(e) => setSection(i, { bars: Number(e.target.value) })} />
          <button className="pgd-btn" onClick={() => removeSection(i)} title="Remove section">✕</button>
        </div>
      ))}

      <div className="pgd-row">
        <button className="pgd-btn pgd-btn-generate" onClick={apply} disabled={busy || sections.length === 0}
                title="Apply plan" data-testid="apply-plan">
          {hasPlan ? "Re-apply Plan" : "Apply Plan"}
        </button>
        <input className="pgd-input" placeholder="template name" value={templateName}
               onChange={(e) => setTemplateName(e.target.value)} aria-label="Template name" />
        <button className="pgd-btn" onClick={saveTemplate} disabled={!hasPlan && !templateName.trim()}
                title="Save current plan as template">Save&nbsp;Template</button>
        {templates.length > 0 && (
          <select className="pgd-select" aria-label="Load template" value=""
                  onChange={(e) => { if (e.target.value) void loadTemplate(e.target.value); }}>
            <option value="">Load template…</option>
            {templates.map((t) => <option key={t} value={t}>{t}</option>)}
          </select>
        )}
      </div>

      <div className="pgd-row">
        <label className="pgd-label">Add cell</label>
        <select className="pgd-select" aria-label="Cell section" value={newCell.section}
                onChange={(e) => setNewCell((c) => ({ ...c, section: e.target.value }))}>
          {sections.map((s) => <option key={s.name} value={s.name}>{s.name}</option>)}
        </select>
        <input className="pgd-input" value={newCell.role} aria-label="Cell role" placeholder="role"
               onChange={(e) => setNewCell((c) => ({ ...c, role: e.target.value }))} />
        <select className="pgd-select" aria-label="Cell track" value={newCell.trackId}
                onChange={(e) => setNewCell((c) => ({ ...c, trackId: Number(e.target.value) }))}>
          {tracks.map((t) => <option key={t.index} value={t.index}>#{t.index} {t.name}</option>)}
        </select>
        <select className="pgd-select" aria-label="Cell source" value={newCell.source}
                onChange={(e) => changeSource(e.target.value)}>
          {SOURCES.map((s) => <option key={s} value={s}>{s}</option>)}
        </select>
        <input className="pgd-input" type="number" min={0} value={newCell.seed} style={{ width: 90 }}
               aria-label="Cell seed" onChange={(e) => setNewCell((c) => ({ ...c, seed: Number(e.target.value) }))} />
        <button className="pgd-btn" title="Add cell" data-testid="add-cell"
                onClick={() => void setCell({ ...newCell, locked: false })}>＋&nbsp;Cell</button>
      </div>
      {newCell.source === "phrase" && styles.length > 0 && (
        <div className="pgd-row">
          <label className="pgd-label">Style</label>
          <select className="pgd-select" aria-label="Phrase style" data-testid="style-picker"
                  value={paramOf("style") || styles[0]}
                  onChange={(e) => patchParams({ style: e.target.value })}>
            {styles.map((s) => <option key={s} value={s}>{s}</option>)}
          </select>
        </div>
      )}
      {newCell.source === "pattern" && patterns.length > 0 && (
        <div className="pgd-row">
          <label className="pgd-label">Pattern</label>
          <select className="pgd-select" aria-label="Pattern id" data-testid="pattern-picker"
                  value={paramOf("patternId")}
                  onChange={(e) => patchParams({ patternId: e.target.value })}>
            <option value="">(choose)</option>
            {patterns.map((p) => <option key={p.id} value={p.id}>{p.name}</option>)}
          </select>
        </div>
      )}
      <div className="pgd-row">
        <input className="pgd-input" value={newCell.paramsJson} aria-label="Cell params JSON"
               onChange={(e) => setNewCell((c) => ({ ...c, paramsJson: e.target.value }))} />
      </div>

      {cells.length > 0 && (
        <div className="pgd-row" style={{ display: "block" }}>
          <label className="pgd-label">Cells ({cells.length})</label>
          {cells.map((c, i) => (
            <div className="pgd-row" key={i} data-testid={"cell-row-" + i}>
              <span className="pgd-value" style={{ minWidth: 70 }}>{c.section}</span>
              <span className="pgd-value" style={{ minWidth: 60 }}>{c.role}</span>
              <span className="pgd-value" style={{ minWidth: 40 }}>t{c.trackId}</span>
              <span className="pgd-value" style={{ minWidth: 56 }}>{c.source}</span>
              <span className="pgd-value" style={{ minWidth: 80 }}>
                {c.lastClipId >= 0 ? "clip " + c.lastClipId : "—"}
              </span>
              {c.locked ? "🔒" : ""}
              <button className="pgd-btn" title="Toggle lock"
                      onClick={() => void setCell({ ...c, params: c.paramsJson })}>
                {c.locked ? "Unlock" : "Lock"}
              </button>
              <button className="pgd-btn" title="Reroll cell" disabled={busy}
                      onClick={() => void reroll(c)}>Reroll</button>
              <button className="pgd-btn" title="Show provenance"
                      onClick={() => void provenance(c)}>Prov</button>
              <button className="pgd-btn" title="Delete cell" onClick={() => void remove(c)}>✕</button>
            </div>
          ))}
          <div className="pgd-row">
            <button className="pgd-btn pgd-btn-generate" title="Fill cells" data-testid="fill-cells"
                    disabled={busy} onClick={() => void fill("all")}>
              {busy ? "Filling..." : "Fill Cells"}
            </button>
            <button className="pgd-btn" title="Fill unfilled cells"
                    disabled={busy} onClick={() => void fill("unfilled")}>Fill Unfilled</button>
          </div>
        </div>
      )}

      <div className="pgd-row">
        <button className="pgd-btn" data-testid="check-energy" title="Check energy"
                disabled={busy || !hasPlan} onClick={() => void checkEnergy()}>
          {busy ? "Working..." : "Check energy"}
        </button>
        <span className="pgd-label">render + analyze against plan sections</span>
      </div>
      {energy && (
        <div data-testid="energy-arc">
          {(() => {
            const max = Math.max(...energy.sections.map((s) => s.rms), 1e-9);
            return energy.sections.map((s, i) => (
              <div className="pgd-row" key={i} data-testid={"energy-row-" + i}>
                <span className="pgd-value" style={{ minWidth: 70 }}>{s.name}</span>
                <div data-testid={"energy-bar-" + i}
                     style={{ height: 10, width: Math.max(2, Math.round(100 * s.rms / max)) + "%",
                              background: s.peak > 0.99 ? "var(--vu-red)" : "var(--vu-green)" }} />
                <span className="pgd-value">{s.rms.toFixed(3)}</span>
              </div>
            ));
          })()}
        </div>
      )}

      {msg && <div className="pgd-preview" data-testid="plan-msg">{msg}</div>}
    </div>
  );
}