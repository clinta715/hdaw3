# HDAW Visual Style Overhaul — "Warm Dark" Design Spec

**Goal:** Unify HDAW's visual identity around a warm, dark, techno-futurist aesthetic inspired by FL Studio's dark theme. Replace the current cyan accent with amber/orange, lighten backgrounds, fix inconsistencies, and centralize all color definitions in `Theme.h`.

**Date:** 2026-06-30

---

## 1. Design Direction

**Reference:** FL Studio dark theme — rich dark grays, warm amber accents, solid fill buttons, professional depth.

**Principles:**
- **Warm dark** — backgrounds shift from cool zinc (blue tint) to warm dark gray
- **Amber accent** — primary accent changes from cyan `#06b6d4` to amber `#d97706`
- **Mostly flat** — subtle depth via borders and inset surfaces, no heavy skeuomorphism
- **Consistent** — all colors flow from `Theme.h`, no hardcoded ad-hoc values
- **Professional** — Segoe UI Semibold stays, clean typography, no gimmicks

---

## 2. Color Palette

### 2.1 Backgrounds

| Function | Current | New | Usage |
|----------|---------|-----|-------|
| `bgWindow()` | `#121214` | `#1a1a1e` | Main window, deepest background |
| `bgPanel()` | `#1c1c1f` | `#222226` | Panel surfaces (mixer, headers, ruler) |
| `bgHeader()` | `#1c1c1f` | `#222226` | Same as bgPanel |
| `bgWidget()` | `#2a2a2e` | `#2e2e32` | Button faces, input backgrounds |
| `bgInput()` | `#2a2a2e` | `#2e2e32` | Same as bgWidget |
| `bgElevated()` | `#2e2e32` | `#333338` | Menus, hover states, tooltips |
| `bgToolbar()` | `rgba(28,28,31,220)` | `rgba(34,34,38,220)` | Semi-transparent toolbar |

### 2.2 Accent Colors

| Function | Current | New | Usage |
|----------|---------|-----|-------|
| `accent()` | `#06b6d4` (cyan) | `#d97706` (amber) | Primary accent — selection, faders, active states |
| `accentDim()` | `#0891b2` | `#b45309` | Darker amber — automation fill, inactive accent |
| `accentBright()` | `#22d3ee` | `#f59e0b` | Bright amber — highlights, FX button text, loop markers |

### 2.3 Semantic Colors (unchanged except warning)

| Function | Current | New | Usage |
|----------|---------|-----|-------|
| `danger()` | `#ef4444` | `#ef4444` | Record arm, mute, VU red |
| `warning()` | `#f59e0b` | `#eab308` | Solo, VU yellow (shifted to yellow to avoid clash with amber accent) |
| `success()` | `#10b981` | `#10b981` | Play, metronome, VU green |

### 2.4 VU Meter (unchanged)

| Function | Value |
|----------|-------|
| `vuGreen()` | `#10b981` |
| `vuYellow()` | `#f59e0b` |
| `vuRed()` | `#ef4444` |

### 2.5 Borders

| Function | Current | New | Usage |
|----------|---------|-----|-------|
| `border()` | `#3a3a3e` | `#3a3a40` | Standard borders, dividers |
| `borderLight()` | `#4a4a4e` | `#4a4a50` | Hover states, button outlines |

### 2.6 Text

| Function | Current | New | Usage |
|----------|---------|-----|-------|
| `textPrimary()` | `#e4e4e7` | `#e8e8ec` | Main text |
| `textSecondary()` | `#a1a1aa` | `#a8a8b0` | Labels, secondary info |
| `textMuted()` | `#71717a` | `#787880` | Disabled, tertiary text |

### 2.7 Track Rows

| Function | Current | New | Usage |
|----------|---------|-----|-------|
| `trackFill1()` | `#2a2a2e` | `#28282c` | Even track rows |
| `trackFill2()` | `#2e2e32` | `#2c2c30` | Odd track rows |
| `trackColor()` | `rgba(6,182,212,40)` | `rgba(217,119,6,40)` | Track tint overlay (amber) |
| `rulerBg()` | `#1c1c1f` | `#222226` | Ruler background |

