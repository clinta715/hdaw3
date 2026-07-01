#pragma once
#include <QString>
#include <QColor>

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
    inline QColor info()         { static const QColor c(0x38, 0xb2, 0xdf); return c; }

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
