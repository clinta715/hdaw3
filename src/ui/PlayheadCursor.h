#pragma once
#include <QGraphicsObject>
#include <QTimer>
#include "../engine/TransportManager.h"

class PlayheadCursor : public QGraphicsObject
{
    Q_OBJECT
public:
    PlayheadCursor(HDAW::TransportManager& tm, double pixelsPerSecond, QGraphicsItem* parent = nullptr);
    ~PlayheadCursor() override;

    void setPixelsPerSecond(double pps);
    void setFollowPlayhead(bool follow) { followPlayhead = follow; }
    bool isFollowing() const { return followPlayhead; }
    void setViewRectHeight(double h);

    void startSync();
    void stopSync();

    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

private slots:
    void syncPosition();

private:
    HDAW::TransportManager& transportManager;
    QTimer syncTimer;
    double pixelsPerSecond;
    bool followPlayhead = true;
    double viewHeight = 800.0;
};
