# Warm Dark Visual Style Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Unify HDAW's visual identity around a warm dark aesthetic with amber/orange accent, replacing the current cyan accent and fixing all color inconsistencies across the codebase.

**Architecture:** All color changes flow through `Theme.h` — the single source of truth. The palette shifts from cool zinc to warm dark gray, the accent changes from cyan `#06b6d4` to amber `#d97706`, and hardcoded ad-hoc colors are replaced with `ThemeColors::` calls or updated hex values.

**Tech Stack:** C++20, Qt 6 stylesheets, QPainter custom painting, `Theme.h` color functions.

---

## File Map

| Action | File | Purpose |
|--------|------|---------|
| Modify | `src/ui/Theme.h` | New palette, grid line constants, updated stylesheet |
| Modify | `src/ui/AboutDialog.cpp` | Amber accent for title, buttons, links |
| Modify | `src/ui/StartupDialog.cpp` | Amber accent for title, buttons, list selection |
| Modify | `src/ui/PhraseGeneratorDialog.cpp` | Amber accent for title, generate button |
| Modify | `src/ui/PianoKeysWidget.cpp` | Dark-themed piano keys |
| Modify | `src/ui/TrackHeaderWidget.cpp` | Amber selection highlight |
| Modify | `src/ui/TimeRuler.cpp` | Use centralized grid line constants |
| Modify | `src/ui/NoteGridWidget.cpp` | Use centralized grid line constants |
| Modify | `src/ui/PianoRollRuler.cpp` | Use centralized grid line constants |
| Modify | `src/ui/AutomationLaneWidget.cpp` | Use centralized grid line constants |
| Modify | `src/ui/CCLaneWidget.cpp` | Use centralized grid line constants |
| Modify | `src/ui/VelocityLaneWidget.cpp` | Use centralized grid line constants |
| Modify | `src/ui/StepEditorWidget.cpp` | Fix title color to use ThemeColors |
| Modify | `src/ui/PreferencesDialog.cpp` | Fix note color to use ThemeColors |
| Modify | `src/ui/MixerWidget.cpp` | Update hardcoded bg colors to new palette |
| Modify | `src/ui/TimelineToolbar.cpp` | Update hardcoded colors to new palette |
| Modify | `src/ui/AudioClipEditorWidget.cpp` | Update hardcoded text colors to new palette |

---

### Task 1: Theme.h — New Palette and Stylesheet

**Files:**
- Modify: `src/ui/Theme.h`

This is the foundation. Every other task depends on the new palette being in place.

- [ ] **Step 1: Replace the entire `ThemeColors` namespace**

Replace lines 5-53 of `src/ui/Theme.h` with:

