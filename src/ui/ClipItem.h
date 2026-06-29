#pragma once
#include <QGraphicsItem>
#include <QGraphicsSceneMouseEvent>
#include <juce_data_structures/juce_data_structures.h>
#include "../model/ProjectModel.h"

class ClipItem : public QGraphicsItem
{
public:
    ClipItem(juce::ValueTree clipTree, double pixelsPerSecond);
    ~ClipItem() override;

    void setPixelsPerSecond(double pps);
    double getPixelsPerSecond() const { return pixelsPerSecond; }
    void setTrackHeight(double h) { trackHeight = h; prepareGeometryChange(); }
    double getTrackHeight() const { return trackHeight; }

    juce::ValueTree& getClipTree() { return clipTree; }
    double getStartTime() const { return clipTree.getProperty(IDs::startTime); }
    double getDuration() const { return clipTree.getProperty(IDs::duration); }
    double getFadeIn() const { return clipTree.getProperty(IDs::fadeIn); }
    double getFadeOut() const { return clipTree.getProperty(IDs::fadeOut); }
    juce::String getClipType() const { return clipTree.getProperty(IDs::clipType).toString(); }
    // Resolves the clip's color from its parent track so clips always match
    // the track they live on (and adopt a new track's color when moved).
    juce::uint32 getColor() const;

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

protected:
    virtual void paintContent(QPainter& painter, const QRectF& contentRect) = 0;

    juce::ValueTree clipTree;
    double pixelsPerSecond;
    double trackHeight = 80.0;

    static constexpr double cornerRadius = 4.0;
    static constexpr double fadeHandleSize = 8.0;
    static constexpr double minClipWidth = 10.0;
};
