export const ENVELOPE_SHAPES = [
  { value: "ramp", label: "Ramp" },
  { value: "adsr", label: "ADSR" },
  { value: "sine", label: "Sine" },
  { value: "triangle", label: "Triangle" },
  { value: "saw", label: "Saw" },
  { value: "square", label: "Square" },
  { value: "pulse", label: "Pulse" },
  { value: "staircase", label: "Staircase" },
  { value: "sCurve", label: "S-Curve" },
  { value: "randomWalk", label: "Random Walk" },
  { value: "noise", label: "Noise" },
] as const;

export type EnvelopeShape = (typeof ENVELOPE_SHAPES)[number]["value"];
