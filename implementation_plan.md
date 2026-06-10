# Implementation Plan - Theme Modernization

## Overview

Modernize HDAW's visual appearance: new dark palette (Deep Charcoal `#121214`), Sleek Cyan accent (`#06b6d4`), Segoe UI typography, enhanced VU/fader/clip rendering, glassmorphic toolbar, and hover micro-interactions.

## Files to Modify

| File | Change |
|------|--------|
| `src/ui/Theme.h` | New palette values + updated QSS stylesheet |
| `src/ui/MixerStripWidget.cpp` | VU gradient, capsule fader, button hover glow |
| `src/ui/TrackHeaderWidget.cpp` | VU gradient, button hover glow, theme colors |
| `src/ui/ClipItem.cpp` | Accent selection, gain overlay, theme colors |
| `src/ui/TimeRuler.cpp` | Grid opacity, ruler bg, loop markers, theme colors |
| `src/ui/NoteGridWidget.cpp` | Grid opacity, theme colors |
| `src/ui/TimelineToolbar.cpp` | Glass bg, updated QSS, hover/checked states |
| `src/ui/PlayheadCursor.cpp` | Cyan accent |
| `src/ui/LoopMarker.cpp` | Green/amber markers |
| `src/ui/VUMeter.cpp` | Gradient VU with peak hold |
| `src/ui/AutomationLaneWidget.cpp` | Grid opacity, theme colors |
| `src/ui/MidiClipItem.cpp` | Theme colors |
| `src/ui/AudioClipItem.cpp` | Theme colors |
| `src/ui/PianoRollRuler.cpp` | Grid opacity, theme colors |
| `src/ui/PianoKeysWidget.cpp` | Theme colors |
| `src/ui/VelocityLaneWidget.cpp` | Theme colors |
| `src/ui/FXSlotRow.cpp` | Theme colors |
| `src/ui/MixerWidget.cpp` | Theme QSS |
| `src/ui/MainWindow.cpp` | Theme QSS (tab bar buttons) |
| `src/ui/TimelineInteraction.cpp` | Rubber band accent |

## Phase 1: Theme.h Palette + QSS

### New ThemeColors Functions

Replace all color values in `Theme.h`:

```cpp
// Backgrounds
inline QColor bgWindow()     { return QColor(0x12, 0x12, 0x14); } // #121214
inline QColor bgPanel()      { return QColor(0x1c, 0x1c, 0x1f); } // #1c1c1f
inline QColor bgWidget()     { return QColor(0x2a, 0x2a, 0x2e); } // #2a2a2e
inline QColor bgElevated()   { return QColor(0x2e, 0x2e, 0x32); } // #2e2e32
inline QColor bgToolbar()    { return QColor(28, 28, 31, 220); }  // frosted glass

// Borders
inline QColor border()       { return QColor(0x3a, 0x3a, 0x3e); } // #3a3a3e
inline QColor borderLight()  { return QColor(0x4a, 0x4a, 0x4e); } // #4a4a4e

// Text
inline QColor textPrimary()  { return QColor(0xe4, 0xe4, 0xe7); } // #e4e4e7
inline QColor textSecondary(){ return QColor(0xa1, 0xa1, 0xaa); } // #a1a1aa
inline QColor textMuted()    { return QColor(0x71, 0x71, 0x7a); } // #71717a

// Accent — Sleek Cyan
inline QColor accent()       { return QColor(0x06, 0xb6, 0xd4); } // #06b6d4
inline QColor accentDim()    { return QColor(0x08, 0x91, 0xb2); } // #0891b2
inline QColor accentBright() { return QColor(0x22, 0xd3, 0xee); } // #22d3ee

// Semantic
inline QColor danger()       { return QColor(0xef, 0x44, 0x44); } // #ef4444
inline QColor warning()      { return QColor(0xf5, 0x9e, 0x0b); } // #f59e0b
inline QColor success()      { return QColor(0x10, 0xb9, 0x81); } // #10b981

// VU
inline QColor vuGreen()      { return QColor(0x10, 0xb9, 0x81); }
inline QColor vuYellow()     { return QColor(0xf5, 0x9e, 0x0b); }
inline QColor vuRed()        { return QColor(0xef, 0x44, 0x44); }

// Track surfaces
inline QColor trackFill1()   { return QColor(0x2a, 0x2a, 0x2e); }
inline QColor trackFill2()   { return QColor(0x2e, 0x2e, 0x32); }
inline QColor trackColor()   { return QColor(0x06, 0xb6, 0xd4, 40); }
inline QColor rulerBg()      { return QColor(0x1c, 0x1c, 0x1f); }

// Automation
inline QColor automationFill(){ return QColor(0x06, 0xb6, 0xd4, 40); }
inline QColor automationLine(){ return QColor(0x06, 0xb6, 0xd4, 200); }

// Scrollbar
inline QColor scrollbarBg()  { return QColor(0x18, 0x18, 0x1b); }
inline QColor scrollbarHandle(){ return QColor(0x3a, 0x3a, 0x3e); }
inline QColor scrollbarHover(){ return QColor(0x06, 0xb6, 0xd4); }
```

