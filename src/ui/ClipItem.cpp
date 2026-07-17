#include "ClipItem.h"
#include "Theme.h"
#include <QPainter>
#include <QStyleOptionGraphicsItem>

ClipItem::ClipItem(juce::ValueTree tree, double pps)
    : clipTree(tree), pixelsPerSecond(pps)
{
    setFlags(ItemSendsGeometryChanges | ItemIsSelectable);
    setAcceptHoverEvents(true);
    setCacheMode(QGraphicsItem::NoCache);
}

ClipItem::~ClipItem() = default;

juce::uint32 ClipItem::getColor() const
{
    // CLIP -> CLIP_LIST -> TRACK. Prefer the track's color so a clip always
    // reflects the track it lives on; this automatically applies the
    // destination track's color when a clip is dragged in from another track.
    auto trackTree = ProjectModel::getTrackOfClip(clipTree);
    if (trackTree.isValid() && trackTree.hasType(IDs::TRACK) && trackTree.hasProperty(IDs::color))
        return static_cast<juce::uint32>(static_cast<int>(trackTree.getProperty(IDs::color)));

    // Fallback: the clip's own color (e.g. before it is attached to a track).
    auto clipColor = clipTree.getProperty(IDs::color);
    if (static_cast<int>(clipColor) != 0)
        return static_cast<juce::uint32>(static_cast<int>(clipColor));

    // Default color if neither track nor clip has one
    return static_cast<juce::uint32>(0xFF4488CC);
}

void ClipItem::setPixelsPerSecond(double pps)
{
    if (pixelsPerSecond != pps)
    {
        pixelsPerSecond = pps;
        prepareGeometryChange();
    }
}

QRectF ClipItem::boundingRect() const
{
    double w = (std::max)(getDuration() * pixelsPerSecond, minClipWidth);
    return QRectF(0, 0, w, trackHeight).adjusted(-1, -1, 1, 1);
}

void ClipItem::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    // Ignore the event so TimelineInteraction (via TimelineScene
    // overrides) handles all clip interaction. The default handler
    // would accept the event for items with ItemIsSelectable,
    // starving the scene of mouse events.
    event->ignore();
}

void ClipItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget*)
{
    Q_UNUSED(option);
    painter->setRenderHint(QPainter::Antialiasing);

    double w = (std::max)(getDuration() * pixelsPerSecond, minClipWidth);
    double h = trackHeight;
    if (h <= 0) return;

    QRectF r(0, 0, w, h);
    auto color = QColor::fromRgba(getColor());

    // Shadow (softer, more diffused)
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(0, 0, 0, 60));
    painter->drawRoundedRect(r.translated(0, 2).adjusted(-1, -1, 1, 1), cornerRadius + 1, cornerRadius + 1);
    painter->drawRoundedRect(r.translated(0, 1).adjusted(0, 0, 0, 0), cornerRadius, cornerRadius);

    // Main fill (subtle vertical gradient)
    QLinearGradient grad(r.topLeft(), r.bottomLeft());
    grad.setColorAt(0, color.lighter(140));
    grad.setColorAt(1, color.lighter(110));
    painter->setBrush(grad);
    painter->drawRoundedRect(r, cornerRadius, cornerRadius);

    // Top highlight (subtle inner glow)
    painter->setPen(QPen(QColor(255, 255, 255, 20), 1));
    painter->setBrush(Qt::NoBrush);
    painter->drawLine(QPointF(r.left() + cornerRadius, r.top() + 0.5),
                      QPointF(r.right() - cornerRadius, r.top() + 0.5));

    // Inner border
    painter->setPen(QPen(color.darker(130), 1));
    painter->setBrush(Qt::NoBrush);
    painter->drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), cornerRadius, cornerRadius);

    // Selection highlight (white outline)
    if (isSelected())
    {
        painter->setPen(QPen(QColor(0xe8, 0xe8, 0xec), 3));
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(r.adjusted(1, 1, -1, -1), cornerRadius, cornerRadius);
    }

    // Clip content (subclass)
    QRectF contentRect = r.adjusted(4, 4, -4, -4);
    if (contentRect.width() > 4 && contentRect.height() > 4)
        paintContent(*painter, contentRect);

    // Name label (with text shadow for readability)
    if (w > 30)
    {
        QFont f = painter->font();
        f.setPointSize((std::max)(7, (std::min)(10, static_cast<int>(h / 6))));
        painter->setFont(f);
        QString clipName = QString::fromUtf8(
            clipTree.getProperty(IDs::name).toString().toRawUTF8());

        // Shadow
        painter->setPen(QColor(0, 0, 0, 120));
        painter->drawText(r.adjusted(5, 3, -3, -1),
                          Qt::AlignLeft | Qt::AlignTop, clipName);
        // Text
        painter->setPen(Qt::white);
        painter->drawText(r.adjusted(4, 2, -4, -2),
                          Qt::AlignLeft | Qt::AlignTop, clipName);
    }

    // Fade triangles
    double fadeIn = getFadeIn();
    double fadeOut = getFadeOut();
    if (fadeIn > 0.001 && w > 10)
    {
        double fw = (std::min)(fadeIn * pixelsPerSecond, w * 0.5);
        QPolygonF tri;
        tri << QPointF(0, 0) << QPointF(fw, 0) << QPointF(0, h);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(0, 0, 0, 80));
        painter->drawPolygon(tri);
    }
    if (fadeOut > 0.001 && w > 10)
    {
        double fw = (std::min)(fadeOut * pixelsPerSecond, w * 0.5);
        QPolygonF tri;
        tri << QPointF(w, 0) << QPointF(w - fw, 0) << QPointF(w, h);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(0, 0, 0, 80));
        painter->drawPolygon(tri);
    }
}
