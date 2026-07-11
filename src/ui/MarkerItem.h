#pragma once
#include <QGraphicsObject>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSceneContextMenuEvent>
#include <juce_data_structures/juce_data_structures.h>
#include "../model/ProjectModel.h"
#include "../common/ProjectCommands.h"

class AudioEngine;

// Visual representation of a single marker in the time ruler. The
// marker holds a ValueTree reference so position and name changes
// round-trip through the undo manager.
class MarkerItem : public QGraphicsObject
{
    Q_OBJECT
public:
    MarkerItem(juce::ValueTree markerTree, AudioEngine& engine, double height = 18.0);
    ~MarkerItem() override;

    void setPixelsPerSecond(double pps);
    double getPixelsPerSecond() const { return pixelsPerSecond; }

    juce::ValueTree& getMarkerTree() { return markerTree; }

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

signals:
    void markerClicked(double timeSeconds);
    void markerMoved(juce::ValueTree markerTree, double newTime);
    void markerDeleteRequested(juce::ValueTree markerTree);
    void markerRenameRequested(juce::ValueTree markerTree);

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    void contextMenuEvent(QGraphicsSceneContextMenuEvent* event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;

private:
    juce::ValueTree markerTree;
    AudioEngine& engine;
    ProjectCommands* projectCmds = nullptr;
    double pixelsPerSecond;
    double height;
    bool dragging = false;
    bool dragMoved = false;
    double dragStartX = 0.0;
    double dragStartTime = 0.0;
};
