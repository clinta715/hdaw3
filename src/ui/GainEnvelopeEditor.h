#pragma once
#include <QWidget>
#include <QVector>
#include <QPointF>

class GainEnvelopeEditor : public QWidget
{
    Q_OBJECT
public:
    struct Point { double time; double gain; };

    explicit GainEnvelopeEditor(QWidget* parent = nullptr);
    void setPoints(const QVector<Point>& points);
    void setDuration(double seconds) { duration = seconds; update(); }
    QVector<Point> getPoints() const;

signals:
    void pointsChanged(const QVector<Point>& points);
    void pointAdded(double time, double gain);
    void pointRemoved(int index);
    void pointMoved(int index, double time, double gain);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;

private:
    QVector<Point> points;
    double duration = 4.0;
    int dragIndex = -1;
    bool adding = false;

    int timeToX(double t) const { return static_cast<int>(t / duration * width()); }
    double xToTime(int x) const { return static_cast<double>(x) / width() * duration; }
    int gainToY(double g) const { return height() - static_cast<int>(g * height()); }
    double yToGain(int y) const { return 1.0 - static_cast<double>(y) / height(); }
    int hitTest(const QPoint& pos) const;
};