```cpp
namespace ThemeColors {
    // Backgrounds (warmer, lighter)
    inline QColor bgWindow()     { static const QColor c(0x1a, 0x1a, 0x1e); return c; }
    inline QColor bgPanel()      { static const QColor c(0x22, 0x22, 0x26); return c; }
    inline QColor bgHeader()     { static const QColor c(0x22, 0x22, 0x26); return c; }
    inline QColor bgWidget()     { static const QColor c(0x2e, 0x2e, 0x32); return c; }
    inline QColor bgInput()      { static const QColor c(0x2e, 0x2e, 0x32); return c; }
    inline QColor bgElevated()   { static const QColor c(0x33, 0x33, 0x38); return c; }
    inline QColor bgToolbar()    { static const QColor c(34, 34, 38, 220); return c; }

    // Borders (slightly warmer)
    inline QColor border()       { static const QColor c(0x3a, 0x3a, 0x40); return c; }
    inline QColor borderLight()  { static const QColor c(0x4a, 0x4a, 0x50); return c; }

    // Text
    inline QColor textPrimary()  { static const QColor c(0xe8, 0xe8, 0xec); return c; }
    inline QColor textSecondary(){ static const QColor c(0xa8, 0xa8, 0xb0); return c; }
    inline QColor textMuted()    { static const QColor c(0x78, 0x78, 0x80); return c; }

    // Accent — Amber/Orange
    inline QColor accent()       { static const QColor c(0xd9, 0x77, 0x06); return c; }
    inline QColor accentDim()    { static const QColor c(0xb4, 0x53, 0x09); return c; }
    inline QColor accentBright() { static const QColor c(0xf5, 0x9e, 0x0b); return c; }

    // Semantic
    inline QColor danger()       { static const QColor c(0xef, 0x44, 0x44); return c; }
    inline QColor warning()      { static const QColor c(0xea, 0xb3, 0x08); return c; }
    inline QColor success()      { static const QColor c(0x10, 0xb9, 0x81); return c; }

    // VU meter gradient
    inline QColor vuGreen()      { static const QColor c(0x10, 0xb9, 0x81); return c; }
    inline QColor vuYellow()     { static const QColor c(0xf5, 0x9e, 0x0b); return c; }
    inline QColor vuRed()        { static const QColor c(0xef, 0x44, 0x44); return c; }

    // Track surfaces
    inline QColor trackFill1()   { static const QColor c(0x28, 0x28, 0x2c); return c; }
    inline QColor trackFill2()   { static const QColor c(0x2c, 0x2c, 0x30); return c; }
    inline QColor trackColor()   { static const QColor c(0xd9, 0x77, 0x06, 40); return c; }
    inline QColor rulerBg()      { static const QColor c(0x22, 0x22, 0x26); return c; }

    // Automation
    inline QColor automationFill(){ static const QColor c(0xd9, 0x77, 0x06, 40); return c; }
    inline QColor automationLine(){ static const QColor c(0xd9, 0x77, 0x06, 200); return c; }

    // Scrollbar
    inline QColor scrollbarBg()  { static const QColor c(0x1e, 0x1e, 0x22); return c; }
    inline QColor scrollbarHandle(){ static const QColor c(0x3a, 0x3a, 0x40); return c; }
    inline QColor scrollbarHover(){ static const QColor c(0xd9, 0x77, 0x06); return c; }

    // Grid lines (centralized — replaces hardcoded QColor(255,255,255,N) scattered across widgets)
    inline QColor gridLineBar()  { static const QColor c(255, 255, 255, 18); return c; }
    inline QColor gridLineBeat() { static const QColor c(255, 255, 255, 8); return c; }
    inline QColor gridLineSub()  { static const QColor c(255, 255, 255, 4); return c; }
}
```

- [ ] **Step 2: Replace the `getGlobalStyleSheet()` function**

Replace the entire `getGlobalStyleSheet()` function (lines 55-222) with:

