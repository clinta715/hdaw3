import { useState } from "react";
import { ENVELOPE_SHAPES } from "./envelopeShapes";
import "./EnvelopeGenerateControl.css";

export interface EnvelopeGenerateParams {
  shape: string;
  start: number;
  end: number;
  startValue: number;
  endValue: number;
  cycles: number;
  seed: number;
}

interface EnvelopeGenerateControlProps {
  onGenerate: (params: EnvelopeGenerateParams) => void;
  defaultValueRange?: [number, number];
  collapsed?: boolean;
}

export default function EnvelopeGenerateControl({
  onGenerate,
  defaultValueRange = [0, 1],
  collapsed = false,
}: EnvelopeGenerateControlProps) {
  const [expanded, setExpanded] = useState(!collapsed);
  const [shape, setShape] = useState<string>(ENVELOPE_SHAPES[0].value);
  const [start, setStart] = useState(0);
  const [end, setEnd] = useState(16);
  const [startValue, setStartValue] = useState(defaultValueRange[0]);
  const [endValue, setEndValue] = useState(defaultValueRange[1]);
  const [cycles, setCycles] = useState(1);
  const [seed, setSeed] = useState(0);

  const handleApply = () => {
    onGenerate({
      shape,
      start,
      end,
      startValue,
      endValue,
      cycles,
      seed,
    });
  };

  return (
    <div className="egc-container">
      <div className="egc-toggle-row">
        <button
          className="egc-toggle-btn"
          onClick={() => setExpanded((v) => !v)}
        >
          {expanded ? "Hide" : "Generate"}
        </button>
      </div>
      {expanded && (
        <div className="egc-controls">
          <div className="egc-field">
            <label htmlFor="egc-shape">Shape</label>
            <select
              id="egc-shape"
              value={shape}
              onChange={(e) => setShape(e.target.value)}
            >
              {ENVELOPE_SHAPES.map((s) => (
                <option key={s.value} value={s.value}>
                  {s.label}
                </option>
              ))}
            </select>
          </div>
          <div className="egc-field">
            <label htmlFor="egc-start">Start</label>
            <input
              id="egc-start"
              type="number"
              value={start}
              onChange={(e) => setStart(Number(e.target.value))}
            />
          </div>
          <div className="egc-field">
            <label htmlFor="egc-end">End</label>
            <input
              id="egc-end"
              type="number"
              value={end}
              onChange={(e) => setEnd(Number(e.target.value))}
            />
          </div>
          <div className="egc-field">
            <label htmlFor="egc-startval">Val Start</label>
            <input
              id="egc-startval"
              type="number"
              value={startValue}
              onChange={(e) => setStartValue(Number(e.target.value))}
            />
          </div>
          <div className="egc-field">
            <label htmlFor="egc-endval">Val End</label>
            <input
              id="egc-endval"
              type="number"
              value={endValue}
              onChange={(e) => setEndValue(Number(e.target.value))}
            />
          </div>
          <div className="egc-field">
            <label htmlFor="egc-cycles">Cycles</label>
            <input
              id="egc-cycles"
              type="number"
              value={cycles}
              min={1}
              onChange={(e) => setCycles(Math.max(1, Number(e.target.value)))}
            />
          </div>
          <div className="egc-field">
            <label htmlFor="egc-seed">Seed</label>
            <input
              id="egc-seed"
              type="number"
              value={seed}
              onChange={(e) => setSeed(Number(e.target.value))}
            />
          </div>
          <button className="egc-apply-btn" onClick={handleApply}>
            Apply
          </button>
        </div>
      )}
    </div>
  );
}
