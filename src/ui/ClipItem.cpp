#include "ClipItem.h"
#include "Theme.h"
#include <QPainter>
#include <QStyleOptionGraphicsItem>

ClipItem::ClipItem(juce::ValueTree tree, double pps)
    : clipTree(tree), pixelsPerSecond(pps)
{
    setFlags(ItemSendsGeometryChanges);
    setAcceptHoverEvents(true);
    setCacheMode(DeviceCoordinateCache);
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
    return static_cast<juce::uint32>(static_cast<int>(clipTree.getProperty(IDs::color)));
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

void ClipItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget*)
{
    Q_UNUSED(option);
    painter->setRenderHint(QPainter::Antialiasing);

    double w = (std::max)(getDuration() * pixelsPerSecond, minClipWidth);
    double h = trackHeight;
    if (h <= 0) return;

    QRectF r(0, 0, w, h);
    auto color = QColor::fromRgba(getColor());

    // Shadow
    QRectF shadowR = r.translated(1, 1);
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(0, 0, 0, 80));
    painter->drawRoundedRect(shadowR, cornerRadius, cornerRadius);

    // Main fill
    painter->setBrush(color.lighter(130));
    painter->drawRoundedRect(r, cornerRadius, cornerRadius);

    // Inner border
    painter->setPen(QPen(color.darker(120), 1));
    painter->setBrush(Qt::NoBrush);
    painter->drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), cornerRadius, cornerRadius);

    // Selection highlight
    if (isSelected())
    {
        painter->setPen(QPen(ThemeColors::accent(), 2));
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(r.adjusted(1, 1, -1, -1), cornerRadius, cornerRadius);
    }

    // Clip content (subclass)
    QRectF contentRect = r.adjusted(4, 4, -4, -4);
    if (contentRect.width() > 4 && contentRect.height() > 4)
        paintContent(*painter, contentRect);

    // Name label
    if (w > 30)
    {
        painter->setPen(Qt::white);
        QFont f = painter->font();
        f.setPointSize((std::max)(7, (std::min)(10, static_cast<int>(h / 6))));
        painter->setFont(f);
        QString clipName = QString::fromUtf8(
            clipTree.getProperty(IDs::name).toString().toRawUTF8());
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
        painter->setBrush(QColor(0, 0, 0, 100));
        painter->drawPolygon(tri);
    }
    if (fadeOut > 0.001 && w > 10)
    {
        double fw = (std::min)(fadeOut * pixelsPerSecond, w * 0.5);
        QPolygonF tri;
        tri << QPointF(w, 0) << QPointF(w - fw, 0) << QPointF(w, h);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(0, 0, 0, 100));
        painter->drawPolygon(tri);
    }
}