```cpp
inline QString getGlobalStyleSheet()
{
    return R"(
        QWidget {
            background-color: #222226;
            color: #e8e8ec;
            font-family: "Segoe UI Semibold", "Segoe UI", "Arial", sans-serif;
            font-size: 8pt;
        }
        QMainWindow {
            background-color: #1a1a1e;
        }

        QFrame {
            background-color: #1a1a1e;
            border: 1px solid #3a3a40;
        }

        QPushButton {
            background-color: #2e2e32;
            color: #e8e8ec;
            border: 1px solid #4a4a50;
            border-radius: 3px;
            padding: 3px 8px;
            font-size: 8pt;
        }
        QPushButton:hover {
            background-color: #333338;
            border-color: #d97706;
        }
        QPushButton:pressed {
            background-color: #222226;
        }
        QPushButton:checked {
            background-color: #d97706;
            color: white;
        }
        QPushButton:disabled {
            background-color: #2e2e32;
            color: #787880;
        }

        QComboBox {
            background-color: #2e2e32;
            color: #e8e8ec;
            border: 1px solid #4a4a50;
            border-radius: 3px;
            padding: 2px 6px;
            font-size: 8pt;
        }
        QComboBox:hover {
            border-color: #d97706;
        }
        QComboBox::drop-down {
            border: none;
            width: 18px;
        }
        QComboBox::down-arrow {
            width: 8px;
            height: 8px;
        }
        QComboBox QAbstractItemView {
            background-color: #333338;
            color: #e8e8ec;
            border: 1px solid #3a3a40;
            selection-background-color: #d97706;
            selection-color: white;
            outline: none;
        }

        QLineEdit {
            background-color: #2e2e32;
            color: #e8e8ec;
            border: 1px solid #4a4a50;
            border-radius: 2px;
            padding: 2px 4px;
            font-size: 8pt;
        }
        QLineEdit:focus {
            border-color: #d97706;
        }

        QScrollBar:vertical {
            background: #1e1e22;
            width: 6px;
            border: none;
            margin: 0;
        }
        QScrollBar::handle:vertical {
            background: #3a3a40;
            min-height: 20px;
            border-radius: 3px;
            margin: 1px;
        }
        QScrollBar::handle:vertical:hover {
            background: #d97706;
        }
        QScrollBar::add-line:vertical,
        QScrollBar::sub-line:vertical {
            height: 0;
            border: none;
        }
        QScrollBar:horizontal {
            background: #1e1e22;
            height: 6px;
            border: none;
            margin: 0;
        }
        QScrollBar::handle:horizontal {
            background: #3a3a40;
            min-width: 20px;
            border-radius: 3px;
            margin: 1px;
        }
        QScrollBar::handle:horizontal:hover {
            background: #d97706;
        }
        QScrollBar::add-line:horizontal,
        QScrollBar::sub-line:horizontal {
            width: 0;
            border: none;
        }

        QSplitter::handle {
            background: #3a3a40;
        }
        QSplitter::handle:horizontal {
            width: 2px;
        }
        QSplitter::handle:vertical {
            height: 2px;
        }

        QLabel {
            background: transparent;
            color: #e8e8ec;
            border: none;
        }

        QStatusBar {
            background: #222226;
            color: #a8a8b0;
            border-top: 1px solid #3a3a40;
        }

        QMenu {
            background-color: #333338;
            color: #e8e8ec;
            border: 1px solid #3a3a40;
        }
        QMenu::item:selected {
            background-color: #d97706;
            color: white;
        }

        QToolTip {
            background-color: #2e2e32;
            color: #e8e8ec;
            border: 1px solid #4a4a50;
            padding: 4px;
        }

        QScrollArea {
            border: none;
            background: transparent;
        }
    )";
}
```

- [ ] **Step 3: Build to verify Theme.h compiles**

Run: `cmake --build build --config Debug 2>&1 | Select-String "error"`
Expected: no errors (only pre-existing main.cpp warning).

- [ ] **Step 4: Commit**

```bash
git add src/ui/Theme.h
git commit -m "theme: warm dark palette with amber accent and centralized grid line constants"
```

---

### Task 2: Dialog Fixes — About, Startup, PhraseGenerator

**Files:**
- Modify: `src/ui/AboutDialog.cpp:27,32,39,46,55-57`
- Modify: `src/ui/StartupDialog.cpp:31,39,48-50,63,69-72,101-103`
- Modify: `src/ui/PhraseGeneratorDialog.cpp:27,121,134-136`

- [ ] **Step 1: Fix AboutDialog.cpp**

Replace line 27:
```cpp
    title->setStyleSheet("color: #88bbff;");
```
with:
```cpp
    title->setStyleSheet(QString("color: %1;").arg(ThemeColors::accent().name()));
```

Add `#include "Theme.h"` at the top if not already present (it's not — AboutDialog doesn't include it).

Replace line 32:
```cpp
    subtitle->setStyleSheet("color: #999; font-size: 11px;");
```
with:
```cpp
    subtitle->setStyleSheet(QString("color: %1; font-size: 11px;").arg(ThemeColors::textMuted().name()));
```

Replace line 39 (the email link):
```cpp
        "(<a href='mailto:clinta@gmail.com' style='color:#88bbff;'>clinta@gmail.com</a>)<br><br>"
```
with:
```cpp
        "(<a href='mailto:clinta@gmail.com' style='color:#f59e0b;'>clinta@gmail.com</a>)<br><br>"
```