### 2.8 Automation

| Function | Current | New | Usage |
|----------|---------|-----|-------|
| `automationFill()` | `rgba(6,182,212,40)` | `rgba(217,119,6,40)` | Automation curve fill (amber) |
| `automationLine()` | `rgba(6,182,212,200)` | `rgba(217,119,6,200)` | Automation curve line (amber) |

### 2.9 Scrollbar

| Function | Current | New | Usage |
|----------|---------|-----|-------|
| `scrollbarBg()` | `#18181b` | `#1e1e22` | Scrollbar track |
| `scrollbarHandle()` | `#3a3a3e` | `#3a3a40` | Scrollbar thumb |
| `scrollbarHover()` | `#06b6d4` | `#d97706` | Scrollbar hover (amber) |

### 2.10 NEW: Grid Lines (currently hardcoded as `QColor(255,255,255,N)`)

| Function | Value | Usage |
|----------|-------|-------|
| `gridLineBar()` | `QColor(255, 255, 255, 18)` | Bar lines in TimeRuler, NoteGrid, PianoRollRuler |
| `gridLineBeat()` | `QColor(255, 255, 255, 8)` | Beat lines |
| `gridLineSub()` | `QColor(255, 255, 255, 4)` | Sub-beat/snap grid lines |

---

## 3. Global Stylesheet Changes

The global stylesheet in `Theme.h::getGlobalStyleSheet()` needs these updates:

- **QWidget**: background `#222226` (was `#1c1c1f`), font unchanged
- **QMainWindow**: background `#1a1a1e` (was `#121214`)
- **QFrame**: background `#1a1a1e` (was `#121214`), border `#3a3a40`
- **QPushButton**: border `#4a4a50`, hover border `#d97706` (was cyan), checked bg `#d97706`
- **QComboBox**: border `#4a4a50`, hover border `#d97706`, selected `#d97706`
- **QLineEdit**: border `#4a4a50`, focus border `#d97706`
- **QScrollBar**: bg `#1e1e22`, handle `#3a3a40`, hover `#d97706`
- **QMenu**: bg `#333338`, selected `#d97706`
- **QToolTip**: bg `#2e2e32`, border `#4a4a50`
- **QStatusBar**: bg `#222226`, text `#a8a8b0`, border `#3a3a40`

---

## 4. Widget-Specific Fixes

### 4.1 Piano Keys (`PianoKeysWidget.cpp`)

**Problem:** Light-colored keys clash with dark theme.

| Element | Current | New |
|---------|---------|-----|
| White keys | `QColor(220, 220, 220)` | `QColor(60, 60, 66)` (dark gray) |
| White key alt | `QColor(200, 200, 200)` | `QColor(52, 52, 58)` (slightly darker) |
| Black keys | `QColor(40, 40, 40)` | `QColor(30, 30, 34)` (darker) |
| Black key pen | `QColor(20, 20, 20)` | `QColor(20, 20, 24)` |
| C label text | `QColor(80, 80, 80)` | `ThemeColors::textMuted()` |

### 4.2 Dialogs — About, Startup, PhraseGenerator

**Problem:** Use off-palette blue `#88bbff` / `#2a6fdb`.

| Element | Current | New |
|---------|---------|-----|
| Title "HDAW" color | `#88bbff` | `ThemeColors::accent()` (`#d97706`) |
| Subtitle color | `#888` / `#999` | `ThemeColors::textMuted()` (`#787880`) |
| Credits text | `#ccc` | `ThemeColors::textSecondary()` (`#a8a8b0`) |
| Primary button bg | `#2a6fdb` | `ThemeColors::accent()` (`#d97706`) |
| Primary button hover | `#3a7feb` | `ThemeColors::accentBright()` (`#f59e0b`) |
| Primary button pressed | `#1a5fcb` | `#b45309` (`accentDim()`) |
| Link color | `#88bbff` | `ThemeColors::accentBright()` (`#f59e0b`) |

