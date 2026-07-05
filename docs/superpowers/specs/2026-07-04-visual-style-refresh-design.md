# Visual Style Refresh — Subtle Aesthetic Improvements

**Date:** 2026-07-04
**Status:** Proposed
**Scope:** Incremental UI polish across Theme.h, widget paint methods, and stylesheet definitions

## Overview

A series of subtle, incremental visual refinements to the existing dark theme with amber accents. These changes preserve the current design language while improving depth perception, interaction feedback, and visual polish. All changes are backward-compatible and can be implemented incrementally.

## Current State

- **Background hierarchy:** #1a1a1e (window) → #222226 (panel) → #2e2e32 (widget) — very close in value, flat appearance
- **Buttons:** Flat fill with border change on hover, no pressed gradient
- **Scrollbars:** 6px wide, simple color change on hover
- **Clips:** Flat fill with 1px shadow, selection uses 2px accent border
- **Typography:** 8pt Segoe UI Semibold, minimal size hierarchy

## Proposed Changes

### 1. Background Hierarchy

**Goal:** Create clearer visual depth between window, panel, and widget layers.

**New palette:**
```
bgWindow()    → #141416  (was #1a1a1e) — darker base
bgPanel()     → #1e1e22  (was #222226) — more distinct from window
bgHeader()    → #1e1e22  (was #222226) — matches panel
bgWidget()    → #2a2a2e  (was #2e2e32) — lighter, more contrast
bgInput()     → #2a2a2e  (was #2e2e32) — matches widget
bgElevated()  → #323236  (was #333338) — slightly refined
bgToolbar()   → rgba(20, 20, 22, 230)  (was rgba(34, 34, 38, 220))
```

**Borders:**
```
border()      → #2a2a2e  (was #3a3a40) — softer
borderLight() → #3a3a40  (was #4a4a50) — medium
```

**Files to modify:**
- `src/ui/Theme.h` — update all `bgXxx()` and `border()` functions
- `getGlobalStyleSheet()` — update hardcoded hex values

### 2. Button Refinement

**Goal:** Softer hover states, subtle gradients on active buttons, better pressed feedback.

**Changes:**
- Default: background `#2a2a2e`, border `#3a3a40`, radius `4px` (was `3px`)
- Hover: background `#323236`, border `#4a4a50`, subtle `box-shadow: 0 1px 2px rgba(0,0,0,0.2)`
- Pressed: background `#1e1e22` (darker, not same as panel)
- Checked: gradient `linear-gradient(180deg, #d97706 0%, #b45309 100%)`, shadow `0 1px 3px rgba(0,0,0,0.3)`

**Files to modify:**
- `src/ui/Theme.h` — `getGlobalStyleSheet()` QPushButton section

### 3. Scrollbar Polish

**Goal:** Thinner, more refined scrollbars with smoother hover transitions.

**Changes:**
- Width/height: `4px` (was `6px`)
- Handle: background `#4a4a50` (was `#3a3a40`), radius `2px` (was `3px`)
- Hover: background `#d97706` (unchanged)
- Track: background `#141416` (was `#1e1e22`)

**Files to modify:**
- `src/ui/Theme.h` — `getGlobalStyleSheet()` QScrollBar section

### 4. Clip Rendering

**Goal:** Better shadows, gradients, and selection highlights.

**Changes in `ClipItem::paint`:**
- Main fill: gradient `color.lighter(130)` at top → `color` at bottom (subtle vertical gradient)
- Shadow: `0 2px 4px rgba(0,0,0,0.3)` instead of `1px 1px 0 rgba(0,0,0,0.8)`
- Inner highlight: `inset 0 1px 0 rgba(255,255,255,0.1)` at top edge
- Selection: `box-shadow: 0 0 0 2px #e8e8ec` (white outline) instead of `2px accent border`
- Name label: add `text-shadow: 0 1px 2px rgba(0,0,0,0.5)` for readability

**Files to modify:**
- `src/ui/ClipItem.cpp` — `ClipItem::paint()` method

### 5. Typography

**Goal:** Better font hierarchy and spacing.

**Changes:**
- Track names: `9pt` (was `8pt`), font-weight `600` (was bold)
- dB labels: `8pt` (unchanged), better vertical spacing
- Channel indicators: `7pt` (was `6pt`), add `letter-spacing: 0.5px`, uppercase format
- Toggle buttons: `7pt` (was `6pt`) for better readability

**Files to modify:**
- `src/ui/TrackHeaderWidget.cpp` — font setup in constructor
- `src/ui/MixerStripWidget.cpp` — font setup in paintEvent

## Implementation Order

1. **Theme.h** — Background hierarchy + borders + global stylesheet (foundational)
2. **TrackHeaderWidget** — Typography + toggle button refinement
3. **MixerStripWidget** — Typography + button refinement
4. **ClipItem.cpp** — Clip rendering improvements
5. **NoteGridWidget** — Note rendering (similar gradient treatment)

## Risk Assessment

- **Low risk:** All changes are visual-only, no logic changes
- **Testing:** Visual inspection across all panels (timeline, mixer, piano roll, FX chain)
- **Rollback:** Each change is isolated in a single function/file

## Success Criteria

- Clearer visual hierarchy between background layers
- More refined interaction feedback on buttons
- Thinner, less intrusive scrollbars
- Clips with better depth and selection visibility
- Improved text readability with better font sizing