Replace line 46:
```cpp
    creditLabel->setStyleSheet("color: #ccc; font-size: 12px; line-height: 1.5;");
```
with:
```cpp
    creditLabel->setStyleSheet(QString("color: %1; font-size: 12px; line-height: 1.5;").arg(ThemeColors::textSecondary().name()));
```

Replace lines 54-57:
```cpp
    closeBtn->setStyleSheet(
        "QPushButton { background-color: #2a6fdb; color: white; border: none; "
        "border-radius: 4px; }"
        "QPushButton:hover { background-color: #3a7feb; }");
```
with:
```cpp
    closeBtn->setStyleSheet(QString(
        "QPushButton { background-color: %1; color: white; border: none; "
        "border-radius: 4px; }"
        "QPushButton:hover { background-color: %2; }")
        .arg(ThemeColors::accent().name(), ThemeColors::accentBright().name()));
```

- [ ] **Step 2: Fix StartupDialog.cpp**

Replace line 31:
```cpp
    titleLabel->setStyleSheet("color: #88bbff;");
```
with:
```cpp
    titleLabel->setStyleSheet(QString("color: %1;").arg(ThemeColors::accent().name()));
```

Replace line 39:
```cpp
    subtitleLabel->setStyleSheet("color: #888;");
```
with:
```cpp
    subtitleLabel->setStyleSheet(QString("color: %1;").arg(ThemeColors::textMuted().name()));
```

Replace lines 47-51:
```cpp
    newBtn->setStyleSheet(
        "QPushButton { background-color: #2a6fdb; color: white; border: none; border-radius: 4px; font-size: 14px; font-weight: bold; }"
        "QPushButton:hover { background-color: #3a7feb; }"
        "QPushButton:pressed { background-color: #1a5fcb; }"
    );
```
with:
```cpp
    newBtn->setStyleSheet(QString(
        "QPushButton { background-color: %1; color: white; border: none; border-radius: 4px; font-size: 14px; font-weight: bold; }"
        "QPushButton:hover { background-color: %2; }"
        "QPushButton:pressed { background-color: %3; }")
        .arg(ThemeColors::accent().name(), ThemeColors::accentBright().name(), ThemeColors::accentDim().name()));
```

Replace line 63:
```cpp
    recentLabel->setStyleSheet("color: #bbb;");
```
with:
```cpp
    recentLabel->setStyleSheet(QString("color: %1;").arg(ThemeColors::textPrimary().name()));
```

Replace lines 68-73:
```cpp
    recentList->setStyleSheet(
        "QListWidget { background-color: #252530; border: 1px solid #3a3a45; border-radius: 4px; }"
        "QListWidget::item { padding: 8px; color: #ccc; }"
        "QListWidget::item:hover { background-color: #333040; }"
        "QListWidget::item:selected { background-color: #2a6fdb; color: white; }"
    );
```
with:
```cpp
    recentList->setStyleSheet(QString(
        "QListWidget { background-color: %1; border: 1px solid %2; border-radius: 4px; }"
        "QListWidget::item { padding: 8px; color: %3; }"
        "QListWidget::item:hover { background-color: %4; }"
        "QListWidget::item:selected { background-color: %5; color: white; }")
        .arg(ThemeColors::bgWidget().name(), ThemeColors::border().name(),
             ThemeColors::textSecondary().name(), ThemeColors::bgElevated().name(),
             ThemeColors::accent().name()));
```

Replace lines 100-104:
```cpp
    openBtn->setStyleSheet(
        "QPushButton { background-color: #353540; color: #ccc; border: 1px solid #4a4a55; border-radius: 4px; font-size: 13px; }"
        "QPushButton:hover { background-color: #454550; }"
        "QPushButton:pressed { background-color: #2a2a35; }"
    );
```
with:
```cpp
    openBtn->setStyleSheet(QString(
        "QPushButton { background-color: %1; color: %2; border: 1px solid %3; border-radius: 4px; font-size: 13px; }"
        "QPushButton:hover { background-color: %4; }"
        "QPushButton:pressed { background-color: %5; }")
        .arg(ThemeColors::bgWidget().name(), ThemeColors::textSecondary().name(),
             ThemeColors::borderLight().name(), ThemeColors::bgElevated().name(),
             ThemeColors::bgPanel().name()));
```

