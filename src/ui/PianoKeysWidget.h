#pragma once
#include <QWidget>

class PianoKeysWidget : public QWidget
{
    Q_OBJECT
public:
    explicit PianoKeysWidget(QWidget* parent = nullptr);

    void setKeyHeight(double px) { keyHeight = px; updateGeometry(); update(); }
    double getKeyHeight() const { return keyHeight; }
    void setScrollOffset(int offset) { scrollOffset = offset; update(); }
    int getScrollOffset() const { return scrollOffset; }

    int noteNumberAtPos(int y) const;
    QRect keyRect(int noteNumber) const;

    static bool isBlackKey(int note);
    static constexpr int minNote = 36;  // C2
    static constexpr int maxNote = 96;  // C7
    static constexpr int noteCount = 61;
    static constexpr int keyWidth = 50;

protected:
    void paintEvent(QPaintEvent* event) override;
    QSize sizeHint() const override { return QSize(keyWidth, noteCount * 10); }

private:
    double keyHeight = 10.0;
    int scrollOffset = 0;
};
