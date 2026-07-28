import React from "react";

// Small monochrome icon set for the transport bar. Drawn with currentColor so
// each icon inherits its button's text color and lights up on hover/active
// without per-icon color rules. Replaces the color emoji (🎛️ 🎵 ⚙️ 📁) that
// rendered inconsistently across platforms and clashed with the dark theme.

interface IconProps {
  size?: number;
}

const base = (size: number): React.SVGProps<SVGSVGElement> => ({
  width: size,
  height: size,
  viewBox: "0 0 24 24",
  fill: "none",
  stroke: "currentColor",
  strokeWidth: 2,
  strokeLinecap: "round",
  strokeLinejoin: "round",
  "aria-hidden": true,
});

export const FolderIcon: React.FC<IconProps> = ({ size = 15 }) => (
  <svg {...base(size)}>
    <path d="M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V7z" />
  </svg>
);

export const KnobIcon: React.FC<IconProps> = ({ size = 15 }) => (
  <svg {...base(size)}>
    <circle cx="12" cy="13" r="7" />
    <path d="M12 13V8" />
    <path d="M5 5l1.5 1.5" />
    <path d="M19 5l-1.5 1.5" />
  </svg>
);

export const NoteIcon: React.FC<IconProps> = ({ size = 15 }) => (
  <svg {...base(size)}>
    <path d="M9 18V6l10-2v11" />
    <circle cx="6.5" cy="18" r="2.5" />
    <circle cx="16.5" cy="15" r="2.5" />
  </svg>
);

export const SlidersIcon: React.FC<IconProps> = ({ size = 15 }) => (
  <svg {...base(size)}>
    <path d="M4 8h9" />
    <path d="M17 8h3" />
    <circle cx="15" cy="8" r="2" />
    <path d="M4 16h3" />
    <path d="M11 16h9" />
    <circle cx="9" cy="16" r="2" />
  </svg>
);