- [ ] **Step 3: Fix PhraseGeneratorDialog.cpp**

Replace line 27:
```cpp
    title->setStyleSheet("color: #88bbff;");
```
with:
```cpp
    title->setStyleSheet(QString("color: %1;").arg(ThemeColors::accent().name()));
```

Replace line 121:
```cpp
    previewLabel->setStyleSheet("color: #888; font-size: 10px;");
```
with:
```cpp
    previewLabel->setStyleSheet(QString("color: %1; font-size: 10px;").arg(ThemeColors::textMuted().name()));
```

Replace lines 133-136:
```cpp
    previewBtn->setStyleSheet(
        "QPushButton { background-color: #2a6fdb; color: white; border: none; "
        "border-radius: 4px; font-weight: bold; }"
        "QPushButton:hover { background-color: #3a7feb; }");
```
with:
```cpp
    previewBtn->setStyleSheet(QString(
        "QPushButton { background-color: %1; color: white; border: none; "
        "border-radius: 4px; font-weight: bold; }"
        "QPushButton:hover { background-color: %2; }")
        .arg(ThemeColors::accent().name(), ThemeColors::accentBright().name()));
```

- [ ] **Step 4: Build and verify**

Run: `cmake --build build --config Debug`
Expected: clean compile.

- [ ] **Step 5: Commit**

```bash
git add src/ui/AboutDialog.cpp src/ui/StartupDialog.cpp src/ui/PhraseGeneratorDialog.cpp
git commit -m "ui: replace off-palette blue with amber accent in dialogs"
```

---

### Task 3: Dark Piano Keys

**Files:**
- Modify: `src/ui/PianoKeysWidget.cpp:54,60,77-78,80`

- [ ] **Step 1: Update white key colors**

Replace line 54:
```cpp
        painter.setBrush((octave % 2 == 0) ? QColor(220, 220, 220) : QColor(200, 200, 200));
```
with:
```cpp
        painter.setBrush((octave % 2 == 0) ? QColor(60, 60, 66) : QColor(52, 52, 58));
```

- [ ] **Step 2: Update C label color**

Replace line 60:
```cpp
            painter.setPen(QColor(80, 80, 80));
```
with:
```cpp
            painter.setPen(ThemeColors::textMuted());
```

- [ ] **Step 3: Update black key colors**

Replace lines 77-78:
```cpp
        painter.setPen(QPen(QColor(20, 20, 20), 1));
        painter.setBrush(QColor(40, 40, 40));
```
with:
```cpp
        painter.setPen(QPen(QColor(20, 20, 24), 1));
        painter.setBrush(QColor(30, 30, 34));
```

Replace line 80:
```cpp
        painter.setPen(QColor(20, 20, 20));
```
with:
```cpp
        painter.setPen(QColor(20, 20, 24));
```

- [ ] **Step 4: Build and verify**

Run: `cmake --build build --config Debug`
Expected: clean compile.

- [ ] **Step 5: Commit**

```bash
git add src/ui/PianoKeysWidget.cpp
git commit -m "ui: dark-themed piano keys to match warm dark aesthetic"
```

---

### Task 4: Amber Selection Highlight in TrackHeaderWidget

**Files:**
- Modify: `src/ui/TrackHeaderWidget.cpp:296-297`

- [ ] **Step 1: Update selection colors**

Replace line 296:
```cpp
            painter.fillRect(row, QColor(80, 160, 255, 60));
```
with:
```cpp
            painter.fillRect(row, QColor(217, 119, 6, 50));
```

Replace line 297:
```cpp
            painter.setPen(QPen(QColor(80, 160, 255), 2));
```
with:
```cpp
            painter.setPen(QPen(ThemeColors::accent(), 2));
```

- [ ] **Step 2: Build and verify**

Run: `cmake --build build --config Debug`
Expected: clean compile.

