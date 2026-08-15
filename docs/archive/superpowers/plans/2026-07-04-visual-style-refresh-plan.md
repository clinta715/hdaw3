# Visual Style Refresh Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Apply 5 subtle aesthetic refinements to the existing dark theme — background hierarchy, button polish, scrollbar thinning, clip rendering, and typography improvements.

**Architecture:** Centralized in `Theme.h` for palette/global stylesheet, with targeted paint-method edits in `TrackHeaderWidget`, `MixerStripWidget`, `ClipItem`, and `NoteGridWidget`. Each task is self-contained and visually verifiable.

**Tech Stack:** Qt 6 QPainter, QSS stylesheets, C++17

---

## File Structure

| File | Responsibility |
|------|----------------|
| `src/ui/Theme.h` | Central palette functions + global stylesheet |
| `src/ui/TrackHeaderWidget.cpp` | Track header paint, font setup |
| `src/ui/MixerStripWidget.cpp` | Mixer strip paint, font setup |
| `src/ui/ClipItem.cpp` | Clip rendering (shadow, gradient, selection) |
| `src/ui/NoteGridWidget.cpp` | Note rendering (gradient treatment) |

---

### Task 1: Update Theme.h — Background Hierarchy & Borders

**Files:**
- Modify: `src/ui/Theme.h:7-18` (background functions)
- Modify: `src/ui/Theme.h:16-17` (border functions)

- [ ] **Step 1: Update background color functions**

In `src/ui/Theme.h`, replace the background function bodies:

```cpp
// Backgrounds (deeper hierarchy)
inline QColor bgWindow()     { static const QColor c(0x14, 0x14, 0x16); return c; }
inline QColor bgPanel()      { static const QColor c(0x1e, 0x1e, 0x22); return c; }
inline QColor bgHeader()     { static const QColor c(0x1e, 0x1e, 0x22); return c; }
inline QColor bgWidget()     { static const QColor c(0x2a, 0x2a, 0x2e); return c; }
inline QColor bgInput()      { static const QColor c(0x2a, 0x2a, 0x2e); return c; }
inline QColor bgElevated()   { static const QColor c(0x32, 0x32, 0x36); return c; }
inline QColor bgToolbar()    { static const QColor c(20, 20, 22, 230); return c; }
```

- [ ] **Step 2: Update border color functions**

```cpp
// Borders (softer)
inline QColor border()       { static const QColor c(0x2a, 0x2a, 0x2e); return c; }
inline QColor borderLight()  { static const QColor c(0x3a, 0x3a, 0x40); return c; }
```

- [ ] **Step 3: Build and verify compilation**

Run: `cmake --build build --config Debug`
Expected: Clean compile, no errors

---

### Task 2: Update Theme.h — Global Stylesheet

**Files:**
- Modify: `src/ui/Theme.h:61-265` (`getGlobalStyleSheet()`)

- [ ] **Step 1: Update QWidget/QMainWindow base styles**

Replace the opening sections of the stylesheet string:

```cpp
QWidget {
    background-color: #1e1e22;
    color: #e8e8ec;
    font-family: "Segoe UI", "Arial", sans-serif;
    font-size: 8pt;
}
QMainWindow {
    background-color: #141416;
}
QFrame {
    background-color: #141416;
    border: 1px solid #2a2a2e;
}
```

- [ ] **Step 2: Update QPushButton styles**

Replace the QPushButton section:

```cpp
QPushButton {
    background-color: #2a2a2e;
    color: #e8e8ec;
    border: 1px solid #3a3a40;
    border-radius: 4px;
    padding: 3px 8px;
    font-size: 8pt;
}
QPushButton:hover {
    background-color: #323236;
    border-color: #4a4a50;
}
QPushButton:pressed {
    background-color: #1e1e22;
}
QPushButton:checked {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
        stop:0 #d97706, stop:1 #b45309);
    color: white;
    border-color: #d97706;
}
QPushButton:disabled {
    background-color: #2a2a2e;
    color: #787880;
}
```

- [ ] **Step 3: Update QScrollBar styles**

Replace the QScrollBar sections:

```cpp
QScrollBar:vertical {
    background: #141416;
    width: 4px;
    border: none;
    margin: 0;
}
QScrollBar::handle:vertical {
    background: #4a4a50;
    min-height: 20px;
    border-radius: 2px;
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
    background: #141416;
    height: 4px;
    border: none;
    margin: 0;
}
QScrollBar::handle:horizontal {
    background: #4a4a50;
    min-width: 20px;
    border-radius: 2px;
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
```

- [ ] **Step 4: Update QComboBox, QLineEdit, QMenu, and remaining styles**

Replace remaining widget styles to match the new palette:

```cpp
QComboBox {
    background-color: #2a2a2e;
    color: #e8e8ec;
    border: 1px solid #3a3a40;
    border-radius: 4px;
    padding: 2px 6px;
    font-size: 8pt;
}
QComboBox:hover {
    border-color: #4a4a50;
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
    background-color: #323236;
    color: #e8e8ec;
    border: 1px solid #2a2a2e;
    selection-background-color: #d97706;
    selection-color: white;
    outline: none;
}
QLineEdit {
    background-color: #2a2a2e;
    color: #e8e8ec;
    border: 1px solid #3a3a40;
    border-radius: 2px;
    padding: 2px 4px;
    font-size: 8pt;
}
QLineEdit:focus {
    border-color: #d97706;
}
QSplitter::handle {
    background: #2a2a2e;
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
    background: #0e0e10;
    color: #a8a8b0;
    border-top: 1px solid #2a2a2e;
}
QMenuBar {
    background: #1e1e22;
    color: #e8e8ec;
    font-size: 9pt;
    border-bottom: 1px solid #2a2a2e;
    padding: 2px 0;
}
QMenuBar::item {
    padding: 4px 10px;
    background: transparent;
}
QMenuBar::item:selected {
    background: #2a2a2e;
}
QMenu {
    background-color: #323236;
    color: #e8e8ec;
    border: 1px solid #2a2a2e;
    padding: 4px 0;
}
QMenu::item {
    padding: 5px 24px 5px 12px;
    font-size: 9pt;
}
QMenu::item:selected {
    background-color: #d97706;
    color: white;
}
QMenu::item:disabled {
    color: #66666e;
}
QMenu::indicator {
    width: 16px;
    height: 16px;
    margin-left: 2px;
}
QMenu::separator {
    height: 1px;
    background: #3a3a40;
    margin: 4px 12px;
}
QMenu::right-arrow {
    width: 8px;
    height: 8px;
    right: 6px;
}
QToolTip {
    background-color: #2a2a2e;
    color: #e8e8ec;
    border: 1px solid #3a3a40;
    padding: 4px;
}
QScrollArea {
    border: none;
    background: transparent;
}
```

- [ ] **Step 5: Build and verify compilation**

Run: `cmake --build build --config Debug`
Expected: Clean compile

---

### Task 3: Update TrackHeaderWidget Typography & Toggles

**Files:**
- Modify: `src/ui/TrackHeaderWidget.cpp:23-32` (font setup)
- Modify: `src/ui/TrackHeaderWidget.cpp:267-400` (paintEvent)

- [ ] **Step 1: Update font sizes in constructor**

Replace the font setup block (lines 23-32):

```cpp
nameFont = font();
nameFont.setPointSize(9);
nameFont.setBold(true);

toggleFont = font();
toggleFont.setPointSize(7);
toggleFont.setBold(true);

smallFont = font();
smallFont.setPointSize(7);
```

- [ ] **Step 2: Update toggle button drawing in paintEvent**

In the `drawToggle` lambda (around line 334), update the font usage:

```cpp
auto drawToggle = [&](const QRect& rect, QColor onColor, bool active, const QString& label) {
    painter.setPen(QPen(active ? onColor.lighter(130) : ThemeColors::borderLight(), 1));
    painter.setBrush(active ? onColor : ThemeColors::bgWidget());
    painter.drawRoundedRect(rect, 2, 2);
    painter.setPen(active ? Qt::white : ThemeColors::textSecondary());
    painter.setFont(toggleFont);
    painter.drawText(rect, Qt::AlignCenter, label);
};
```

- [ ] **Step 3: Update MIDI channel indicator font**

