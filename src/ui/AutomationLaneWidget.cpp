#include "AutomationLaneWidget.h"
#include "Theme.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <cmath>

AutomationLaneWidget::AutomationLaneWidget(AudioEngine& ae, QWidget* parent)
    : QWidget(parent), engine(ae)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* header = new QWidget(this);
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(4, 2, 4, 2);

    auto* label = new QLabel("Automation:", header);
    headerLayout->addWidget(label);

    paramCombo = new QComboBox(header);
    paramCombo->setFixedHeight(22);
    connect(paramCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AutomationLaneWidget::onParamChanged);
    headerLayout->addWidget(paramCombo, 1);

    layout->addWidget(header);
    layout->addStretch();

    setMinimumHeight(laneHeight + 30);
    setMouseTracking(true);
}

AutomationLaneWidget::~AutomationLaneWidget() = default;

juce::ValueTree AutomationLaneWidget::currentAutoTree() const
{
    if (currentTrack < 0) return {};
    auto trackList = engine.getProjectModel().getTrackListTree();
    if (currentTrack >= trackList.getNumChildren()) return {};
    auto trackTree = trackList.getChild(currentTrack);
    auto autoList = trackTree.getChildWithName(IDs::AUTOMATION_LIST);
    if (!autoList.isValid() || autoList.getNumChildren() <= currentParamIndex)
        return {};
    return autoList.getChild(currentParamIndex);
}

void AutomationLaneWidget::refreshParamCombo()
{
    paramCombo->blockSignals(true);
    paramCombo->clear();

    if (currentTrack < 0)
    {
        paramCombo->addItem("No track selected");
        paramCombo->setEnabled(false);
        paramCombo->blockSignals(false);
        return;
    }

    auto trackList = engine.getProjectModel().getTrackListTree();
    if (currentTrack >= trackList.getNumChildren())
    {
        paramCombo->addItem("No track selected");
        paramCombo->setEnabled(false);
        paramCombo->blockSignals(false);
        return;
    }

    paramCombo->setEnabled(true);
    auto trackTree = trackList.getChild(currentTrack);
    auto autoList = trackTree.getChildWithName(IDs::AUTOMATION_LIST);

    if (autoList.isValid())
    {
        for (int i = 0; i < autoList.getNumChildren(); ++i)
        {
            auto a = autoList.getChild(i);
            auto name = a.getProperty(IDs::name).toString();
            paramCombo->addItem(QString::fromUtf8(name.toRawUTF8()));
        }
    }

    if (paramCombo->count() == 0)
        paramCombo->addItem("(no automation)");

    if (currentParamIndex < paramCombo->count())
        paramCombo->setCurrentIndex(currentParamIndex);

    paramCombo->blockSignals(false);
}

void AutomationLaneWidget::onParamChanged(int index)
{
    if (index < 0) return;
    currentParamIndex = index;
    update();
}

void AutomationLaneWidget::loadTrack(int trackIndex)
{
    currentTrack = trackIndex;
    automation = nullptr;
    currentParamIndex = 0;
    refreshParamCombo();
    update();
}

void AutomationLaneWidget::clear()
{
    currentTrack = -1;
    automation = nullptr;
    currentParamIndex = 0;
    paramCombo->clear();
    update();
}

double AutomationLaneWidget::timeFromX(int x) const
{
    return static_cast<double>(x + scrollX) / pixelsPerSecond;
}

double AutomationLaneWidget::valueFromY(int y) const
{
    return 1.0 - static_cast<double>(y - 30) / (height() - 30);
}

int AutomationLaneWidget::xFromTime(double t) const
{
    return static_cast<int>(t * pixelsPerSecond) - scrollX;
}

int AutomationLaneWidget::yFromValue(double v) const
{
    return 30 + static_cast<int>((1.0 - v) * (height() - 30));
}

