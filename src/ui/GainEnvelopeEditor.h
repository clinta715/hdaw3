#pragma once
#include <QWidget>
#include <QVector>
#include <QPointF>

class GainEnvelopeEditor : public QWidget
{
    Q_OBJECT
public:
    struct Point {
        double time; double gain;
        // Unique id assigned at creation so a dragged point can be tracked
        // across std::sort even when two points share the same {time,gain}
        // (indexOf({t,g}) would otherwise return the first match and rebind
        // the drag to the wrong point). id is not part of value equality.
        long long id = 0;
        bool operator==(const Point& o) const { return time == o.time && gain == o.gain; }
    };

    explicit GainEnvelopeEditor(QWidget* parent = nullptr);
    void setPoints(const QVector<Point>& points);
    void setDuration(double seconds) { duration = seconds; update(); }
    QVector<Point> getPoints() const;

signals:
    void pointsChanged(const QVector<Point>& points);
    void pointAdded(double time, double gain);
    void pointRemoved(int index);
    void pointMoved(int index, double time, double gain);
    void dragStarted();
    void dragFinished();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    QVector<Point> points;
    double duration = 4.0;
    int dragIndex = -1;
    bool adding = false;
    long long nextPointId = 1; // monotonic id source for new points

    int timeToX(double t) const { return static_cast<int>(t / duration * width()); }
    double xToTime(int x) const { return static_cast<double>(x) / width() * duration; }
    int gainToY(double g) const { return height() - static_cast<int>(g * height()); }
    double yToGain(int y) const { return 1.0 - static_cast<double>(y) / height(); }
    int hitTest(const QPoint& pos) const;
    // Find the index of the point with the given id (the dragged point),
    // ignoring any neighbors that happen to share its {time,gain}.
    int indexOfId(long long id) const;
};