### 4.3 Track Selection Highlight (`TrackHeaderWidget.cpp`)

**Problem:** Uses blue `QColor(80, 160, 255)` instead of amber.

| Element | Current | New |
|---------|---------|-----|
| Selection fill | `QColor(80, 160, 255, 60)` | `QColor(217, 119, 6, 50)` |
| Selection border | `QColor(80, 160, 255)` | `ThemeColors::accent()` |

### 4.4 Loop Region Colors (`TimeRuler.cpp`)

Currently uses cyan. Change to amber:

| Element | Current | New |
|---------|---------|-----|
| Loop fill | `ThemeColors::accent()` alpha 60 | Same (now amber) |
| Loop border | `ThemeColors::accent()` | Same (now amber) |
| Loop label | `ThemeColors::accent()` | Same (now amber) |

No code change needed — these already use `ThemeColors::accent()` which will automatically shift.

### 4.5 Clip Selection (`ClipItem.cpp`)

Already uses `ThemeColors::accent()`. Will automatically shift to amber.

### 4.6 Note Grid (`NoteGridWidget.cpp`)

Already uses `ThemeColors::accent()` for notes, playhead, rubber band. Will automatically shift. Grid lines should be updated to use the new centralized constants.

### 4.7 Automation Lane (`AutomationLaneWidget.cpp`)

Already uses `ThemeColors::automationLine()` / `automationFill()`. Will automatically shift.

### 4.8 Step Editor (`StepEditorWidget.cpp`)

Minor: title label `color: #ccc` should become `ThemeColors::textPrimary()`.

### 4.9 Preferences Dialog (`PreferencesDialog.cpp`)

Minor: note label `color: #a0a0a0` should become `ThemeColors::textSecondary()`.

---

## 5. Files to Modify

| File | Changes |
|------|---------|
| `src/ui/Theme.h` | New palette, new grid line constants, updated stylesheet |
| `src/ui/AboutDialog.cpp` | Amber accent for title, buttons, links |
| `src/ui/StartupDialog.cpp` | Amber accent for title, buttons, selected items |
| `src/ui/PhraseGeneratorDialog.cpp` | Amber accent for title, generate button |
| `src/ui/PianoKeysWidget.cpp` | Dark-themed piano keys |
| `src/ui/TrackHeaderWidget.cpp` | Amber selection highlight |
| `src/ui/TimeRuler.cpp` | Use centralized grid line constants |
| `src/ui/NoteGridWidget.cpp` | Use centralized grid line constants |
| `src/ui/PianoRollRuler.cpp` | Use centralized grid line constants |
| `src/ui/AutomationLaneWidget.cpp` | Use centralized grid line constants |
| `src/ui/StepEditorWidget.cpp` | Fix title color to use ThemeColors |
| `src/ui/PreferencesDialog.cpp` | Fix note color to use ThemeColors |
| `src/ui/TimelineToolbar.cpp` | Verify colors match new palette (most already do) |
| `src/ui/MixerStripWidget.cpp` | Verify VU bg, no changes expected |
| `src/ui/ClipItem.cpp` | No changes expected (uses ThemeColors) |

---

## 6. What Does NOT Change

- **Font family and size** — Segoe UI Semibold 8pt stays
- **Button shape** — 3px border-radius, solid fill
- **Layout** — no structural changes
- **Custom painting logic** — only color values change
- **JUCE audio code** — no engine changes
- **MCP tools** — no API changes
- **Plugin scanner** — no changes

---

## 7. Testing

- Visual inspection of every major UI surface after the change
- Verify all hardcoded colors are replaced (grep for `QColor(255`, `#88bbff`, `#2a6fdb`, `#06b6d4`, `#0891b2`, `#22d3ee`)
- Run full test suite — no tests should break (they test logic, not colors)
- Verify the app starts and all panels render correctly