### Updated QSS Stylesheet

Replace all hex colors in `getGlobalStyleSheet()` to match the new palette:
- `#252525` → `#1c1c1f` (widget bg)
- `#1e1e1e` → `#121214` (window bg)
- `#cccccc` → `#e4e4e7` (text)
- `#3a3a3a` → `#3a3a3e` (border)
- `#484848` → `#4a4a4e` (border light)
- `#4488cc` → `#06b6d4` (accent)
- `#353535` → `#2a2a2e` (input bg)
- `#2a2a2a` → `#2e2e32` (elevated bg)
- `#888888` → `#a1a1aa` (secondary text)
- `#666666` → `#71717a` (muted text)

Font: keep `"Segoe UI Semibold", "Segoe UI", "Arial", sans-serif`

Scrollbar QSS: update handle width to 6px, hover color to `#06b6d4`.

## Phase 2: Bulk Color Replacement

Replace hardcoded `QColor(r, g, b)` calls with `ThemeColors::` equivalents in every `.cpp` file listed above. Mapping:

| Old QColor | ThemeColors Replacement |
|---|---|
| `QColor(30, 30, 33)` | `ThemeColors::bgWindow()` |
| `QColor(35, 35, 38)` | `ThemeColors::bgPanel()` |
| `QColor(37, 37, 40)` | `ThemeColors::bgPanel()` |
| `QColor(40, 40, 43)` / `QColor(40, 40, 40)` | `ThemeColors::trackFill1()` |
| `QColor(45, 45, 48)` / `QColor(45, 45, 45)` | `ThemeColors::trackFill2()` |
| `QColor(50, 50, 53)` | `ThemeColors::bgWidget()` |
| `QColor(42, 42, 45)` | `ThemeColors::bgWidget()` |
| `QColor(60, 60, 60)` / `QColor(58, 58, 62)` | `ThemeColors::border()` |
| `QColor(70, 70, 74)` / `QColor(80, 80, 80)` | `ThemeColors::borderLight()` |
| `QColor(0x44, 0x88, 0xCC)` / `#4488cc` | `ThemeColors::accent()` |
| `QColor(55, 100, 155)` / `QColor(60, 100, 160)` | `ThemeColors::accentDim()` |
| `QColor(120, 180, 240)` / `QColor(80, 180, 255)` | `ThemeColors::accentBright()` |
| `QColor(204, 68, 68)` / `QColor(200, 40, 40)` / `QColor(255, 60, 60)` | `ThemeColors::danger()` |
| `QColor(200, 160, 40)` / `QColor(200, 180, 40)` / `QColor(220, 200, 40)` | `ThemeColors::warning()` |
| `QColor(68, 170, 68)` / `QColor(80, 180, 80)` / `QColor(60, 200, 60)` | `ThemeColors::success()` |
| `QColor(160, 160, 160)` / `QColor(136, 136, 136)` / `QColor(120, 120, 120)` | `ThemeColors::textSecondary()` |
| `QColor(180, 180, 180)` | `ThemeColors::textSecondary()` |
| `QColor(204, 204, 204)` | `ThemeColors::textPrimary()` |
| `QColor(100, 100, 100)` | `ThemeColors::textMuted()` |

