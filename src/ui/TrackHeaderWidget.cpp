#include "TrackHeaderWidget.h"
#include "../engine/AudioEngine.h"
#include "Theme.h"
#include "../engine/LevelMeter.h"
#include "../engine/PluginManager.h"
#include <QPainter>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QVBoxLayout>
#include <QApplication>
#include <QMenu>
#include <QAction>
#include <QTimer>
#include <QInputDialog>
#include <QColorDialog>

TrackHeaderWidget::TrackHeaderWidget(AudioEngine& ae, QWidget* parent)
    : QWidget(parent), engine(ae)
{
    projectCmds = &engine.getProjectCommands();
    transportCmds = &engine.getTransportCommands();
    audioGraphCmds = &engine.getAudioGraphCommands();
    readModel = &engine.getReadModel();
    setFixedWidth(static_cast<int>(headerWidth));
    setMinimumHeight(100);
    setMouseTracking(true);
    qApp->installEventFilter(this);

    nameFont = font();
    nameFont.setPointSize(9);
    nameFont.setBold(true);

    toggleFont = font();
    toggleFont.setPointSize(7);
    toggleFont.setBold(true);

    smallFont = font();
    smallFont.setPointSize(7);

    connect(&vuTimer, &QTimer::timeout, this, &TrackHeaderWidget::updateVU);
    vuTimer.start(static_cast<int>(vuUpdateInterval));

    rebuild();
}

TrackHeaderWidget::~TrackHeaderWidget() = default;

void TrackHeaderWidget::rebuild()
{
    for (auto& h : tracks)
    {
        if (h.nameEdit != nullptr)
        {
            h.nameEdit->deleteLater();
            h.nameEdit = nullptr;
        }
    }
    tracks.clear();
    auto trackList = engine.getProjectModel().getTrackListTree();
    int count = trackList.getNumChildren();

    for (int i = 0; i < count; ++i)
    {
        auto tree = trackList.getChild(i);
        TrackHeader h;
        h.trackIndex = i;
        h.volValue = tree.getProperty(IDs::volume);
        h.panValue = tree.getProperty(IDs::pan);
        h.isMuted = tree.getProperty(IDs::isMuted);
        h.isSoloed = tree.getProperty(IDs::isSoloed);
        h.nameEdit = nullptr;
        tracks.push_back(h);
    }

    setMinimumHeight(100);
    layoutRects();
    update();
}

void TrackHeaderWidget::setTrackHeight(int index, double height)
{
    auto trackList = engine.getProjectModel().getTrackListTree();
    if (index >= 0 && index < trackList.getNumChildren())
    {
        trackList.getChild(index).setProperty(IDs::trackHeight,
            (std::max)(40.0, height), &engine.getProjectModel().getUndoManager());
        layoutRects();
        update();
    }
}

double TrackHeaderWidget::getTrackHeight(int index) const
{
    auto trackList = engine.getProjectModel().getTrackListTree();
    if (index >= 0 && index < trackList.getNumChildren())
    {
        double h = trackList.getChild(index).getProperty(IDs::trackHeight, defaultTrackHeight);
        return (std::max)(40.0, h);
    }
    return defaultTrackHeight;
}

void TrackHeaderWidget::updateVU()
{
    for (auto& h : tracks)
    {
        auto* track = engine.getMainProcessor()->getTrack(h.trackIndex);
        if (track != nullptr)
        {
            auto& meter = track->getMeter();
            h.vuLeft = meter.getLeftLevel();
            h.vuRight = meter.getRightLevel();
        }
    }
    for (auto& h : tracks)
    {
        if (!h.vuRect.isEmpty())
            update(h.vuRect);
    }
}

void TrackHeaderWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    layoutRects();
}

