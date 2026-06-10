# Theme Modernization Design

## Overview

Modernize HDAW's visual appearance with a curated dark palette, vibrant cyan accent, refined typography, and enhanced component rendering (VU meters, faders, clips, hover states).

## Color Palette

All colors centralized in `src/ui/Theme.h` via `ThemeColors` namespace with inline `QColor()` accessors.

### Backgrounds
| Token | Hex | Description |
|---|---|---|
| `bgWindow` | `#121214` | Deep Charcoal — main window, scroll areas |
| `bgPanel` | `#1c1c1f` | Panel cards, frames |
| `bgWidget` | `#2a2a2e` | Widget surfaces, combo boxes, inputs |
| `bgElevated` | `#2e2e32` | Elevated surfaces, tooltips |
| `bgToolbar` | `#1c1c1f` (220 alpha) | Toolbar frosted glass |

### Borders & Lines
| Token | Hex | Description |
|---|---|---|
| `border` | `#3a3a3e` | Default borders |
| `borderLight` | `#4a4a4e` | Subtle dividers |
| `scrollbarBg` | `#18181b` | Scrollbar track |
| `scrollbarHandle` | `#3a3a3e` | Scrollbar thumb |
| `scrollbarHover` | `#06b6d4` | Scrollbar thumb hover |

### Text
| Token | Hex | Description |
|---|---|---|
| `textPrimary` | `#e4e4e7` | Primary labels, values |
| `textSecondary` | `#a1a1aa` | Secondary info, units |
| `textMuted` | `#71717a` | Disabled, placeholders |

### Accent (Sleek Cyan)
| Token | Hex | Description |
|---|---|---|
| `accent` | `#06b6d4` | Primary accent — selection, faders, active states |
| `accentDim` | `#0891b2` | Dim accent — hover backgrounds |
| `accentBright` | `#22d3ee` | Bright accent — highlights, glow |

### Semantic
| Token | Hex | Description |
|---|---|---|
| `danger` | `#ef4444` | Mute, record arm, clip-over |
| `warning` | `#f59e0b` | Solo, near-clip levels |
| `success` | `#10b981` | Loop markers, play/enabled |

### VU Meter Gradient
| Level | Hex |
|---|---|
| Green (≤ -12 dB) | `#10b981` |
| Amber (-12 to -3 dB) | `#f59e0b` |
| Red (> -3 dB) | `#ef4444` |

### Track Headers
| Token | Hex |
|---|---|
| `trackFill1` | `#2a2a2e` |
| `trackFill2` | `#2e2e32` |
| `trackColor` | `#06b6d4` (40 alpha) |
| `rulerBg` | `#1c1c1f` |

### Timeline & Grid
- Bar lines: `QColor(255, 255, 255, 20)` — 2px
- Beat lines: `QColor(255, 255, 255, 10)` — 1px
- Sub-beat lines: `QColor(255, 255, 255, 5)` — 1px

## Typography

- Family: "Segoe UI Semibold", "Segoe UI", "Arial", sans-serif
- Sizes: 7pt (compact labels), 8pt (default), 9pt (track names), 10pt (timecode)
- Timecode: monospace (`"Consolas", "Courier New", monospace`)

## Component Changes

### Timeline Clips (`ClipItem.cpp`, `AudioClipItem.cpp`, `MidiClipItem.cpp`)
- Rounded corners maintained
- Selection outline: accent cyan `#06b6d4`
- Gain overlay: clip color at 20% opacity drawn as fill
- Clip name: `ThemeColors::textPrimary()`

### Toolbar (`TimelineToolbar.cpp`)
- Background: `bgToolbar` (`#1c1c1f` at 220 alpha) with bottom border `#3a3a3e`
- All button QSS updated to use theme colors
- Hover: `accentDim` at 20% opacity
- Checked/active: `accent` at 30% opacity
- Transport buttons: play checked → success green, record checked → danger red

### VU Meters (`MixerStripWidget.cpp`, `TrackHeaderWidget.cpp`, `VUMeter.cpp`)
- Gradient: green → amber → red at the same dB thresholds
- Peak hold dot: small circle at the highest recent level, decays

### Faders (`MixerStripWidget.cpp`)
- Groove: recessed look (darker inner rect with lighter 1px border)
- Handle: capsule shape (rounded rect, 4px radius), 15×8px, accent cyan
- Handle hover: `accentBright`

### Hover States (buttons across `TrackHeaderWidget.cpp`, `MixerStripWidget.cpp`)
- Mute active: `#ef4444` — hover: 20% brighter
- Solo active: `#f59e0b` — hover: 20% brighter
- Arm active: `#ef4444` — hover: 20% brighter
- Automation active: accent cyan — hover: 20% brighter

### Playhead & Loop Markers
- Playhead: accent cyan `#06b6d4` (was red `#ff5050`)
- Loop markers: left `#10b981` (green), right `#f59e0b` (amber)

### Scrollbars (QSS)
- Track: `#18181b` (darker)
- Handle: 6px width/height, 3px radius, `#3a3a3e`
- Handle hover: `#06b6d4` (accent cyan)

## Implementation Strategy

1. Update `Theme.h` with all new color constants and updated QSS stylesheet
2. Bulk-replace hardcoded `QColor(...)` calls with `ThemeColors::` function calls across all UI files
3. Update custom paint logic:
   - `MixerStripWidget.cpp` — VU gradient, fader capsule handle and recessed groove
   - `TrackHeaderWidget.cpp` — VU gradient, hover glow on buttons
   - `ClipItem.cpp` — gain overlay, accent selection
   - `TimeRuler.cpp` — grid opacity, ruler background
   - `NoteGridWidget.cpp` — grid opacity
   - `TimelineToolbar.cpp` — glass background, updated QSS
   - `PlayheadCursor.cpp` — cyan accent
   - `LoopMarker.cpp` — green/amber loop markers
   - `VUMeter.cpp` — gradient VU

## Non-Goals
- No layout or behavior changes
- No font file bundling (Segoe UI is a system font on Windows)
- No animated transitions beyond simple immediate color shifts