- [ ] **Step 3: Commit**

```bash
git add src/ui/TrackHeaderWidget.cpp
git commit -m "ui: amber selection highlight in track headers"
```

---

### Task 5: Centralize Grid Lines

**Files:**
- Modify: `src/ui/TimeRuler.cpp:167,177`
- Modify: `src/ui/NoteGridWidget.cpp:140,148,153,165,172`
- Modify: `src/ui/PianoRollRuler.cpp:40`
- Modify: `src/ui/AutomationLaneWidget.cpp:272`
- Modify: `src/ui/CCLaneWidget.cpp:54`
- Modify: `src/ui/VelocityLaneWidget.cpp:61`

Replace all hardcoded `QColor(255, 255, 255, N)` grid line colors with the appropriate `ThemeColors::gridLineBar()`, `gridLineBeat()`, or `gridLineSub()`.

- [ ] **Step 1: Fix TimeRuler.cpp**

Replace line 167:
```cpp
        painter->setPen(isBar ? QColor(255, 255, 255, 20) : QColor(255, 255, 255, 10));
```
with:
```cpp
        painter->setPen(isBar ? ThemeColors::gridLineBar() : ThemeColors::gridLineBeat());
```

Replace line 177:
```cpp
                painter->setPen(QColor(255, 255, 255, 5));
```
with:
```cpp
                painter->setPen(ThemeColors::gridLineSub());
```

- [ ] **Step 2: Fix NoteGridWidget.cpp**

Replace line 140:
```cpp
    painter.setPen(QPen(QColor(255, 255, 255, 3), 1));
```
with:
```cpp
    painter.setPen(QPen(ThemeColors::gridLineSub(), 1));
```

Replace line 148:
```cpp
            painter.setPen(QPen(QColor(255, 255, 255, 8), 1));
```
with:
```cpp
            painter.setPen(QPen(ThemeColors::gridLineBeat(), 1));
```

Replace line 153:
```cpp
            painter.setPen(QPen(QColor(255, 255, 255, 5), 1));
```
with:
```cpp
            painter.setPen(QPen(ThemeColors::gridLineSub(), 1));
```

Replace line 165:
```cpp
        painter.setPen(QPen(isBar ? QColor(255, 255, 255, 12) : QColor(255, 255, 255, 6), isBar ? 2 : 1));
```
with:
```cpp
        painter.setPen(QPen(isBar ? ThemeColors::gridLineBar() : ThemeColors::gridLineBeat(), isBar ? 2 : 1));
```

Replace line 172:
```cpp
        painter.setPen(QPen(QColor(255, 255, 255, 4), 1));
```
with:
```cpp
        painter.setPen(QPen(ThemeColors::gridLineSub(), 1));
```

- [ ] **Step 3: Fix PianoRollRuler.cpp**

Replace line 40:
```cpp
        painter.setPen(isBar ? QColor(255, 255, 255, 20) : QColor(255, 255, 255, 10));
```
with:
```cpp
        painter.setPen(isBar ? ThemeColors::gridLineBar() : ThemeColors::gridLineBeat());
```

- [ ] **Step 4: Fix AutomationLaneWidget.cpp**

Replace line 272:
```cpp
    painter.setPen(QPen(QColor(255, 255, 255, 6), 1));
```
with:
```cpp
    painter.setPen(QPen(ThemeColors::gridLineBeat(), 1));
```

- [ ] **Step 5: Fix CCLaneWidget.cpp**

Replace line 54:
```cpp
        painter.setPen(QPen(QColor(255, 255, 255, 6), 1));
```
with:
```cpp
        painter.setPen(QPen(ThemeColors::gridLineBeat(), 1));
```

- [ ] **Step 6: Fix VelocityLaneWidget.cpp**

Replace line 61:
```cpp
        painter.setPen(QPen(QColor(255, 255, 255, 6), 1));
```
with:
```cpp
        painter.setPen(QPen(ThemeColors::gridLineBeat(), 1));
```

- [ ] **Step 7: Build and verify**

Run: `cmake --build build --config Debug`
Expected: clean compile.

