#include "AutomationLaneWidget.h"
#include "../engine/AudioEngine.h"
#include "Theme.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QApplication>
#include <QMenu>
#include <QAction>
#include <QSet>
#include <QKeyEvent>
#include <cmath>
#include "../engine/AutomationClipboard.h"
#include "../engine/RoutingManager.h"
#include "../engine/Track.h"
#include "../engine/TrackFXSlot.h"

AutomationLaneWidget::AutomationLaneWidget(AudioEngine& ae, QWidget* parent)
    : QWidget(parent), engine(ae)
{
    projectCmds = &engine.getProjectCommands();
    transportCmds = &engine.getTransportCommands();
    audioGraphCmds = &engine.getAudioGraphCommands();
    readModel = &engine.getReadModel();
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
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

    addLaneBtn = new QPushButton("+", header);
    addLaneBtn->setFixedSize(22, 22);
    addLaneBtn->setToolTip("Add automation lane");
    connect(addLaneBtn, &QPushButton::clicked, this, &AutomationLaneWidget::showAddLaneMenu);
    headerLayout->addWidget(addLaneBtn);

    removeLaneBtn = new QPushButton(QString::fromUtf8("\xe2\x88\x92"), header);
    removeLaneBtn->setFixedSize(22, 22);
    removeLaneBtn->setToolTip("Remove current automation lane");
    connect(removeLaneBtn, &QPushButton::clicked, this, &AutomationLaneWidget::removeCurrentLane);
    headerLayout->addWidget(removeLaneBtn);

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
    currentParamIndex = 0;
    refreshParamCombo();
    update();
}

void AutomationLaneWidget::clear()
{
    currentTrack = -1;
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
    // Guard against a zero/negative usable height (laneHeight is set via
    // setMinimumHeight, which is a floor, not a guarantee — a splitter or
    // window-restore animation can shrink the widget below 30 px
    // transiently). Without this, the division produces NaN/inf which
    // would be clamped to 0..1 but still get written to the ValueTree.
    const int usable = (std::max)(1, height() - 30);
    return 1.0 - static_cast<double>(y - 30) / usable;
}

int AutomationLaneWidget::xFromTime(double t) const
{
    return static_cast<int>(t * pixelsPerSecond) - scrollX;
}

int AutomationLaneWidget::yFromValue(double v) const
{
    const int usable = (std::max)(1, height() - 30);
    return 30 + static_cast<int>((1.0 - v) * usable);
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

    // paintEvent fires on every mouseMove during a drag, so reserve up front
    // to avoid per-paint heap reallocation. The sort is kept (cheap for the
    // typical 2–20 points) because clicking can insert a point out of order.
    // We deliberately read from the ValueTree rather than the engine's
    // AutomationManager cache: the widget just wrote to the tree and must see
    // its own edit immediately, while the engine cache lags one
    // rebuildAutomationCache behind.
    struct AP { double time; double value; };
    std::vector<AP> pts;
    pts.reserve(static_cast<size_t>(pointList.getNumChildren()));
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
    for (const auto& p : pts)
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
    for (const auto& p : pts)
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
        bool selected = selectedPoints.count(static_cast<int>(i)) > 0;
        if (selected)
        {
            painter.setPen(QPen(ThemeColors::accentBright(), 2));
            painter.setBrush(ThemeColors::accentBright());
        }
        else
        {
            painter.setPen(QPen(curveColor.darker(150), hovered ? 2 : 1));
            painter.setBrush(hovered ? ThemeColors::warning() : ThemeColors::textSecondary());
        }
        painter.drawEllipse(QPointF(px, py),
            pointRadius + (hovered ? 2 : 0),
            pointRadius + (hovered ? 2 : 0));
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

    // Grid (beat lines)
    double bpm = readModel->getTransport().bpm;
    double beatsPerSec = bpm / 60.0;
    for (int b = 0; b < 1000; ++b)
    {
        double beatTime = b / beatsPerSec;
        int x = static_cast<int>(beatTime * pixelsPerSecond - scrollX);
        if (x > w) break;
        if (x >= 0)
        {
            bool isBar = (b % 4 == 0);
            painter.setPen(QPen(isBar ? ThemeColors::gridLineBar() : ThemeColors::gridLineBeat(),
                                isBar ? 1 : 1));
            painter.drawLine(x, 30, x, h);
        }
    }

    // Playhead
    if (playheadSeconds >= 0)
    {
        int phx = static_cast<int>(playheadSeconds * pixelsPerSecond - scrollX);
        if (phx >= 0 && phx <= w)
        {
            painter.setPen(QPen(ThemeColors::accentBright(), 1));
            painter.drawLine(phx, 30, phx, h);
        }
    }
}