void TrackHeaderWidget::layoutRects()
{
    int w = width();
    auto trackList = engine.getProjectModel().getTrackListTree();
    int count = trackList.getNumChildren();

    if (static_cast<int>(tracks.size()) != count)
        return;

    double trackY = rulerHeight - scrollOffset;

    for (int i = 0; i < count; ++i)
    {
        auto& header = tracks[i];
        double trackH = getTrackHeight(i);
        int y = static_cast<int>(trackY);
        int h = static_cast<int>(trackH);

        QRect row(0, y, w, h);
        header.bounds = row;

        header.nameRect = QRect(8, y + 4, w - 16, 16);

        int btnY = y + 24;
        int btnSize = 14;
        int btnSpacing = 4;
        int btnX = 6;
        header.muteRect = QRect(btnX, btnY, btnSize, btnSize);
        header.soloRect = QRect(btnX + btnSize + btnSpacing, btnY, btnSize, btnSize);
        header.armRect = QRect(btnX + 2 * (btnSize + btnSpacing), btnY, btnSize, btnSize);
        header.autoRect = QRect(btnX + 3 * (btnSize + btnSpacing), btnY, btnSize, btnSize);
        header.monitorRect = QRect(btnX + 4 * (btnSize + btnSpacing), btnY, btnSize, btnSize);

        int faderY = y + 44;
        int faderH = 20;
        int faderW = w - 20;
        int faderX = 10;
        header.volRect = QRect(faderX, faderY, faderW, faderH);

        int panY = y + 66;
        int panW = faderW;
        int panH = 10;
        header.panRect = QRect(faderX, panY, panW, panH);

        header.vuRect = QRect(w - 14, y + 4, 10, h - 8);

        trackY += trackH;
    }
}

int TrackHeaderWidget::hitTest(const QPoint& pos, int& outTrackIndex) const
{
    for (const auto& h : tracks)
    {
        if (h.bounds.contains(pos))
        {
            outTrackIndex = h.trackIndex;

            auto clampRect = [&](QRect r) -> QRect {
                return r.intersected(h.bounds);
            };

            if (clampRect(h.muteRect).contains(pos)) return 1;
            if (clampRect(h.soloRect).contains(pos)) return 2;
            if (clampRect(h.armRect).contains(pos)) return 3;
            if (clampRect(h.autoRect).contains(pos)) return 7;
            if (clampRect(h.monitorRect).contains(pos)) return 8;
            if (clampRect(h.volRect).contains(pos)) return 4;
            if (clampRect(h.panRect).contains(pos)) return 5;
            if (clampRect(h.nameRect).contains(pos)) return 6;
            return 0;
        }
    }
    return -1;
}

TrackHeaderWidget::TrackHeader& TrackHeaderWidget::headerFor(int trackIndex)
{
    if (trackIndex >= 0 && trackIndex < static_cast<int>(tracks.size()))
        return tracks[trackIndex];
    // Sentinel for out-of-range access. Callers MUST re-validate the index
    // before mutating any field — the same dummy is shared across all
    // out-of-range calls, so writing through it (e.g. nameEdit) tramples
    // state and can leak QLineEdits. The name-edit lambdas below capture
    // the QLineEdit* directly to avoid this trap.
    static TrackHeader dummy;
    return dummy;
}

void TrackHeaderWidget::commitVolume(int trackIndex, float vol)
{
    if (trackIndex < 0) return;
    auto trackList = engine.getProjectModel().getTrackListTree();
    if (trackIndex < trackList.getNumChildren())
    {
        auto tree = trackList.getChild(trackIndex);
        tree.setProperty(IDs::volume, vol, &engine.getProjectModel().getUndoManager());

        ParamUpdate update{ trackIndex, 1, vol };
        engine.getBridge().pushUpdate(update);
    }
}

void TrackHeaderWidget::commitPan(int trackIndex, float pan)
{
    if (trackIndex < 0) return;
    auto trackList = engine.getProjectModel().getTrackListTree();
    if (trackIndex < trackList.getNumChildren())
    {
        auto tree = trackList.getChild(trackIndex);
        tree.setProperty(IDs::pan, pan, &engine.getProjectModel().getUndoManager());
    }
}

void TrackHeaderWidget::setScrollOffset(double yOffset)
{
    if (scrollOffset != yOffset)
    {
        scrollOffset = yOffset;
        layoutRects();
        update();
    }
}

void TrackHeaderWidget::setSelectedTrack(int index)
{
    if (selectedTrack != index)
    {
        selectedTrack = index;
        update();
    }
}

QSize TrackHeaderWidget::sizeHint() const
{
    auto trackList = engine.getProjectModel().getTrackListTree();
    int count = trackList.getNumChildren();
    double totalH = rulerHeight;
    for (int i = 0; i < count; ++i)
        totalH += getTrackHeight(i);
    int hintH = static_cast<int>(totalH) + 20;
    int hintW = static_cast<int>(headerWidth);
    return QSize(hintW, (std::max)(hintH, minimumHeight()));
}

QSize TrackHeaderWidget::minimumSizeHint() const
{
    return QSize(static_cast<int>(headerWidth), minimumHeight());
}

void TrackHeaderWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    int w = width();
    auto trackList = engine.getProjectModel().getTrackListTree();
    int count = trackList.getNumChildren();

    if (count != static_cast<int>(tracks.size()))
    {
        QTimer::singleShot(0, this, &TrackHeaderWidget::rebuild);
        return;
    }

    double trackY = rulerHeight - scrollOffset;

    painter.save();
    painter.setClipRect(0, static_cast<int>(rulerHeight), w, height() - static_cast<int>(rulerHeight));

    for (int i = 0; i < count; ++i)
    {
        auto tree = trackList.getChild(i);
        double trackH = getTrackHeight(i);

        auto& header = headerFor(i);
        const QRect& row = header.bounds;

        // Background
        QColor bg = (i % 2 == 0) ? ThemeColors::trackFill1() : ThemeColors::trackFill2();
        painter.fillRect(row, bg);

        // Selection highlight
        bool isSelected = (i == selectedTrack);
        if (isSelected)
        {
            painter.fillRect(row, QColor(217, 119, 6, 50));
            painter.setPen(QPen(ThemeColors::accent(), 2));
            painter.drawRect(row.adjusted(2, 1, -1, -1));
        }

        // Color strip — wider when selected
        {
            int tc = tree.getProperty(IDs::color, static_cast<int>(0xFF4488CC));
            QColor trackColor((tc >> 16) & 0xFF, (tc >> 8) & 0xFF, tc & 0xFF);
            int stripW = isSelected ? 6 : 3;
            painter.fillRect(QRect(0, row.y(), stripW, row.height()),
                isSelected ? trackColor.lighter(140) : trackColor);
        }

        // Name
        painter.setPen(Qt::white);
        painter.setFont(nameFont);
        QString name = QString::fromUtf8(tree.getProperty(IDs::name).toString().toRawUTF8());
        painter.drawText(header.nameRect, Qt::AlignLeft | Qt::AlignVCenter, name);

        // MIDI channel indicator (small text on the right of the name row)
        {
            int channel = tree.getProperty(IDs::midiChannel, 1);
            QString chText = QString("CH %1").arg(channel);
            QFont chFont = painter.font();
            chFont.setPointSize(7);
            painter.setFont(chFont);
            painter.setPen(ThemeColors::textMuted());
            QRect chRect = header.nameRect;
            chRect.setLeft(chRect.right() - 50);
            painter.drawText(chRect, Qt::AlignRight | Qt::AlignVCenter, chText);
        }

        // Mute / Solo / Arm buttons (rects pre-computed in layoutRects)
        auto drawToggle = [&](const QRect& rect, QColor onColor, bool active, const QString& label) {
            painter.setPen(QPen(active ? onColor.lighter(130) : ThemeColors::borderLight(), 1));
            painter.setBrush(active ? onColor : ThemeColors::bgWidget());
            painter.drawRoundedRect(rect, 2, 2);
            painter.setPen(active ? Qt::white : ThemeColors::textSecondary());
            painter.setFont(toggleFont);
            painter.drawText(rect, Qt::AlignCenter, label);
        };

        drawToggle(header.muteRect, ThemeColors::danger(), header.isMuted, "M");
        drawToggle(header.soloRect, ThemeColors::warning(), header.isSoloed, "S");
        {
            bool rArm = tree.getProperty(IDs::isArm);
            drawToggle(header.armRect, ThemeColors::danger(), rArm, "R");
        }
        {
            bool mon = tree.getProperty(IDs::inputMonitor);
            drawToggle(header.monitorRect, ThemeColors::info(), mon, "In");
        }

        // Automation toggle
        bool autoEnabled = tree.getProperty(IDs::automationEnabled);
        painter.setPen(QPen(autoEnabled ? ThemeColors::accentBright() : ThemeColors::borderLight(), 1));
        painter.setBrush(autoEnabled ? ThemeColors::accentDim() : ThemeColors::bgWidget());
        painter.drawRoundedRect(header.autoRect, 2, 2);
        painter.setPen(autoEnabled ? ThemeColors::accentBright() : ThemeColors::textSecondary());
        painter.setFont(toggleFont);
        painter.drawText(header.autoRect, Qt::AlignCenter, "A");

        // Volume fader
        painter.setPen(ThemeColors::border());
        painter.setBrush(ThemeColors::bgPanel());
        painter.drawRoundedRect(header.volRect, 3, 3);

        // `volume` is a linear gain and may exceed 1.0 (e.g. +6 dB from a
        // saved project or MCP write), but the fader track is a normalized
        // 0..1 representation and the drag handler clamps to that range.
        // Clamp the display position to match so the thumb stays in the track.
        float vol = tree.getProperty(IDs::volume);
        float volClamped = (std::max)(0.0f, (std::min)(1.0f, vol));
        float volPos = volClamped * (header.volRect.width() - 8);
        QRect thumb((header.volRect.x() + static_cast<int>(volPos)),
                    header.volRect.y(), 8, header.volRect.height());
        painter.setPen(Qt::NoPen);
        painter.setBrush(ThemeColors::accent());
        painter.drawRoundedRect(thumb, 2, 2);

        // Volume label
        painter.setPen(ThemeColors::textSecondary());
        painter.setFont(smallFont);
        int db = static_cast<int>(20.0 * std::log10((std::max)(vol, 0.001f)));
        painter.drawText(header.volRect.adjusted(2, 0, -2, 0), Qt::AlignRight | Qt::AlignVCenter,
                         QString::number(db) + "dB");

        // Pan
        painter.setPen(ThemeColors::border());
        painter.setBrush(ThemeColors::bgPanel());
        painter.drawRoundedRect(header.panRect, 2, 2);

        float pan = tree.getProperty(IDs::pan);
        float panPos = (pan * 0.5f + 0.5f) * (header.panRect.width() - 4) + 2;
        painter.setPen(Qt::NoPen);
        painter.setBrush(ThemeColors::textSecondary());
        painter.drawEllipse(QPointF(header.panRect.x() + panPos,
                                    header.panRect.y() + header.panRect.height() / 2), 3, 3);

        // Pan label
        painter.setPen(ThemeColors::textSecondary());
        painter.drawText(QRect(header.panRect.x() + header.panRect.width() + 2,
                               header.panRect.y(), 20, header.panRect.height()),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QString::number(static_cast<int>(pan * 100.0)) + "%");

        // VU meter area
        painter.setPen(ThemeColors::border());
        painter.setBrush(QColor(20, 20, 22));
        painter.drawRect(header.vuRect);

        {
            int vh = header.vuRect.height() - 2;
            int barW = 3;
            int lx = header.vuRect.x() + 2;
            int rx = lx + barW + 1;

            auto drawVU = [&](int x, float level) {
                float db = 20.0f * std::log10((std::max)(level, 0.0001f));
                float norm = (std::max)(0.0f, (std::min)(1.0f, (db + 60.0f) / 60.0f));
                int barH = static_cast<int>(norm * vh);
                int by = header.vuRect.y() + 1 + vh - barH;
                QColor color = (db > -3.0f) ? ThemeColors::vuRed()
                            : (db > -12.0f) ? ThemeColors::vuYellow()
                            : ThemeColors::vuGreen();
                painter.fillRect(QRect(x, by, barW, barH), color);
            };

            drawVU(lx, header.vuLeft);
            drawVU(rx, header.vuRight);
        }

        trackY += trackH;
    }
    painter.restore();

    // Bottom fill
    int totalH = static_cast<int>(trackY);
    if (totalH < height())
        painter.fillRect(QRect(0, (std::max)(totalH, static_cast<int>(rulerHeight)), w, height() - (std::max)(totalH, static_cast<int>(rulerHeight))), ThemeColors::bgWindow());

    // Draw top ruler header background overlay
    QRect topRulerHeader(0, 0, w, static_cast<int>(rulerHeight));
    painter.fillRect(topRulerHeader, ThemeColors::bgWindow());
    painter.setPen(QPen(ThemeColors::border(), 1));
    painter.drawLine(0, static_cast<int>(rulerHeight) - 1, w, static_cast<int>(rulerHeight) - 1);
}

void TrackHeaderWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    int trackIdx = -1;
    int type = hitTest(event->pos(), trackIdx);

    // Double-click on empty area below all tracks -> add track
    if (trackIdx < 0 || trackIdx >= static_cast<int>(tracks.size()))
    {
        emit addTrackRequested();
        return;
    }

    // Double-click on a track name -> rename inline
    if (type == 6)
    {
        auto& header = headerFor(trackIdx);
        if (header.nameEdit == nullptr)
        {
            auto trackList = engine.getProjectModel().getTrackListTree();
            auto tree = trackList.getChild(trackIdx);
            header.nameEdit = new QLineEdit(
                QString::fromUtf8(tree.getProperty(IDs::name).toString().toRawUTF8()), this);
            header.nameEdit->setGeometry(header.nameRect);
            header.nameEdit->selectAll();
            header.nameEdit->show();
            header.nameEdit->setFocus();
            // Capture the QLineEdit* directly rather than re-resolving via
            // headerFor() in the lambda: if the track is deleted between the
            // press and the edit-commit, headerFor(trackIdx) would return the
            // shared dummy sentinel, and deleteLater/nullptr reset on the
            // dummy would leak the QLineEdit and corrupt shared state.
            QLineEdit* edit = header.nameEdit;
            connect(edit, &QLineEdit::editingFinished, this, [this, trackIdx, edit]() {
                QString newName = edit->text();
                edit->deleteLater();
                // Only clear the back-pointer on the live header — if the
                // track was removed, headerFor would alias the dummy and we
                // must not touch it.
                if (trackIdx >= 0 && trackIdx < static_cast<int>(tracks.size()))
                {
                    auto& hdr = tracks[trackIdx];
                    if (hdr.nameEdit == edit)
                        hdr.nameEdit = nullptr;
                }
                auto trackList = engine.getProjectModel().getTrackListTree();
                if (trackIdx >= 0 && trackIdx < trackList.getNumChildren())
                    trackList.getChild(trackIdx).setProperty(IDs::name, newName.toUtf8().constData(), &engine.getProjectModel().getUndoManager());
                update();
            });
        }
        return;
    }

    QWidget::mouseDoubleClickEvent(event);
}