- [ ] **Step 8: Commit**

```bash
git add src/ui/TimeRuler.cpp src/ui/NoteGridWidget.cpp src/ui/PianoRollRuler.cpp src/ui/AutomationLaneWidget.cpp src/ui/CCLaneWidget.cpp src/ui/VelocityLaneWidget.cpp
git commit -m "ui: centralize grid line colors in ThemeColors"
```

---

### Task 6: Minor Color Fixes

**Files:**
- Modify: `src/ui/StepEditorWidget.cpp:31`
- Modify: `src/ui/PreferencesDialog.cpp:65`
- Modify: `src/ui/MixerWidget.cpp:20,33`
- Modify: `src/ui/TimelineToolbar.cpp:24,105,114,125,134,147,198,206`
- Modify: `src/ui/AudioClipEditorWidget.cpp:66,70,76,88,92,105,118,122,135`

- [ ] **Step 1: Fix StepEditorWidget.cpp**

Replace line 31:
```cpp
    titleLabel->setStyleSheet("color: #ccc; font-weight: bold;");
```
with:
```cpp
    titleLabel->setStyleSheet(QString("color: %1; font-weight: bold;").arg(ThemeColors::textPrimary().name()));
```

Add `#include "Theme.h"` if not already present.

- [ ] **Step 2: Fix PreferencesDialog.cpp**

Replace line 65:
```cpp
    mcpHostNote->setStyleSheet("color: #a0a0a0;");
```
with:
```cpp
    mcpHostNote->setStyleSheet(QString("color: %1;").arg(ThemeColors::textSecondary().name()));
```

- [ ] **Step 3: Fix MixerWidget.cpp**

Replace line 20:
```cpp
    scrollArea->setStyleSheet("QScrollArea { background-color: #121214; }");
```
with:
```cpp
    scrollArea->setStyleSheet(QString("QScrollArea { background-color: %1; }").arg(ThemeColors::bgWindow().name()));
```

Replace line 33:
```cpp
    masterWidget->setStyleSheet("background-color: #1c1c1f;");
```
with:
```cpp
    masterWidget->setStyleSheet(QString("background-color: %1;").arg(ThemeColors::bgPanel().name()));
```

- [ ] **Step 4: Fix TimelineToolbar.cpp**

Replace line 24:
```cpp
        "QToolButton:hover { border-color: #06b6d4; }");
```
with:
```cpp
        "QToolButton:hover { border-color: #d97706; }");
```

Replace line 105:
```cpp
        "QDoubleSpinBox { background: #121214; color: #e4e4e7; border: 1px solid #3a3a3e; "
```
with:
```cpp
        "QDoubleSpinBox { background: #1a1a1e; color: #e8e8ec; border: 1px solid #3a3a40; "
```

Replace line 114:
```cpp
        "QLabel { color: #a1a1aa; font-family: monospace; font-size: 8pt; "
```
with:
```cpp
        "QLabel { color: #a8a8b0; font-family: monospace; font-size: 8pt; "
```

Replace line 125:
```cpp
        "QPushButton { color: #a1a1aa; }"
```
with:
```cpp
        "QPushButton { color: #a8a8b0; }"
```

Replace line 134:
```cpp
    clipLenLabel->setStyleSheet("QLabel { color: #a1a1aa; font-size: 7pt; }");
```
with:
```cpp
    clipLenLabel->setStyleSheet("QLabel { color: #a8a8b0; font-size: 7pt; }");
```

Replace line 147:
```cpp
        "QDoubleSpinBox { background: #121214; color: #e4e4e7; border: 1px solid #3a3a3e; "
```
with:
```cpp
        "QDoubleSpinBox { background: #1a1a1e; color: #e8e8ec; border: 1px solid #3a3a40; "
```

Replace line 198:
```cpp
        "QPushButton { color: #a1a1aa; font-weight: bold; }"
```
with:
```cpp
        "QPushButton { color: #a8a8b0; font-weight: bold; }"
```