void AutomationLaneWidget::showAddLaneMenu()
{
    if (currentTrack < 0) return;

    auto trackList = engine.getProjectModel().getTrackListTree();
    if (currentTrack >= trackList.getNumChildren()) return;
    auto trackTree = trackList.getChild(currentTrack);
    auto autoList = trackTree.getChildWithName(IDs::AUTOMATION_LIST);

    QSet<int> existingParamIDs;
    if (autoList.isValid())
    {
        for (int i = 0; i < autoList.getNumChildren(); ++i)
            existingParamIDs.insert(static_cast<int>(autoList.getChild(i).getProperty(IDs::paramID)));
    }

    QMenu menu(this);

    auto addEntry = [&](const QString& name, int paramID) {
        if (!existingParamIDs.contains(paramID))
        {
            auto* action = menu.addAction(name);
            connect(action, &QAction::triggered, this, [this, name, paramID]() {
                addAutomationLane(name, paramID);
            });
        }
    };

    QAction* trackHeader = menu.addAction("Track Parameters");
    trackHeader->setEnabled(false);
    QFont f = menu.font();
    f.setBold(true);
    trackHeader->setFont(f);

    addEntry("Volume", 1);
    addEntry("Pan", 2);
    addEntry("Mute", 3);

    // Enumerate automatable plugin params via the decoupled ReadModel
    // accessors (getFxSlots for slot/plugin names, getAutomatableParams for
    // the per-param name/index list). This replaces the direct walk of
    // engine.getMainProcessor()->getRoutingManager()->getTrackNode().
    auto fxSlots = readModel->getFxSlots(currentTrack);
    auto params = readModel->getAutomatableParams(currentTrack);

    // Group params by slotIndex so each plugin gets its own menu section.
    for (const auto& slot : fxSlots)
    {
        // Only plugin slots expose automatable params; internal FX (eq,
        // reverb, etc.) are handled by dedicated track-level lanes above.
        if (slot.fxType != "plugin" || slot.bypassed)
            continue;

        // Collect this slot's automatable params.
        std::vector<AutomatableParamSnapshot> slotParams;
        for (const auto& p : params)
        {
            if (p.slotIndex == slot.slotIndex && p.automatable && p.paramIndex < 100)
                slotParams.push_back(p);
        }
        if (slotParams.empty())
            continue;

        menu.addSeparator();

        QString slotName = QString("Slot %1").arg(slot.slotIndex + 1);
        if (!slot.pluginName.empty())
            slotName += ": " + QString::fromStdString(slot.pluginName);
        QAction* slotHeader = menu.addAction(slotName);
        slotHeader->setEnabled(false);
        slotHeader->setFont(f);

        for (const auto& p : slotParams)
        {
            // The compound paramID scheme reserves 100 indices per slot,
            // so a plugin exposing >= 100 automatable params would alias
            // the next slot. Guard against that (p.paramIndex < 100 above)
            // rather than silently corrupting the dispatch.
            int compoundID = 100 + slot.slotIndex * 100 + p.paramIndex;
            QString paramName = QString::fromStdString(p.name);
            if (paramName.trimmed().isEmpty())
                paramName = QString("Param %1").arg(p.paramIndex);
            if (!existingParamIDs.contains(compoundID))
            {
                auto* action = menu.addAction(paramName);
                connect(action, &QAction::triggered, this, [this, paramName, compoundID]() {
                    addAutomationLane(paramName, compoundID);
                });
            }
        }
    }

    if (menu.actions().size() <= 1)
    {
        menu.addAction("(no available parameters)")->setEnabled(false);
    }

    menu.exec(addLaneBtn->mapToGlobal(QPoint(0, addLaneBtn->height())));
}

