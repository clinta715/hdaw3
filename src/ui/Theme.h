#pragma once
#include <QString>
#include <QColor>

namespace ThemeColors {
    // Backgrounds
    inline QColor bgWindow()     { return QColor(0x12, 0x12, 0x14); }
    inline QColor bgPanel()      { return QColor(0x1c, 0x1c, 0x1f); }
    inline QColor bgHeader()     { return QColor(0x1c, 0x1c, 0x1f); }
    inline QColor bgWidget()     { return QColor(0x2a, 0x2a, 0x2e); }
    inline QColor bgInput()      { return QColor(0x2a, 0x2a, 0x2e); }
    inline QColor bgElevated()   { return QColor(0x2e, 0x2e, 0x32); }
    inline QColor bgToolbar()    { return QColor(28, 28, 31, 220); }

    // Borders
    inline QColor border()       { return QColor(0x3a, 0x3a, 0x3e); }
    inline QColor borderLight()  { return QColor(0x4a, 0x4a, 0x4e); }

    // Text
    inline QColor textPrimary()  { return QColor(0xe4, 0xe4, 0xe7); }
    inline QColor textSecondary(){ return QColor(0xa1, 0xa1, 0xaa); }
    inline QColor textMuted()    { return QColor(0x71, 0x71, 0x7a); }

    // Accent - Sleek Cyan
    inline QColor accent()       { return QColor(0x06, 0xb6, 0xd4); }
    inline QColor accentDim()    { return QColor(0x08, 0x91, 0xb2); }
    inline QColor accentBright() { return QColor(0x22, 0xd3, 0xee); }

    // Semantic
    inline QColor danger()       { return QColor(0xef, 0x44, 0x44); }
    inline QColor warning()      { return QColor(0xf5, 0x9e, 0x0b); }
    inline QColor success()      { return QColor(0x10, 0xb9, 0x81); }

    // VU meter gradient
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