void TrackHeaderWidget::mousePressEvent(QMouseEvent* event)
{
    int trackIdx = -1;
    int type = hitTest(event->pos(), trackIdx);
    if (trackIdx < 0 || trackIdx >= static_cast<int>(tracks.size()))
    {
        QWidget::mousePressEvent(event);
        return;
    }

    auto& header = headerFor(trackIdx);
    auto trackList = engine.getProjectModel().getTrackListTree();

    if (type == 1) // Mute
    {
        header.isMuted = !header.isMuted;
        auto trackList = engine.getProjectModel().getTrackListTree();
        if (trackIdx < trackList.getNumChildren())
            trackList.getChild(trackIdx).setProperty(IDs::isMuted, header.isMuted, &engine.getProjectModel().getUndoManager());
        update();
    }
    else if (type == 2) // Solo
    {
        header.isSoloed = !header.isSoloed;
        auto trackList = engine.getProjectModel().getTrackListTree();
        if (trackIdx < trackList.getNumChildren())
            trackList.getChild(trackIdx).setProperty(IDs::isSoloed, header.isSoloed, &engine.getProjectModel().getUndoManager());
        update();
    }
    else if (type == 3) // Record arm
    {
        auto trackList = engine.getProjectModel().getTrackListTree();
        if (trackIdx < trackList.getNumChildren())
        {
            auto tree = trackList.getChild(trackIdx);
            bool armed = !tree.getProperty(IDs::isArm);
            tree.setProperty(IDs::isArm, armed, &engine.getProjectModel().getUndoManager());
        }
        update();
    }
    else if (type == 7) // Automation
    {
        emit automationToggled(trackIdx);
    }
    else if (type == 8) // Input monitor
    {
        auto trackList = engine.getProjectModel().getTrackListTree();
        if (trackIdx < trackList.getNumChildren())
        {
            auto tree = trackList.getChild(trackIdx);
            bool mon = !tree.getProperty(IDs::inputMonitor);
            tree.setProperty(IDs::inputMonitor, mon, &engine.getProjectModel().getUndoManager());
            emit inputMonitoringChanged(trackIdx, mon);
        }
    }
    else if (type == 4) // Volume drag
    {
        engine.getProjectModel().getUndoManager().beginNewTransaction("Adjust volume");
        dragTrack = trackIdx;
        dragStart = event->pos();
        dragStartValue = static_cast<float>(trackList.getChild(trackIdx).getProperty(IDs::volume));
    }
    else if (type == 5) // Pan drag
    {
        engine.getProjectModel().getUndoManager().beginNewTransaction("Adjust pan");
        dragTrack = trackIdx;
        dragStart = event->pos();
        dragStartValue = static_cast<float>(trackList.getChild(trackIdx).getProperty(IDs::pan));
    }
    else if (type == 0) // Track body — check for resize at bottom edge
    {
        auto& h = headerFor(trackIdx);
        QRect resizeHandle(h.bounds.x(), h.bounds.bottom() - 4, h.bounds.width(), 8);
        if (resizeHandle.contains(event->pos()))
        {
            resizeTrack = trackIdx;
            dragStart = event->pos();
        }
        else
        {
            selectedTrack = trackIdx;
            emit trackSelectionChanged(trackIdx);
            update();
        }
    }
    else if (type == 6) // Name
    {
        if (header.nameEdit == nullptr)
        {
            auto trackList = engine.getProjectModel().getTrackListTree();
            auto tree = trackList.getChild(trackIdx);

            header.nameEdit = new QLineEdit(
                QString::fromUtf8(tree.getProperty(IDs::name).toString().toRawUTF8()), this);
            header.nameEdit->setGeometry(header.nameRect);
            header.nameEdit->selectAll();
            header.nameEdit->show();
            header.nameEdit->setFocus();

            // Capture the QLineEdit* directly; see the matching handler in
            // mouseDoubleClickEvent for why we don't re-resolve via headerFor()
            // here (the track may be deleted between press and edit-commit).
            QLineEdit* edit = header.nameEdit;
            connect(edit, &QLineEdit::editingFinished, this, [this, trackIdx, edit]() {
                QString newName = edit->text();
                edit->deleteLater();
                if (trackIdx >= 0 && trackIdx < static_cast<int>(tracks.size()))
                {
                    auto& hdr = tracks[trackIdx];
                    if (hdr.nameEdit == edit)
                        hdr.nameEdit = nullptr;
                }
                auto trackList = engine.getProjectModel().getTrackListTree();
                if (trackIdx >= 0 && trackIdx < trackList.getNumChildren())
                    trackList.getChild(trackIdx).setProperty(IDs::name, newName.toUtf8().constData(), &engine.getProjectModel().getUndoManager());
                update();
            });
        }
    }
}

void TrackHeaderWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (resizeTrack >= 0)
    {
        QPoint delta = event->pos() - dragStart;
        double oldH = getTrackHeight(resizeTrack);
        double newH = oldH + delta.y();
        setTrackHeight(resizeTrack, newH);
        dragStart = event->pos();
        update();
        return;
    }

    if (dragTrack < 0 || dragTrack >= static_cast<int>(tracks.size()))
    {
        // Check for resize cursor
        int trackIdx = -1;
        int type = hitTest(event->pos(), trackIdx);
        if (trackIdx >= 0)
        {
            auto& h = headerFor(trackIdx);
            QRect resizeHandle(h.bounds.x(), h.bounds.bottom() - 4, h.bounds.width(), 8);
            setCursor(resizeHandle.contains(event->pos()) ? Qt::SizeVerCursor : Qt::ArrowCursor);
        }
        QWidget::mouseMoveEvent(event);
        return;
    }

    auto& header = headerFor(dragTrack);
    QPoint delta = event->pos() - dragStart;

    if (header.volRect.contains(dragStart))
    {
        float range = static_cast<float>(header.volRect.width() - 8);
        if (range > 0)
        {
            float newVol = (std::max)(0.0f, (std::min)(1.0f,
                dragStartValue + static_cast<float>(delta.x()) / range));
            commitVolume(dragTrack, newVol);
        }
    }
    else if (header.panRect.contains(dragStart))
    {
        float range = static_cast<float>(header.panRect.width() - 4);
        if (range > 0)
        {
            float newPan = (std::max)(-1.0f, (std::min)(1.0f,
                dragStartValue + static_cast<float>(delta.x()) / (range * 0.5f)));
            commitPan(dragTrack, newPan);
        }
    }

    update();
}

void TrackHeaderWidget::mouseReleaseEvent(QMouseEvent* event)
{
    Q_UNUSED(event);
    dragTrack = -1;
    resizeTrack = -1;
}

void TrackHeaderWidget::contextMenuEvent(QContextMenuEvent* event)
{
    int trackIdx = -1;
    hitTest(event->pos(), trackIdx);

    if (trackIdx < 0 || trackIdx >= static_cast<int>(tracks.size()))
        buildEmptyAreaMenu(event->globalPos());
    else
        buildTrackMenu(trackIdx, event->globalPos());

    event->accept();
}

