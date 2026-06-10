#pragma once
#include <QWidget>

class PianoRollRuler : public QWidget
{
    Q_OBJECT
public:
    explicit PianoRollRuler(QWidget* parent = nullptr);

    void setPixelsPerBeat(double ppb) { pixelsPerBeat = ppb; update(); }
    double getPixelsPerBeat() const { return pixelsPerBeat; }
    void setScrollOffset(int offset) { scrollOffset = offset; update(); }
    int getScrollOffset() const { return scrollOffset; }

    double beatFromX(int x) const { return static_cast<double>(x + scrollOffset) / pixelsPerBeat; }
    int xFromBeat(double beat) const { return static_cast<int>(beat * pixelsPerBeat) - scrollOffset; }

    static constexpr int rulerHeight = 24;

protected:
    void paintEvent(QPaintEvent* event) override;
    QSize sizeHint() const override { return QSize(500, rulerHeight); }

private:
    double pixelsPerBeat = 40.0;
    int scrollOffset = 0;
};
