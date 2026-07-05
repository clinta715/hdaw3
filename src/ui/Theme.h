#pragma once
#include <QString>
#include <QColor>

namespace ThemeColors {
    // Backgrounds (deeper hierarchy)
    inline QColor bgWindow()     { static const QColor c(0x14, 0x14, 0x16); return c; }
    inline QColor bgPanel()      { static const QColor c(0x1e, 0x1e, 0x22); return c; }
    inline QColor bgHeader()     { static const QColor c(0x1e, 0x1e, 0x22); return c; }
    inline QColor bgWidget()     { static const QColor c(0x2a, 0x2a, 0x2e); return c; }
    inline QColor bgInput()      { static const QColor c(0x2a, 0x2a, 0x2e); return c; }
    inline QColor bgElevated()   { static const QColor c(0x32, 0x32, 0x36); return c; }
    inline QColor bgToolbar()    { static const QColor c(20, 20, 22, 230); return c; }

    // Borders (softer)
    inline QColor border()       { static const QColor c(0x2a, 0x2a, 0x2e); return c; }
    inline QColor borderLight()  { static const QColor c(0x3a, 0x3a, 0x40); return c; }

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
    )";
}
