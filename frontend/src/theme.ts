export const theme = {
  bgWindow: "#141416",
  bgPanel: "#1e1e22",
  bgHeader: "#1e1e22",
  bgWidget: "#2a2a2e",
  bgInput: "#2a2a2e",
  bgElevated: "#323236",
  bgToolbar: "rgba(20, 20, 22, 0.9)",

  border: "#2a2a2e",
  borderLight: "#3a3a40",

  textPrimary: "#e8e8ec",
  textSecondary: "#a8a8b0",
  textMuted: "#787880",

  accent: "#d97706",
  accentDim: "#b45309",
  accentBright: "#f59e0b",

  danger: "#ef4444",
  warning: "#eab308",
  success: "#10b981",
  info: "#38b2df",

  vuGreen: "#10b981",
  vuYellow: "#f59e0b",
  vuRed: "#ef4444",

  trackFill1: "#28282c",
  trackFill2: "#2c2c30",
  trackColor: "rgba(217, 119, 6, 0.16)",
  rulerBg: "#222226",

  automationFill: "#4fc3f722",
  automationLine: "#4fc3f7",

  scrollbarBg: "#1e1e22",
  scrollbarHandle: "#3a3a40",
  scrollbarHover: "#d97706",

  gridLineBar: "rgba(255, 255, 255, 0.07)",
  gridLineBeat: "rgba(255, 255, 255, 0.03)",
  gridLineSub: "rgba(255, 255, 255, 0.016)",
  placeholderText: "rgba(255, 255, 255, 0.31)",
} as const;

export function injectTheme() {
  const root = document.documentElement;
  for (const [key, value] of Object.entries(theme)) {
    const cssName = "--" + key.replace(/([A-Z])/g, "-$1").toLowerCase();
    root.style.setProperty(cssName, value);
  }
}

// Format a packed 0xRRGGBB integer (as carried by TrackSnapshot.color) as a
// CSS hex color string. Inline style assignments like
// `style={{ background: track.color }}` pass the raw integer through, which
// browsers parse as an invalid color (silent no-op). Always format first.
export function colorStr(c: number): string {
  return "#" + (c & 0xffffff).toString(16).padStart(6, "0");
}
