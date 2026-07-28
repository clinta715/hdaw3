// frontend/src/utils/trackTypes.ts
//
// Shared track-type metadata so the header, the add-track menu, and the type
// chips all agree on icons, labels, and colors.

export const TRACK_TYPE_ICONS: Record<number, string> = {
  0: "\u25B2", // audio: triangle
  1: "\u266B", // instrument: music note
  2: "\u25BC", // folder: down triangle
};

export const TRACK_TYPE_LABELS: Record<number, string> = {
  0: "AUDIO",
  1: "MIDI",
  2: "FOLDER",
};

export const TRACK_TYPE_CLASSES: Record<number, string> = {
  0: "audio",
  1: "instrument",
  2: "folder",
};

// Distinct, dark-background-friendly hues so AUDIO and MIDI tracks read as
// different at a glance (the distinction is logical, but humans need it).
export const TRACK_TYPE_COLORS: Record<number, string> = {
  0: "#4a9eff", // audio: blue
  1: "#9b59b6", // instrument/MIDI: purple
  2: "#f39c12", // folder: orange
};