void TrackHeaderWidget::buildEmptyAreaMenu(const QPoint& globalPos)
{
    QMenu menu;

    auto* addAction = menu.addAction("Add Track");
    connect(addAction, &QAction::triggered, this, &TrackHeaderWidget::addTrackRequested);

    auto* addFxs = menu.addMenu("Add Track with FX");
    auto* eqTrk = addFxs->addAction("EQ");
    connect(eqTrk, &QAction::triggered, this, [this]() { emit addTrackWithFX("eq"); });
    auto* compTrk = addFxs->addAction("Compressor");
    connect(compTrk, &QAction::triggered, this, [this]() { emit addTrackWithFX("compressor"); });
    auto* revTrk = addFxs->addAction("Reverb");
    connect(revTrk, &QAction::triggered, this, [this]() { emit addTrackWithFX("reverb"); });
    auto* delTrk = addFxs->addAction("Delay");
    connect(delTrk, &QAction::triggered, this, [this]() { emit addTrackWithFX("delay"); });

    auto& pluginManager = engine.getPluginManager();
    const auto& plugins = pluginManager.getPlugins();
    if (!plugins.empty())
    {
        auto addPluginToSubmenu = [&](QMenu* parent, const juce::PluginDescription& d)
        {
            QString label = QString("[%1] %2")
                .arg(QString::fromUtf8(d.pluginFormatName.toRawUTF8()))
                .arg(QString::fromUtf8(d.name.toRawUTF8()));
            auto* act = parent->addAction(label);
            connect(act, &QAction::triggered, this,
                [this, d]() {
                    emit addTrackWithPlugin(d.fileOrIdentifier, d.pluginFormatName);
                });
        };

        auto* instMenu = addFxs->addMenu("Instrument");
        auto* fxMenu = addFxs->addMenu("Effect");
        for (const auto& desc : plugins)
        {
            if (pluginManager.isBlacklisted(desc.fileOrIdentifier))
                continue;

            if (desc.isInstrument)
                addPluginToSubmenu(instMenu, desc);
            else
                addPluginToSubmenu(fxMenu, desc);
        }

        // Remove empty submenus
        if (instMenu->actions().isEmpty())
            addFxs->removeAction(instMenu->menuAction());
        if (fxMenu->actions().isEmpty())
            addFxs->removeAction(fxMenu->menuAction());
    }

    menu.exec(globalPos);
}

void TrackHeaderWidget::buildTrackMenu(int trackIdx, const QPoint& globalPos)
{
    QMenu menu;

    auto* renameAction = menu.addAction("Rename Track");
    connect(renameAction, &QAction::triggered, this, [this, trackIdx]() {
        auto trackList = engine.getProjectModel().getTrackListTree();
        if (trackIdx >= trackList.getNumChildren()) return;
        auto tree = trackList.getChild(trackIdx);
        QString current = QString::fromUtf8(tree.getProperty(IDs::name).toString().toRawUTF8());
        bool ok = false;
        QString newName = QInputDialog::getText(const_cast<QWidget*>(static_cast<const QWidget*>(this)),
            "Rename Track", "Track name:", QLineEdit::Normal, current, &ok);
        if (ok && !newName.isEmpty())
        {
            tree.setProperty(IDs::name, newName.toUtf8().constData(),
                &engine.getProjectModel().getUndoManager());
            update();
        }
    });

    auto* colorAction = menu.addAction("Track Color...");
    connect(colorAction, &QAction::triggered, this, [this, trackIdx]() {
        auto trackList = engine.getProjectModel().getTrackListTree();
        if (trackIdx >= trackList.getNumChildren()) return;
        auto tree = trackList.getChild(trackIdx);

        int currentColor = tree.getProperty(IDs::color, static_cast<int>(0xFF4488CC));
        QColor initial((currentColor >> 16) & 0xFF, (currentColor >> 8) & 0xFF, currentColor & 0xFF);
        QColor chosen = QColorDialog::getColor(initial, const_cast<QWidget*>(static_cast<const QWidget*>(this)),
            "Choose Track Color");
        if (chosen.isValid())
        {
            int newColor = (0xFF << 24) | (chosen.red() << 16) | (chosen.green() << 8) | chosen.blue();
            tree.setProperty(IDs::color, newColor, &engine.getProjectModel().getUndoManager());
            update();
        }
    });

    menu.addSeparator();

    // Add FX Slot submenu
    auto* fxMenu = menu.addMenu("Add FX Slot");
    auto* eqAction = fxMenu->addAction("EQ");
    connect(eqAction, &QAction::triggered, this, [this, trackIdx]() { addFXToTrack(trackIdx, "eq"); });
    auto* compAction = fxMenu->addAction("Compressor");
    connect(compAction, &QAction::triggered, this, [this, trackIdx]() { addFXToTrack(trackIdx, "compressor"); });
    auto* reverbAction = fxMenu->addAction("Reverb");
    connect(reverbAction, &QAction::triggered, this, [this, trackIdx]() { addFXToTrack(trackIdx, "reverb"); });
    auto* delayAction = fxMenu->addAction("Delay");
    connect(delayAction, &QAction::triggered, this, [this, trackIdx]() { addFXToTrack(trackIdx, "delay"); });

    // Plugin submenus (skip blacklisted)
    auto& pluginManager = engine.getPluginManager();
    const auto& plugins = pluginManager.getPlugins();
    if (!plugins.empty())
    {
        fxMenu->addSeparator();

        auto addPluginToSubmenu = [&](QMenu* parent, const juce::PluginDescription& d)
        {
            QString label = QString("[%1] %2")
                .arg(QString::fromUtf8(d.pluginFormatName.toRawUTF8()))
                .arg(QString::fromUtf8(d.name.toRawUTF8()));
            auto* pluginAction = parent->addAction(label);
            connect(pluginAction, &QAction::triggered, this,
                [this, trackIdx, d]() {
                    addPluginToTrack(trackIdx,
                        d.fileOrIdentifier,
                        d.pluginFormatName);
                });
        };

        auto* instMenu = fxMenu->addMenu("Instrument");
        auto* effMenu = fxMenu->addMenu("Effect");
        for (const auto& desc : plugins)
        {
            if (pluginManager.isBlacklisted(desc.fileOrIdentifier))
                continue;

            if (desc.isInstrument)
                addPluginToSubmenu(instMenu, desc);
            else
                addPluginToSubmenu(effMenu, desc);
        }

        if (instMenu->actions().isEmpty())
            fxMenu->removeAction(instMenu->menuAction());
        if (effMenu->actions().isEmpty())
            fxMenu->removeAction(effMenu->menuAction());
    }

    menu.addSeparator();

    auto* midiChAction = menu.addAction("MIDI Channel...");
    connect(midiChAction, &QAction::triggered, this, [this, trackIdx]() {
        auto trackList = engine.getProjectModel().getTrackListTree();
        if (trackIdx >= trackList.getNumChildren()) return;
        auto tree = trackList.getChild(trackIdx);
        int current = tree.getProperty(IDs::midiChannel, 1);
        bool ok = false;
        int channel = QInputDialog::getInt(
            const_cast<QWidget*>(static_cast<const QWidget*>(this)),
            "MIDI Channel",
            "MIDI channel (1-16):",
            current, 1, 16, 1, &ok);
        if (ok)
        {
            tree.setProperty(IDs::midiChannel, channel,
                &engine.getProjectModel().getUndoManager());
            update();
        }
    });

    auto* deleteAction = menu.addAction("Delete Track");
    connect(deleteAction, &QAction::triggered, this, [this, trackIdx]() {
        auto& model = engine.getProjectModel();
        auto trackList = model.getTrackListTree();
        if (trackIdx < trackList.getNumChildren())
        {
            trackList.removeChild(trackList.getChild(trackIdx),
                &model.getUndoManager());
            engine.getMainProcessor()->rebuildRoutingGraph();
            rebuild();
        }
    });

    menu.exec(globalPos);
}