void AutomationLaneWidget::addAutomationLane(const QString& name, int paramID)
{
    if (currentTrack < 0) return;
    auto trackList = engine.getProjectModel().getTrackListTree();
    if (currentTrack >= trackList.getNumChildren()) return;
    auto trackTree = trackList.getChild(currentTrack);

    juce::ValueTree autoTree(IDs::AUTOMATION);
    autoTree.setProperty(IDs::name, juce::String(name.toUtf8().constData()), nullptr);
    autoTree.setProperty(IDs::paramID, paramID, nullptr);
    autoTree.setProperty(IDs::curveType, "linear", nullptr);
    autoTree.setProperty(IDs::automationEnabled, false, nullptr);

    double defaultVal = 0.5;
    if (paramID == 1)      defaultVal = 1.0; // Volume
    else if (paramID == 2) defaultVal = 0.5; // Pan (center)
    else if (paramID == 3) defaultVal = 0.0; // Mute (unmuted)

    juce::ValueTree pointList(IDs::POINT_LIST);
    juce::ValueTree pt1(IDs::POINT);
    pt1.setProperty(IDs::startTime, 0.0, nullptr);
    pt1.setProperty(IDs::gain, defaultVal, nullptr);
    pointList.addChild(pt1, -1, nullptr);
    juce::ValueTree pt2(IDs::POINT);
    pt2.setProperty(IDs::startTime, 16.0, nullptr);
    pt2.setProperty(IDs::gain, defaultVal, nullptr);
    pointList.addChild(pt2, -1, nullptr);
    autoTree.addChild(pointList, -1, nullptr);

    auto autoList = trackTree.getChildWithName(IDs::AUTOMATION_LIST);
    if (!autoList.isValid())
    {
        autoList = juce::ValueTree(IDs::AUTOMATION_LIST);
        trackTree.addChild(autoList, -1, &engine.getProjectModel().getUndoManager());
    }
    int newIdx = autoList.getNumChildren();
    autoList.addChild(autoTree, -1, &engine.getProjectModel().getUndoManager());
    currentParamIndex = newIdx;
    refreshParamCombo();
    emit automationChanged();
    update();
}

void AutomationLaneWidget::removeCurrentLane()
{
    if (currentTrack < 0 || currentParamIndex < 0) return;
    auto autoTree = currentAutoTree();
    if (!autoTree.isValid()) return;

    auto name = autoTree.getProperty(IDs::name).toString().toStdString();
    projectCmds->removeAutomationLane(currentTrack, name);

    auto trackList = engine.getProjectModel().getTrackListTree();
    auto autoList = trackList.getChild(currentTrack).getChildWithName(IDs::AUTOMATION_LIST);
    if (autoList.isValid() && currentParamIndex >= autoList.getNumChildren())
        currentParamIndex = std::max(0, autoList.getNumChildren() - 1);

    refreshParamCombo();
    emit automationChanged();
    update();
}