In the MIDI channel indicator section (around line 321), update font size:

```cpp
{
    int channel = tree.getProperty(IDs::midiChannel, 1);
    QString chText = QString("CH %1").arg(channel);
    QFont chFont = painter.font();
    chFont.setPointSize(7);
    painter.setFont(chFont);
    painter.setPen(ThemeColors::textMuted());
    QRect chRect = header.nameRect;
    chRect.setLeft(chRect.right() - 50);
    painter.drawText(chRect, Qt::AlignRight | Qt::AlignVCenter, chText);
}
```

- [ ] **Step 4: Build and verify compilation**

Run: `cmake --build build --config Debug`
Expected: Clean compile

---

### Task 4: Update MixerStripWidget Typography & Buttons

**Files:**
- Modify: `src/ui/MixerStripWidget.cpp:101-215` (paintEvent)

- [ ] **Step 1: Update button drawing lambda**

In the `drawBtn` lambda (around line 155), update font size:

```cpp
auto drawBtn = [&](const QRect& rect, QColor onColor, bool active, const QString& label) {
    painter.setPen(QPen(active ? onColor.lighter(130) : ThemeColors::borderLight(), 1));
    painter.setBrush(active ? onColor : ThemeColors::bgWidget());
    painter.drawRoundedRect(rect, 2, 2);
    painter.setPen(active ? Qt::white : ThemeColors::textSecondary());
    QFont sf = painter.font();
    sf.setPointSize(7);
    sf.setBold(true);
    painter.setFont(sf);
    painter.drawText(rect, Qt::AlignCenter, label);
};
```

- [ ] **Step 2: Update FX button font**

In the FX button section (around line 170), update font size:

```cpp
// FX button
painter.setPen(QPen(ThemeColors::borderLight(), 1));
painter.setBrush(ThemeColors::bgWidget());
painter.drawRoundedRect(fxBtnRect, 2, 2);
painter.setPen(ThemeColors::accentBright());
QFont sf2 = painter.font();
sf2.setPointSize(7);
sf2.setBold(true);
painter.setFont(sf2);
painter.drawText(fxBtnRect, Qt::AlignCenter, "FX");
```

- [ ] **Step 3: Build and verify compilation**

Run: `cmake --build build --config Debug`
Expected: Clean compile

---

### Task 5: Update ClipItem Rendering

**Files:**
- Modify: `src/ui/ClipItem.cpp:44-117` (paint method)

- [ ] **Step 1: Replace the paint method**

Replace the entire `ClipItem::paint` method with the improved version:

```cpp
void ClipItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget*)
{
    Q_UNUSED(option);
    painter->setRenderHint(QPainter::Antialiasing);

    double w = (std::max)(getDuration() * pixelsPerSecond, minClipWidth);
    double h = trackHeight;
    if (h <= 0) return;

    QRectF r(0, 0, w, h);
    auto color = QColor::fromRgba(getColor());

    // Shadow (softer, more diffused)
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(0, 0, 0, 60));
    painter->drawRoundedRect(r.translated(0, 2).adjusted(-1, -1, 1, 1), cornerRadius + 1, cornerRadius + 1);
    painter->drawRoundedRect(r.translated(0, 1).adjusted(0, 0, 0, 0), cornerRadius, cornerRadius);

    // Main fill (subtle vertical gradient)
    QLinearGradient grad(r.topLeft(), r.bottomLeft());
    grad.setColorAt(0, color.lighter(140));
    grad.setColorAt(1, color.lighter(110));
    painter->setBrush(grad);
    painter->drawRoundedRect(r, cornerRadius, cornerRadius);

    // Top highlight (subtle inner glow)
    painter->setPen(QPen(QColor(255, 255, 255, 20), 1));
    painter->setBrush(Qt::NoBrush);
    painter->drawLine(QPointF(r.left() + cornerRadius, r.top() + 0.5),
                      QPointF(r.right() - cornerRadius, r.top() + 0.5));

    // Inner border
    painter->setPen(QPen(color.darker(130), 1));
    painter->setBrush(Qt::NoBrush);
    painter->drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), cornerRadius, cornerRadius);

    // Selection highlight (white outline)
    if (isSelected())
    {
        painter->setPen(QPen(QColor(0xe8, 0xe8, 0xec), 2));
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(r.adjusted(1, 1, -1, -1), cornerRadius, cornerRadius);
    }

    // Clip content (subclass)
    QRectF contentRect = r.adjusted(4, 4, -4, -4);
    if (contentRect.width() > 4 && contentRect.height() > 4)
        paintContent(*painter, contentRect);

    // Name label (with text shadow for readability)
    if (w > 30)
    {
        QFont f = painter->font();
        f.setPointSize((std::max)(7, (std::min)(10, static_cast<int>(h / 6))));
        painter->setFont(f);
        QString clipName = QString::fromUtf8(
            clipTree.getProperty(IDs::name).toString().toRawUTF8());

        // Shadow
        painter->setPen(QColor(0, 0, 0, 120));
        painter->drawText(r.adjusted(5, 3, -3, -1),
                          Qt::AlignLeft | Qt::AlignTop, clipName);
        // Text
        painter->setPen(Qt::white);
        painter->drawText(r.adjusted(4, 2, -4, -2),
                          Qt::AlignLeft | Qt::AlignTop, clipName);
    }

    // Fade triangles
    double fadeIn = getFadeIn();
    double fadeOut = getFadeOut();
    if (fadeIn > 0.001 && w > 10)
    {
        double fw = (std::min)(fadeIn * pixelsPerSecond, w * 0.5);
        QPolygonF tri;
        tri << QPointF(0, 0) << QPointF(fw, 0) << QPointF(0, h);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(0, 0, 0, 80));
        painter->drawPolygon(tri);
    }
    if (fadeOut > 0.001 && w > 10)
    {
        double fw = (std::min)(fadeOut * pixelsPerSecond, w * 0.5);
        QPolygonF tri;
        tri << QPointF(w, 0) << QPointF(w - fw, 0) << QPointF(w, h);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(0, 0, 0, 80));
        painter->drawPolygon(tri);
    }
}
```

- [ ] **Step 2: Build and verify compilation**

Run: `cmake --build build --config Debug`
Expected: Clean compile

---

### Task 6: Update NoteGridWidget Note Rendering

**Files:**
- Modify: `src/ui/NoteGridWidget.cpp:186-200` (note drawing loop)

- [ ] **Step 1: Update note rendering with gradient**

Replace the note drawing section (around line 186):

```cpp
// Draw notes
for (int i = 0; i < model.getNumNotes(); ++i)
{
    auto note = model.getNote(i);
    auto r = noteRect(i);
    if (r.right() < 0 || r.left() > w || r.bottom() < 0 || r.top() > h) continue;

    float vel = note.getProperty(IDs::velocity);
    int alpha = static_cast<int>(vel / 127.0f * 200.0f + 55.0f);
    alpha = (std::min)(255, alpha);

    QColor noteColor(ThemeColors::accent().red(), ThemeColors::accent().green(), ThemeColors::accent().blue(), alpha);

    // Note shadow
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 40));
    painter.drawRoundedRect(r.adjusted(0, 1, -1, 0), 2, 2);

    // Note body (gradient)
    QLinearGradient noteGrad(r.topLeft(), r.bottomLeft());
    noteGrad.setColorAt(0, noteColor.lighter(120));
    noteGrad.setColorAt(1, noteColor);
    painter.setPen(QPen(noteColor.darker(130), 1));
    painter.setBrush(noteGrad);
    painter.drawRoundedRect(r.adjusted(0, 0, -1, -1), 2, 2);

    // Selection highlight
    if (i == selectedNote)
    {
        painter.setPen(QPen(QColor(0xe8, 0xe8, 0xec), 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(r.adjusted(1, 1, -2, -2), 2, 2);
    }
}
```

- [ ] **Step 2: Build and verify compilation**

Run: `cmake --build build --config Debug`
Expected: Clean compile

---

### Task 7: Update TimelineToolbar Stylesheet

**Files:**
- Modify: `src/ui/TimelineToolbar.cpp:11-12` (toolbar stylesheet)

- [ ] **Step 1: Update toolbar background**

