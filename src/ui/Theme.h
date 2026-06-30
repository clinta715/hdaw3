#pragma once
#include <QString>
#include <QColor>

namespace ThemeColors {
    // Backgrounds
    inline QColor bgWindow()     { static const QColor c(0x12, 0x12, 0x14); return c; }
    inline QColor bgPanel()      { static const QColor c(0x1c, 0x1c, 0x1f); return c; }
    inline QColor bgHeader()     { static const QColor c(0x1c, 0x1c, 0x1f); return c; }
    inline QColor bgWidget()     { static const QColor c(0x2a, 0x2a, 0x2e); return c; }
    inline QColor bgInput()      { static const QColor c(0x2a, 0x2a, 0x2e); return c; }
    inline QColor bgElevated()   { static const QColor c(0x2e, 0x2e, 0x32); return c; }
    inline QColor bgToolbar()    { static const QColor c(28, 28, 31, 220); return c; }

    // Borders
    inline QColor border()       { static const QColor c(0x3a, 0x3a, 0x3e); return c; }
    inline QColor borderLight()  { static const QColor c(0x4a, 0x4a, 0x4e); return c; }

    // Text
    inline QColor textPrimary()  { static const QColor c(0xe4, 0xe4, 0xe7); return c; }
    inline QColor textSecondary(){ static const QColor c(0xa1, 0xa1, 0xaa); return c; }
    inline QColor textMuted()    { static const QColor c(0x71, 0x71, 0x7a); return c; }

    // Accent - Sleek Cyan
    inline QColor accent()       { static const QColor c(0x06, 0xb6, 0xd4); return c; }
    inline QColor accentDim()    { static const QColor c(0x08, 0x91, 0xb2); return c; }
    inline QColor accentBright() { static const QColor c(0x22, 0xd3, 0xee); return c; }

    // Semantic
    inline QColor danger()       { static const QColor c(0xef, 0x44, 0x44); return c; }
    inline QColor warning()      { static const QColor c(0xf5, 0x9e, 0x0b); return c; }
    inline QColor success()      { static const QColor c(0x10, 0xb9, 0x81); return c; }

    // VU meter gradient
    inline QColor vuGreen()      { static const QColor c(0x10, 0xb9, 0x81); return c; }
    inline QColor vuYellow()     { static const QColor c(0xf5, 0x9e, 0x0b); return c; }
    inline QColor vuRed()        { static const QColor c(0xef, 0x44, 0x44); return c; }

    // Track surfaces
    inline QColor trackFill1()   { static const QColor c(0x2a, 0x2a, 0x2e); return c; }
    inline QColor trackFill2()   { static const QColor c(0x2e, 0x2e, 0x32); return c; }
    inline QColor trackColor()   { static const QColor c(0x06, 0xb6, 0xd4, 40); return c; }
    inline QColor rulerBg()      { static const QColor c(0x1c, 0x1c, 0x1f); return c; }

    // Automation
    inline QColor automationFill(){ static const QColor c(0x06, 0xb6, 0xd4, 40); return c; }
    inline QColor automationLine(){ static const QColor c(0x06, 0xb6, 0xd4, 200); return c; }

    // Scrollbar
    inline QColor scrollbarBg()  { static const QColor c(0x18, 0x18, 0x1b); return c; }
    inline QColor scrollbarHandle(){ static const QColor c(0x3a, 0x3a, 0x3e); return c; }
    inline QColor scrollbarHover(){ static const QColor c(0x06, 0xb6, 0xd4); return c; }
}

inline QString getGlobalStyleSheet()
{
    return R"(
        QWidget {
            background-color: #1c1c1f;
            color: #e4e4e7;
            font-family: "Segoe UI Semibold", "Segoe UI", "Arial", sans-serif;
            font-size: 8pt;
        }
        QMainWindow {
            background-color: #121214;
        }

        QFrame {
            background-color: #121214;
            border: 1px solid #3a3a3e;
        }

        QPushButton {
            background-color: #2a2a2e;
            color: #e4e4e7;
            border: 1px solid #4a4a4e;
            border-radius: 3px;
            padding: 3px 8px;
            font-size: 8pt;
        }
        QPushButton:hover {
            background-color: #2e2e32;
            border-color: #06b6d4;
        }
        QPushButton:pressed {
            background-color: #1c1c1f;
        }
        QPushButton:checked {
            background-color: #06b6d4;
            color: white;
        }
        QPushButton:disabled {
            background-color: #2e2e32;
            color: #71717a;
        }

        QComboBox {
            background-color: #2a2a2e;
            color: #e4e4e7;
            border: 1px solid #4a4a4e;
            border-radius: 3px;
            padding: 2px 6px;
            font-size: 8pt;
        }
        QComboBox:hover {
            border-color: #06b6d4;
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
            background-color: #2e2e32;
            color: #e4e4e7;
            border: 1px solid #3a3a3e;
            selection-background-color: #06b6d4;
            selection-color: white;
            outline: none;
        }

        QLineEdit {
            background-color: #2a2a2e;
            color: #e4e4e7;
            border: 1px solid #4a4a4e;
            border-radius: 2px;
            padding: 2px 4px;
            font-size: 8pt;
        }
        QLineEdit:focus {
            border-color: #06b6d4;
        }

        QScrollBar:vertical {
            background: #18181b;
            width: 6px;
            border: none;
            margin: 0;
        }
        QScrollBar::handle:vertical {
            background: #3a3a3e;
            min-height: 20px;
            border-radius: 3px;
            margin: 1px;
        }
        QScrollBar::handle:vertical:hover {
            background: #06b6d4;
        }
        QScrollBar::add-line:vertical,
        QScrollBar::sub-line:vertical {
            height: 0;
            border: none;
        }
        QScrollBar:horizontal {
            background: #18181b;
            height: 6px;
            border: none;
            margin: 0;
        }
        QScrollBar::handle:horizontal {
            background: #3a3a3e;
            min-width: 20px;
            border-radius: 3px;
            margin: 1px;
        }
        QScrollBar::handle:horizontal:hover {
            background: #06b6d4;
        }
        QScrollBar::add-line:horizontal,
        QScrollBar::sub-line:horizontal {
            width: 0;
            border: none;
        }

        QSplitter::handle {
            background: #3a3a3e;
        }
        QSplitter::handle:horizontal {
            width: 2px;
        }
        QSplitter::handle:vertical {
            height: 2px;
        }

        QLabel {
            background: transparent;
            color: #e4e4e7;
            border: none;
        }

        QStatusBar {
            background: #1c1c1f;
            color: #a1a1aa;
            border-top: 1px solid #3a3a3e;
        }

        QMenu {
            background-color: #2e2e32;
            color: #e4e4e7;
            border: 1px solid #3a3a3e;
        }
        QMenu::item:selected {
            background-color: #06b6d4;
            color: white;
        }

        QToolTip {
            background-color: #2a2a2e;
            color: #e4e4e7;
            border: 1px solid #4a4a4e;
            padding: 4px;
        }

        QScrollArea {
            border: none;
            background: transparent;
        }
    )";
}