void AutomationLaneWidget::mousePressEvent(QMouseEvent* event)
{
    setFocus();
    if (event->pos().y() < 30) { QWidget::mousePressEvent(event); return; }

    if (event->button() == Qt::LeftButton)
    {
        int idx = pointAtPos(event->pos());
        bool ctrl = event->modifiers() & Qt::ControlModifier;

        if (idx >= 0)
        {
            if (ctrl)
            {
                if (selectedPoints.count(idx))
                    selectedPoints.erase(idx);
                else
                    selectedPoints.insert(idx);
            }
            else if (!selectedPoints.count(idx))
            {
                selectedPoints.clear();
                selectedPoints.insert(idx);
            }
            dragPoint = idx;
            grabMouse();
            projectCmds->beginTransaction("drag automation point");
        }
        else
        {
            double t = timeFromX(event->pos().x());
            double v = valueFromY(event->pos().y());
            v = (std::max)(0.0, (std::min)(1.0, v));

            auto autoTree = currentAutoTree();
            if (autoTree.isValid())
            {
                auto laneName = autoTree.getProperty(IDs::name).toString().toStdString();
                projectCmds->beginTransaction("add automation point");
                projectCmds->addAutomationPoint(currentTrack, laneName, t, v);
                projectCmds->setAutomationEnabled(currentTrack, laneName, true);

                auto pointList = autoTree.getChildWithName(IDs::POINT_LIST);
                int newIdx = pointList.isValid() ? pointList.getNumChildren() - 1 : 0;
                if (!ctrl)
                    selectedPoints.clear();
                selectedPoints.insert(newIdx);
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
                if (pointList.isValid())
                {
                    auto laneName = autoTree.getProperty(IDs::name).toString().toStdString();
                    // If the clicked point is in the selection, delete all selected.
                    if (selectedPoints.count(idx))
                    {
                        projectCmds->beginTransaction("delete automation points");
                        for (auto it = selectedPoints.rbegin(); it != selectedPoints.rend(); ++it)
                        {
                            if (*it >= 0 && *it < pointList.getNumChildren())
                            {
                                auto pt = pointList.getChild(*it);
                                double t = pt.getProperty(IDs::startTime);
                                projectCmds->removeAutomationPoint(currentTrack, laneName, t);
                            }
                        }
                        selectedPoints.clear();
                    }
                    else if (idx < pointList.getNumChildren())
                    {
                        auto pt = pointList.getChild(idx);
                        double t = pt.getProperty(IDs::startTime);
                        projectCmds->removeAutomationPoint(currentTrack, laneName, t);
                    }
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
        v = (std::max)(0.0, (std::min)(1.0, v));

        auto autoTree = currentAutoTree();
        if (autoTree.isValid())
        {
            auto laneName = autoTree.getProperty(IDs::name).toString().toStdString();
            projectCmds->setAutomationPointValue(currentTrack, laneName, t, v);
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
    if (dragPoint >= 0)
    {
        releaseMouse();
        dragPoint = -1;
        update();
    }
}

void AutomationLaneWidget::wheelEvent(QWheelEvent* event)
{
    if (event->modifiers() & Qt::ControlModifier)
    {
        double factor = (event->angleDelta().y() > 0) ? 1.3 : 1.0 / 1.3;
        pixelsPerSecond = (std::max)(5.0, (std::min)(200.0, pixelsPerSecond * factor));
        update();
        event->accept();
    }
    else
    {
        scrollX = (std::max)(0, scrollX - event->angleDelta().y());
        update();
        event->accept();
    }
}

void AutomationLaneWidget::keyPressEvent(QKeyEvent* event)
{
    auto autoTree = currentAutoTree();
    const auto mods = event->modifiers();

    // Delete / Backspace — remove selected points
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace)
    {
        if (!selectedPoints.empty() && autoTree.isValid())
        {
            auto pointList = autoTree.getChildWithName(IDs::POINT_LIST);
            if (pointList.isValid())
            {
                auto laneName = autoTree.getProperty(IDs::name).toString().toStdString();
                projectCmds->beginTransaction("delete automation points");
                for (auto it = selectedPoints.rbegin(); it != selectedPoints.rend(); ++it)
                {
                    if (*it >= 0 && *it < pointList.getNumChildren())
                    {
                        auto pt = pointList.getChild(*it);
                        double t = pt.getProperty(IDs::startTime);
                        projectCmds->removeAutomationPoint(currentTrack, laneName, t);
                    }
                }
                selectedPoints.clear();
                emit automationChanged();
                update();
            }
        }
        event->accept();
        return;
    }

    // Ctrl+A — select all
    if (event->key() == Qt::Key_A && (mods & Qt::ControlModifier))
    {
        selectedPoints.clear();
        if (autoTree.isValid())
        {
            auto pointList = autoTree.getChildWithName(IDs::POINT_LIST);
            if (pointList.isValid())
            {
                for (int i = 0; i < pointList.getNumChildren(); ++i)
                    selectedPoints.insert(i);
            }
        }
        update();
        event->accept();
        return;
    }

    // Escape — clear selection
    if (event->key() == Qt::Key_Escape)
    {
        selectedPoints.clear();
        update();
        event->accept();
        return;
    }

    auto doCopy = [&]() -> bool {
        if (selectedPoints.empty() || !autoTree.isValid())
            return false;
        auto pointList = autoTree.getChildWithName(IDs::POINT_LIST);
        if (!pointList.isValid())
            return false;
        std::vector<HDAW::AutomationPointEntry> entries;
        for (int idx : selectedPoints)
        {
            if (idx >= 0 && idx < pointList.getNumChildren())
            {
                auto p = pointList.getChild(idx);
                entries.push_back({
                    static_cast<double>(p.getProperty(IDs::startTime)),
                    static_cast<float>(static_cast<double>(p.getProperty(IDs::gain)))
                });
            }
        }
        int paramID = static_cast<int>(autoTree.getProperty(IDs::paramID));
        HDAW::AutomationClipboard::copyPoints(entries, paramID);
        return true;
    };

    auto doPaste = [&]() {
        if (!HDAW::AutomationClipboard::hasContent() || !autoTree.isValid())
            return;
        auto pointList = autoTree.getChildWithName(IDs::POINT_LIST);
        if (!pointList.isValid())
        {
            pointList = juce::ValueTree(IDs::POINT_LIST);
            autoTree.addChild(pointList, -1, nullptr);
        }

        auto laneName = autoTree.getProperty(IDs::name).toString().toStdString();
        projectCmds->beginTransaction("paste automation points");

        double origin = playheadSeconds >= 0.0 ? playheadSeconds
            : timeFromX(scrollX + width() / 2);
        double minTime = HDAW::AutomationClipboard::getMeta().minTime;

        std::set<int> newSelection;
        int baseIdx = pointList.getNumChildren();

        for (const auto& pt : HDAW::AutomationClipboard::getPoints())
        {
            double newTime = origin + (pt.time - minTime);
            if (newTime < 0.0) newTime = 0.0;
            projectCmds->addAutomationPoint(currentTrack, laneName, newTime, pt.value);
            newSelection.insert(baseIdx++);
        }

        projectCmds->setAutomationEnabled(currentTrack, laneName, true);
        // Re-query to get actual point count after bulk add
        if (pointList.isValid())
        {
            newSelection.clear();
            for (int i = 0; i < pointList.getNumChildren(); ++i)
                newSelection.insert(i);
        }
        selectedPoints = newSelection;
        emit automationChanged();
        update();
    };

    if ((mods & Qt::ControlModifier) && event->key() == Qt::Key_C)
    {
        doCopy();
        event->accept();
        return;
    }

    if ((mods & Qt::ControlModifier) && event->key() == Qt::Key_X)
    {
        if (doCopy() && autoTree.isValid())
        {
            auto pointList = autoTree.getChildWithName(IDs::POINT_LIST);
            if (pointList.isValid())
            {
                auto laneName = autoTree.getProperty(IDs::name).toString().toStdString();
                projectCmds->beginTransaction("cut automation points");
                for (auto it = selectedPoints.rbegin(); it != selectedPoints.rend(); ++it)
                {
                    if (*it >= 0 && *it < pointList.getNumChildren())
                    {
                        auto pt = pointList.getChild(*it);
                        double t = pt.getProperty(IDs::startTime);
                        projectCmds->removeAutomationPoint(currentTrack, laneName, t);
                    }
                }
                selectedPoints.clear();
                emit automationChanged();
                update();
            }
        }
        event->accept();
        return;
    }

    if ((mods & Qt::ControlModifier) && event->key() == Qt::Key_V)
    {
        doPaste();
        event->accept();
        return;
    }

    if ((mods & Qt::ControlModifier) && event->key() == Qt::Key_D)
    {
        if (selectedPoints.empty() || !autoTree.isValid())
        {
            event->accept();
            return;
        }
        auto pointList = autoTree.getChildWithName(IDs::POINT_LIST);
        if (!pointList.isValid())
        {
            event->accept();
            return;
        }

        auto laneName = autoTree.getProperty(IDs::name).toString().toStdString();
        projectCmds->beginTransaction("duplicate automation points");

        std::set<int> newSelection;
        for (int idx : selectedPoints)
        {
            if (idx < 0 || idx >= pointList.getNumChildren()) continue;
            auto p = pointList.getChild(idx);
            double t = static_cast<double>(p.getProperty(IDs::startTime)) + 0.25;
            float v = static_cast<float>(static_cast<double>(p.getProperty(IDs::gain)));
            projectCmds->addAutomationPoint(currentTrack, laneName, t, v);
        }

        // Re-query new point indices
        if (pointList.isValid())
        {
            for (int i = 0; i < pointList.getNumChildren(); ++i)
                newSelection.insert(i);
        }

        projectCmds->setAutomationEnabled(currentTrack, laneName, true);
        selectedPoints = newSelection;
        emit automationChanged();
        update();
        event->accept();
        return;
    }

    QWidget::keyPressEvent(event);
}

void AutomationLaneWidget::focusOutEvent(QFocusEvent* event)
{
    QWidget::focusOutEvent(event);
    if (dragPoint >= 0)
    {
        releaseMouse();
        dragPoint = -1;
        update();
    }
}

void AutomationLaneWidget::leaveEvent(QEvent* event)
{
    QWidget::leaveEvent(event);
    if (dragPoint >= 0)
    {
        releaseMouse();
        dragPoint = -1;
        update();
    }
}

// eventFilter removed — using grabMouse/releaseMouse instead of global qApp filter