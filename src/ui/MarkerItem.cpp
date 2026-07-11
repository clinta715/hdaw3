#include "MarkerItem.h"
#include "Theme.h"
#include "../engine/AudioEngine.h"
#include <QPainter>
#include <QAction>
#include <QMenu>
#include <QInputDialog>
#include <QApplication>
#include <QCursor>

MarkerItem::MarkerItem(juce::ValueTree tree, AudioEngine& ae, double h)
    : markerTree(tree), engine(ae), pixelsPerSecond(10.0), height(h)
{
    projectCmds = &engine.getProjectCommands();
    setAcceptHoverEvents(true);
    setCursor(Qt::SizeHorCursor);
    setFlag(QGraphicsItem::ItemSendsScenePositionChanges);
}

MarkerItem::~MarkerItem() = default;

void MarkerItem::setPixelsPerSecond(double pps)
{
    if (pixelsPerSecond != pps)
    {
        pixelsPerSecond = pps;
        prepareGeometryChange();
    }
}

QRectF MarkerItem::boundingRect() const
{
    // Tall enough to extend into the ruler area, wide enough for a small flag
    // and a short name.
    return QRectF(-6, -height, 90, height + 4);
}

void MarkerItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*)
{
    painter->setRenderHint(QPainter::Antialiasing);

    juce::String name = markerTree.getProperty(IDs::markerName).toString();
    int colorInt = markerTree.getProperty(IDs::markerColor, static_cast<int>(0xFF59e0c4));
    QColor markerColor = QColor::fromRgba(static_cast<QRgb>(colorInt));

    // The item's pos() already encodes the time offset (t * pixelsPerSecond),
    // so paint at local x = 0 — matching the LoopMarker pattern.
    double x = 0.0;

    // Vertical line (down through the ruler)
    painter->setPen(QPen(markerColor, 1, Qt::SolidLine));
    painter->drawLine(QPointF(x, 0), QPointF(x, height + 4));

    // Triangle flag
    QPolygonF flag;
    flag << QPointF(x, -height) << QPointF(x + 12, -height) << QPointF(x + 6, -height + 8);
    painter->setPen(Qt::NoPen);
    painter->setBrush(markerColor);
    painter->drawPolygon(flag);

    // Name label
    if (name.isNotEmpty())
    {
        QRectF labelRect(x + 6, -height + 1, 80, height - 2);
        painter->setPen(markerColor);
        QFont f = painter->font();
        f.setPointSize(7);
        painter->setFont(f);
        painter->drawText(labelRect, Qt::AlignLeft | Qt::AlignVCenter,
                          QString::fromUtf8(name.toRawUTF8()));
    }
}

void MarkerItem::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        dragging = true;
        dragMoved = false;
        dragStartX = event->scenePos().x();
        dragStartTime = markerTree.getProperty(IDs::markerTime);
        auto& um = engine.getProjectModel().getUndoManager();
        um.beginNewTransaction("Move marker");
        event->accept();
    }
    else if (event->button() == Qt::RightButton)
    {
        event->accept();
    }
}

void MarkerItem::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
    if (!dragging) return;
    double dx = event->scenePos().x() - dragStartX;
    if (std::abs(dx) > 1.0)
        dragMoved = true;
    double newTime = (std::max)(0.0, dragStartTime + dx / pixelsPerSecond);
    auto& um = engine.getProjectModel().getUndoManager();
    markerTree.setProperty(IDs::markerTime, newTime, &um);
    event->accept();
}

void MarkerItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    if (dragging && event->button() == Qt::LeftButton)
    {
        dragging = false;
        if (!dragMoved)
        {
            // Click without drag → seek to marker position
            double time = markerTree.getProperty(IDs::markerTime);
            emit markerClicked(time);
        }
        else
        {
            double newTime = markerTree.getProperty(IDs::markerTime);
            emit markerMoved(markerTree, newTime);
        }
        event->accept();
    }
}

void MarkerItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        emit markerRenameRequested(markerTree);
        event->accept();
    }
}

void MarkerItem::contextMenuEvent(QGraphicsSceneContextMenuEvent* event)
{
    QMenu menu;
    auto* rename = menu.addAction("Rename Marker");
    auto* deleteAct = menu.addAction("Delete Marker");
    auto* chosen = menu.exec(event->screenPos());
    if (chosen == rename)
        emit markerRenameRequested(markerTree);
    else if (chosen == deleteAct)
        emit markerDeleteRequested(markerTree);
}