int AutomationLaneWidget::pointAtPos(const QPoint& pos) const
{
    auto autoTree = currentAutoTree();
    if (!autoTree.isValid()) return -1;

    auto pointList = autoTree.getChildWithName(IDs::POINT_LIST);
    if (!pointList.isValid()) return -1;

    for (int i = 0; i < pointList.getNumChildren(); ++i)
    {
        auto p = pointList.getChild(i);
        double t = p.getProperty(IDs::startTime);
        double v = p.getProperty(IDs::gain);
        int px = xFromTime(t);
        int py = yFromValue(v);
        int dx = pos.x() - px;
        int dy = pos.y() - py;
        if (dx * dx + dy * dy < (pointRadius + 4) * (pointRadius + 4))
            return i;
    }
    return -1;
}

void AutomationLaneWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    int w = width();
    int h = height();

    // Background
    painter.fillRect(rect(), ThemeColors::bgWindow());

    if (currentTrack < 0)
    {
        painter.setPen(ThemeColors::textSecondary());
        painter.drawText(rect().adjusted(0, 30, 0, 0), Qt::AlignCenter, "No track selected");
        return;
    }

    auto autoTree = currentAutoTree();
    if (!autoTree.isValid())
    {
        painter.setPen(ThemeColors::textSecondary());
        painter.drawText(rect().adjusted(0, 30, 0, 0), Qt::AlignCenter, "No automation data");
        return;
    }

    bool enabled = autoTree.getProperty(IDs::automationEnabled);
    auto pointList = autoTree.getChildWithName(IDs::POINT_LIST);
    if (!pointList.isValid()) return;

    int laneArea = h - 30;

    struct AP { double time; double value; };
    std::vector<AP> pts;
    for (int i = 0; i < pointList.getNumChildren(); ++i)
    {
        auto p = pointList.getChild(i);
        pts.push_back({ p.getProperty(IDs::startTime), p.getProperty(IDs::gain) });
    }
    std::sort(pts.begin(), pts.end(), [](const AP& a, const AP& b) { return a.time < b.time; });

    if (pts.empty()) return;

    // Center line
    painter.setPen(QPen(ThemeColors::border(), 1));
    painter.drawLine(0, 30 + laneArea / 2, w, 30 + laneArea / 2);

    QColor curveColor = enabled ? ThemeColors::automationLine() : QColor(ThemeColors::textMuted().red(), ThemeColors::textMuted().green(), ThemeColors::textMuted().blue(), 150);

    // Filled curve
    QPainterPath fillPath;
    fillPath.moveTo(xFromTime(pts.front().time), yFromValue(0.5));
    for (auto& p : pts)
    {
        int px = xFromTime(p.time);
        int py = yFromValue(p.value);
        fillPath.lineTo(px, py);
    }
    fillPath.lineTo(xFromTime(pts.back().time), yFromValue(0.5));
    fillPath.closeSubpath();
    painter.setPen(Qt::NoPen);
    painter.setBrush(ThemeColors::automationFill());
    painter.drawPath(fillPath);

    // Curve line
    QPen curvePen(curveColor, 2);
    painter.setPen(curvePen);
    painter.setBrush(Qt::NoBrush);
    QPainterPath path;
    bool hasPrev = false;
    for (auto& p : pts)
    {
        int px = xFromTime(p.time);
        int py = yFromValue(p.value);
        if (!hasPrev) { path.moveTo(px, py); hasPrev = true; }
        else { path.lineTo(px, py); }
    }
    painter.drawPath(path);

    // Points
    for (size_t i = 0; i < pts.size(); ++i)
    {
        int px = xFromTime(pts[i].time);
        int py = yFromValue(pts[i].value);
        bool hovered = (static_cast<int>(i) == hoverPoint);
        painter.setPen(QPen(curveColor.darker(150), hovered ? 2 : 1));
        painter.setBrush(hovered ? ThemeColors::warning() : ThemeColors::textSecondary());
        painter.drawEllipse(QPointF(px, py), pointRadius + (hovered ? 2 : 0), pointRadius + (hovered ? 2 : 0));
    }

    // Value labels
    QFont f = painter.font();
    f.setPointSize(7);
    painter.setFont(f);
    painter.setPen(ThemeColors::textSecondary());
    painter.drawText(2, 32, 40, 12, Qt::AlignLeft, "100%");
    painter.drawText(2, h - 14, 40, 12, Qt::AlignLeft, "0%");
    painter.drawText(2, 30 + laneArea / 2 - 6, 40, 12, Qt::AlignLeft, "50%");

    // Param label
    painter.setPen(ThemeColors::textPrimary());
    f.setBold(true);
    painter.setFont(f);
    auto name = autoTree.getProperty(IDs::name).toString();
    painter.drawText(40, 32, 150, 14, Qt::AlignLeft,
        QString::fromUtf8(name.toRawUTF8()) + QString(enabled ? " (A)" : ""));

    // Grid
    painter.setPen(QPen(QColor(255, 255, 255, 6), 1));
    for (int b = 0; b < 100; ++b)
    {
        int x = static_cast<int>(b * pixelsPerSecond - scrollX);
        if (x > w) break;
        if (x >= 0) painter.drawLine(x, 30, x, h);
    }
}

void AutomationLaneWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->pos().y() < 30) { QWidget::mousePressEvent(event); return; }

    if (event->button() == Qt::LeftButton)
    {
        int idx = pointAtPos(event->pos());
        if (idx >= 0) { dragPoint = idx; }
        else
        {
            double t = timeFromX(event->pos().x());
            double v = valueFromY(event->pos().y());
            v = std::max(0.0, std::min(1.0, v));

            auto autoTree = currentAutoTree();
            if (autoTree.isValid())
            {
                auto pointList = autoTree.getChildWithName(IDs::POINT_LIST);
                if (!pointList.isValid())
                {
                    pointList = juce::ValueTree(IDs::POINT_LIST);
                    autoTree.addChild(pointList, -1, &engine.getProjectModel().getUndoManager());
                }
                juce::ValueTree pt(IDs::POINT);
                pt.setProperty(IDs::startTime, t, &engine.getProjectModel().getUndoManager());
                pt.setProperty(IDs::gain, v, &engine.getProjectModel().getUndoManager());
                pointList.addChild(pt, -1, &engine.getProjectModel().getUndoManager());
                autoTree.setProperty(IDs::automationEnabled, true, &engine.getProjectModel().getUndoManager());
                emit automationChanged();
                update();
            }
        }
    }
    else if (event->button() == Qt::RightButton)
    {
        int idx = pointAtPos(event->pos());
        if (idx >= 0)
        {
            auto autoTree = currentAutoTree();
            if (autoTree.isValid())
            {
                auto pointList = autoTree.getChildWithName(IDs::POINT_LIST);
                if (pointList.isValid() && idx < pointList.getNumChildren())
                {
                    pointList.removeChild(idx, &engine.getProjectModel().getUndoManager());
                    emit automationChanged();
                    update();
                }
            }
        }
    }
}

void AutomationLaneWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (dragPoint >= 0)
    {
        double t = timeFromX(event->pos().x());
        double v = valueFromY(event->pos().y());
        v = std::max(0.0, std::min(1.0, v));

        auto autoTree = currentAutoTree();
        if (autoTree.isValid())
        {
            auto pointList = autoTree.getChildWithName(IDs::POINT_LIST);
            if (pointList.isValid() && dragPoint >= 0 && dragPoint < pointList.getNumChildren())
            {
                auto p = pointList.getChild(dragPoint);
                p.setProperty(IDs::startTime, std::max(0.0, t), &engine.getProjectModel().getUndoManager());
                p.setProperty(IDs::gain, v, &engine.getProjectModel().getUndoManager());
                emit automationChanged();
                update();
            }
        }
    }
    else
    {
        hoverPoint = pointAtPos(event->pos());
        setCursor(hoverPoint >= 0 ? Qt::SizeAllCursor : Qt::ArrowCursor);
        update();
    }
}

void AutomationLaneWidget::mouseReleaseEvent(QMouseEvent*)
{
    dragPoint = -1;
}

void AutomationLaneWidget::wheelEvent(QWheelEvent* event)
{
    if (event->modifiers() & Qt::ControlModifier)
    {
        double factor = (event->angleDelta().y() > 0) ? 1.3 : 1.0 / 1.3;
        pixelsPerSecond = std::max(5.0, std::min(200.0, pixelsPerSecond * factor));
        update();
        event->accept();
    }
    else
    {
        scrollX = std::max(0, scrollX - event->angleDelta().y());
        update();
        event->accept();
    }
}