void TrackHeaderWidget::addFXToTrack(int trackIndex, const juce::String& type)
{
    if (engine.getProjectModel().addFxSlot(trackIndex, type.toStdString()) < 0) return;

    engine.getMainProcessor()->rebuildTrackFX(trackIndex);
    rebuild();
    emit fxSlotAdded(trackIndex);
}

void TrackHeaderWidget::addPluginToTrack(int trackIndex, const juce::String& pluginID, const juce::String& /*pluginFormat*/)
{
    if (engine.getProjectModel().addFxSlot(trackIndex, "plugin", -1, pluginID.toStdString()) < 0) return;

    engine.getMainProcessor()->rebuildTrackFX(trackIndex);
    rebuild();
    emit fxSlotAdded(trackIndex);
}

void TrackHeaderWidget::focusOutEvent(QFocusEvent* event)
{
    QWidget::focusOutEvent(event);
    bool wasDragging = false;
    for (auto& t : tracks)
    {
        if (t.draggingVol || t.draggingPan)
        {
            t.draggingVol = false;
            t.draggingPan = false;
            wasDragging = true;
        }
    }
    dragTrack = -1;
    resizeTrack = -1;
    if (wasDragging) update();
}

void TrackHeaderWidget::leaveEvent(QEvent* event)
{
    QWidget::leaveEvent(event);
    bool wasDragging = false;
    for (auto& t : tracks)
    {
        if (t.draggingVol || t.draggingPan)
        {
            t.draggingVol = false;
            t.draggingPan = false;
            wasDragging = true;
        }
    }
    if (wasDragging) update();
}

bool TrackHeaderWidget::eventFilter(QObject* obj, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonRelease && obj != this)
    {
        bool wasDragging = false;
        for (auto& t : tracks)
        {
            if (t.draggingVol || t.draggingPan)
            {
                t.draggingVol = false;
                t.draggingPan = false;
                wasDragging = true;
            }
        }
        if (dragTrack >= 0 || resizeTrack >= 0)
        {
            dragTrack = -1;
            resizeTrack = -1;
            wasDragging = true;
        }
        if (wasDragging) update();
    }
    return QWidget::eventFilter(obj, event);
}