Replace the toolbar stylesheet (line 11-12):

```cpp
setStyleSheet(
    "TimelineToolbar { background: rgba(20, 20, 22, 230); border-bottom: 1px solid #2a2a2e; }");
```

- [ ] **Step 2: Update add track button hover**

Replace the addTrackBtn stylesheet (lines 22-24):

```cpp
addTrackBtn->setStyleSheet(
    "QToolButton { font-weight: bold; font-size: 11pt; }"
    "QToolButton:hover { border-color: #d97706; }");
```

- [ ] **Step 3: Update BPM spinbox and time sig combo stylesheets**

Replace the bpmSpinBox stylesheet (lines 112-114):

```cpp
bpmSpinBox->setStyleSheet(
    "QDoubleSpinBox { background: #141416; color: #e8e8ec; border: 1px solid #2a2a2e; "
    "border-radius: 2px; padding: 1px 2px; }");
```

Replace the timeSigCombo stylesheet (lines 125-127):

```cpp
timeSigCombo->setStyleSheet(
    "QComboBox { background: #141416; color: #e8e8ec; border: 1px solid #2a2a2e; "
    "border-radius: 2px; padding: 1px 2px; font-family: monospace; font-size: 8pt; }");
```

Replace the midiDeviceCombo stylesheet (lines 141-143):

```cpp
midiDeviceCombo->setStyleSheet(
    "QComboBox { background: #141416; color: #e8e8ec; border: 1px solid #2a2a2e; "
    "border-radius: 2px; padding: 1px 2px; font-size: 7pt; }");
```

- [ ] **Step 4: Build and verify compilation**

Run: `cmake --build build --config Debug`
Expected: Clean compile

---

### Task 8: Update StatusBar Stylesheet

**Files:**
- Modify: `src/ui/StatusBar.cpp:11-13` (field stylesheet)
- Modify: `src/ui/StatusBar.cpp:22` (separator stylesheet)
- Modify: `src/ui/StatusBar.cpp:75-76` (status bar stylesheet)

- [ ] **Step 1: Update field label stylesheet**

Replace the `makeField` function stylesheet (lines 11-13):

```cpp
l->setStyleSheet(
    "QLabel { color: #c8c8cc; font-size: 8pt; padding: 1px 6px; "
    "background: transparent; }");
```

- [ ] **Step 2: Update separator stylesheet**

Replace the `makeSeparator` function stylesheet (line 22):

```cpp
sep->setStyleSheet("color: #2a2a2e;");
```

- [ ] **Step 3: Update status bar stylesheet**

Replace the status bar stylesheet (lines 75-76):

```cpp
setStyleSheet(
    "QWidget { background: #0e0e10; border-top: 1px solid #2a2a2e; }");
```

- [ ] **Step 4: Build and verify compilation**

Run: `cmake --build build --config Debug`
Expected: Clean compile

---

### Task 9: Final Build & Visual Verification

- [ ] **Step 1: Full rebuild**

Run: `cmake --build build --config Debug`
Expected: Clean compile with no errors or warnings

- [ ] **Step 2: Launch application and verify**

Run: `build\Debug\HDAW.exe`

Checklist:
- [ ] Window background is darker (#141416)
- [ ] Panel backgrounds are distinct from window
- [ ] Widget backgrounds have clear contrast
- [ ] Buttons have subtle hover/pressed states
- [ ] Scrollbars are thinner (4px)
- [ ] Clips have gradient fill and white selection outline
- [ ] Track header text is slightly larger (9pt)
- [ ] Toggle buttons are readable (7pt)
- [ ] Mixer strip buttons match track header style
- [ ] No visual regressions in piano roll, FX chain, or automation

- [ ] **Step 3: Commit all changes**

```bash
git add src/ui/Theme.h src/ui/TrackHeaderWidget.cpp src/ui/MixerStripWidget.cpp \
        src/ui/ClipItem.cpp src/ui/NoteGridWidget.cpp src/ui/TimelineToolbar.cpp \
        src/ui/StatusBar.cpp
git commit -m "ui: subtle visual refresh — deeper backgrounds, refined buttons, thinner scrollbars, gradient clips, improved typography"
```