Replace line 206:
```cpp
        "QLabel { color: #e4e4e7; font-family: monospace; font-size: 10pt; "
```
with:
```cpp
        "QLabel { color: #e8e8ec; font-family: monospace; font-size: 10pt; "
```

- [ ] **Step 5: Fix AudioClipEditorWidget.cpp**

Replace line 66:
```cpp
    sourceLabel->setStyleSheet("color: #71717a; font-size: 7pt;");
```
with:
```cpp
    sourceLabel->setStyleSheet("color: #787880; font-size: 7pt;");
```

Replace line 70:
```cpp
    infoLabel->setStyleSheet("color: #71717a; font-size: 7pt;");
```
with:
```cpp
    infoLabel->setStyleSheet("color: #787880; font-size: 7pt;");
```

Replace line 76:
```cpp
    gainLbl->setStyleSheet("color: #a1a1aa; font-size: 7pt;");
```
with:
```cpp
    gainLbl->setStyleSheet("color: #a8a8b0; font-size: 7pt;");
```

Replace line 88:
```cpp
    gainLabel->setStyleSheet("color: #e4e4e7; font-size: 7pt;");
```
with:
```cpp
    gainLabel->setStyleSheet("color: #e8e8ec; font-size: 7pt;");
```

Replace line 92:
```cpp
    fadeInLbl->setStyleSheet("color: #a1a1aa; font-size: 7pt;");
```
with:
```cpp
    fadeInLbl->setStyleSheet("color: #a8a8b0; font-size: 7pt;");
```

Replace line 105:
```cpp
    fadeOutLbl->setStyleSheet("color: #a1a1aa; font-size: 7pt;");
```
with:
```cpp
    fadeOutLbl->setStyleSheet("color: #a8a8b0; font-size: 7pt;");
```

Replace line 118:
```cpp
    loopCheck->setStyleSheet("color: #a1a1aa; font-size: 7pt;");
```
with:
```cpp
    loopCheck->setStyleSheet("color: #a8a8b0; font-size: 7pt;");
```

Replace line 122:
```cpp
    offsetLbl->setStyleSheet("color: #a1a1aa; font-size: 7pt;");
```
with:
```cpp
    offsetLbl->setStyleSheet("color: #a8a8b0; font-size: 7pt;");
```

Replace line 135:
```cpp
    durLbl->setStyleSheet("color: #a1a1aa; font-size: 7pt;");
```
with:
```cpp
    durLbl->setStyleSheet("color: #a8a8b0; font-size: 7pt;");
```

- [ ] **Step 6: Build and verify**

Run: `cmake --build build --config Debug`
Expected: clean compile.

- [ ] **Step 7: Commit**

```bash
git add src/ui/StepEditorWidget.cpp src/ui/PreferencesDialog.cpp src/ui/MixerWidget.cpp src/ui/TimelineToolbar.cpp src/ui/AudioClipEditorWidget.cpp
git commit -m "ui: update hardcoded colors to match warm dark palette"
```

---

### Task 7: Verification

- [ ] **Step 1: Grep for remaining old colors**

Run:
```bash
rg "#06b6d4|#0891b2|#22d3ee|#88bbff|#2a6fdb|#3a7feb|#1a5fcb" src/ui/ --include "*.cpp" --include "*.h"
```
Expected: no matches (all old cyan and blue colors replaced).

- [ ] **Step 2: Grep for remaining hardcoded grid lines**

Run:
```bash
rg "QColor\(255, 255, 255," src/ui/ --include "*.cpp"
```
Expected: only `AudioClipItem.cpp:97` and `AudioWaveformWidget.cpp:166,171` (these are waveform fallback fills, not grid lines — leave as-is).

- [ ] **Step 3: Full build**

Run: `cmake --build build --config Debug`
Expected: clean compile.

- [ ] **Step 4: Run tests**

Run: `build\Debug\hdaw_tests.exe`
Expected: all tests pass (color changes don't affect logic).

- [ ] **Step 5: Final commit if needed**

```bash
git add -A
git commit -m "ui: warm dark visual style overhaul complete"
```