Add `#include "Theme.h"` to any `.cpp` that doesn't already include it.

## Phase 3: Paint Logic Enhancements

### VU Meters (MixerStripWidget, TrackHeaderWidget, VUMeter)

Replace current discrete color logic with:

```cpp
// Gradient color function
QColor vuColor(float db) {
    if (db > -3.0f) return ThemeColors::vuRed();
    else if (db > -12.0f) return ThemeColors::vuYellow();
    else return ThemeColors::vuGreen();
}
```

Add **peak hold dot**: store `peakLeft` / `peakRight` in widget members. Draw a small circle (2px radius) at the peak level, decaying at 50 dB/s.

### Faders (MixerStripWidget)

Replace current fader drawing with:
- **Groove**: `painter.drawRoundedRect(QRect(faderX, faderY, 8, faderH), 3, 3)` with fill `bgWidget()` and 1px border `border()`
- **Handle**: capsule 15x8px, fill `accent()` when idle, `accentBright()` when hovered, border `accentDim()`
- Track hover via a `bool faderHovered` member, toggled in `mouseMoveEvent` when within handle rect

### Clips (ClipItem)

- Selection border: `QPen(ThemeColors::accent(), 2)`
- Add gain overlay: fill clip rect with `QColor(color.red(), color.green(), color.blue(), 40)` after main fill
- Name text: `ThemeColors::textPrimary()`

### Timeline Grid Lines (TimeRuler, NoteGridWidget, PianoRollRuler, AutomationLaneWidget)

Replace colored grid lines with white-with-alpha:
- Bar lines: `QPen(QColor(255, 255, 255, 20), 2)`
- Beat lines: `QPen(QColor(255, 255, 255, 10), 1)`
- Sub-beat: `QPen(QColor(255, 255, 255, 5), 1)`

### Playhead (PlayheadCursor)

- Triangle fill: `ThemeColors::accent()`
- Line: `QPen(ThemeColors::accent(), 1)`

### Loop Markers (LoopMarker)

- Left marker: `ThemeColors::success()` (green, was `QColor(80, 200, 80)`)
- Right marker: `ThemeColors::warning()` (amber, was `QColor(220, 80, 80)`)

### Toolbar (TimelineToolbar)

- Add `setStyleSheet` for toolbar background: `"background: rgba(28, 28, 31, 220); border-bottom: 1px solid #3a3a3e;"`
- Update all button QSS to use new accent/theme colors
- Play checked: `#10b981` (success)
- Record checked: `#ef4444` (danger)

### Button Hover Glow (TrackHeaderWidget, MixerStripWidget)

For toggle buttons (Mute/Solo/Arm/Auto), when `active`:
- Fill uses semantic color (danger/warning/accent)
- Hover brightens: `color.lighter(130)` → store as inline effect

For MixerStripWidget FX button:
- Add hover tracking using `underMouse()`
- Hover border: `accent()`

### Rubber Band (TimelineInteraction)

- Pen: `QPen(ThemeColors::accent(), 1, Qt::DashLine)`
- Brush: `QColor(ThemeColors::accent().red(), ThemeColors::accent().green(), ThemeColors::accent().blue(), 30)`

## Verification

Build with: `cmake --build build --config Release`

No new warnings should be introduced. All existing widget colors should render with the new palette — launch and visually verify each component.
