#include "TrackHeaderWidget.h"
#include "Theme.h"
#include "../engine/LevelMeter.h"
#include <QPainter>
#include <QMouseEvent>
#include <QVBoxLayout>
#include <QApplication>
#include <QMenu>
#include <QAction>
#include <QInputDialog>
#include <QColorDialog>

TrackHeaderWidget::TrackHeaderWidget(AudioEngine& ae, QWidget* parent)
    : QWidget(parent), engine(ae)
{
    setFixedWidth(static_cast<int>(headerWidth));
    setMinimumHeight(100);

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
    update();
}

void TrackHeaderWidget::setTrackHeight(int index, double height)
{
    auto trackList = engine.getProjectModel().getTrackListTree();
    if (index >= 0 && index < trackList.getNumChildren())
    {
        trackList.getChild(index).setProperty(IDs::trackHeight,
            std::max(40.0, height), &engine.getProjectModel().getUndoManager());
        update();
    }
}

double TrackHeaderWidget::getTrackHeight(int index) const
{
    auto trackList = engine.getProjectModel().getTrackListTree();
    if (index >= 0 && index < trackList.getNumChildren())
    {
        double h = trackList.getChild(index).getProperty(IDs::trackHeight, defaultTrackHeight);
        return std::max(40.0, h);
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
    update();
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
    for (auto& h : tracks)
        if (h.trackIndex == trackIndex) return h;
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
    return QSize(hintW, std::max(hintH, minimumHeight()));
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
        rebuild();

    double trackY = rulerHeight - scrollOffset;
    double trackH = defaultTrackHeight;

    painter.save();
    painter.setClipRect(0, static_cast<int>(rulerHeight), w, height() - static_cast<int>(rulerHeight));

    for (int i = 0; i < count; ++i)
    {
        auto tree = trackList.getChild(i);
        trackH = getTrackHeight(i);

        int y = static_cast<int>(trackY);
        int h = static_cast<int>(trackH);
        QRect row(0, y, w, h);

        auto& header = headerFor(i);
        header.bounds = row;

        // Background
        QColor bg = (i % 2 == 0) ? ThemeColors::trackFill1() : ThemeColors::trackFill2();
        painter.fillRect(row, bg);

        // Color strip
        {
            int tc = tree.getProperty(IDs::color, static_cast<int>(0xFF4488CC));
            QColor trackColor((tc >> 16) & 0xFF, (tc >> 8) & 0xFF, tc & 0xFF);
            painter.fillRect(QRect(0, y, 3, h), trackColor);
        }

        // Name
        header.nameRect = QRect(8, y + 4, w - 16, 16);
        painter.setPen(Qt::white);
        QFont f = painter.font();
        f.setPointSize(9);
        f.setBold(true);
        painter.setFont(f);
    QString name = QString::fromUtf8(tree.getProperty(IDs::name).toString().toRawUTF8());
    painter.drawText(header.nameRect, Qt::AlignLeft | Qt::AlignVCenter, name);

        // Mute / Solo / Arm buttons
        int btnY = y + 24;
        int btnSize = 14;
        int btnSpacing = 4;
        int btnX = 6;

        auto drawToggle = [&](QRect& rect, int x, QColor onColor, bool active, const QString& label) {
            rect = QRect(x, btnY, btnSize, btnSize);
            painter.setPen(QPen(active ? onColor.lighter(130) : ThemeColors::borderLight(), 1));
            painter.setBrush(active ? onColor : ThemeColors::bgWidget());
            painter.drawRoundedRect(rect, 2, 2);
            painter.setPen(active ? Qt::white : ThemeColors::textSecondary());
            QFont sf = painter.font();
            sf.setPointSize(6);
            sf.setBold(true);
            painter.setFont(sf);
            painter.drawText(rect, Qt::AlignCenter, label);
        };

        drawToggle(header.muteRect, btnX, ThemeColors::danger(), header.isMuted, "M");
        drawToggle(header.soloRect, btnX + btnSize + btnSpacing, ThemeColors::warning(), header.isSoloed, "S");
        {
            bool rArm = tree.getProperty(IDs::isArm);
            drawToggle(header.armRect, btnX + 2 * (btnSize + btnSpacing), ThemeColors::danger(), rArm, "R");
        }

        // Automation toggle
        int autoX = btnX + 3 * (btnSize + btnSpacing);
        header.autoRect = QRect(autoX, btnY, btnSize, btnSize);
        bool autoEnabled = tree.getProperty(IDs::automationEnabled);
        painter.setPen(QPen(autoEnabled ? ThemeColors::accentBright() : ThemeColors::borderLight(), 1));
        painter.setBrush(autoEnabled ? ThemeColors::accentDim() : ThemeColors::bgWidget());
        painter.drawRoundedRect(header.autoRect, 2, 2);
        painter.setPen(autoEnabled ? ThemeColors::accentBright() : ThemeColors::textSecondary());
        painter.drawText(header.autoRect, Qt::AlignCenter, "A");

        // Volume fader
        int faderY = y + 44;
        int faderH = 20;
        int faderW = w - 20;
        int faderX = 10;
        header.volRect = QRect(faderX, faderY, faderW, faderH);

        painter.setPen(ThemeColors::border());
        painter.setBrush(ThemeColors::bgPanel());
        painter.drawRoundedRect(header.volRect, 3, 3);

        float vol = tree.getProperty(IDs::volume);
        float volPos = vol * (faderW - 8);
        QRect thumb((faderX + static_cast<int>(volPos)), faderY, 8, faderH);
        painter.setPen(Qt::NoPen);
        painter.setBrush(ThemeColors::accent());
        painter.drawRoundedRect(thumb, 2, 2);

        // Volume label
        painter.setPen(ThemeColors::textSecondary());
        QFont sf2 = painter.font();
        sf2.setPointSize(6);
        painter.setFont(sf2);
        int db = static_cast<int>(20.0 * std::log10(std::max(vol, 0.001f)));
        painter.drawText(header.volRect.adjusted(2, 0, -2, 0), Qt::AlignRight | Qt::AlignVCenter,
                         QString::number(db) + "dB");

        // Pan
        int panY = y + 66;
        int panW = faderW;
        int panH = 10;
        header.panRect = QRect(faderX, panY, panW, panH);

        painter.setPen(ThemeColors::border());
        painter.setBrush(ThemeColors::bgPanel());
        painter.drawRoundedRect(header.panRect, 2, 2);

        float pan = tree.getProperty(IDs::pan);
        float panPos = (pan * 0.5f + 0.5f) * (panW - 4) + 2;
        painter.setPen(Qt::NoPen);
        painter.setBrush(ThemeColors::textSecondary());
        painter.drawEllipse(QPointF(faderX + panPos, panY + panH / 2), 3, 3);

        // Pan label
        painter.setPen(ThemeColors::textSecondary());
        painter.drawText(QRect(faderX + panW + 2, panY, 20, panH), Qt::AlignLeft | Qt::AlignVCenter,
                         QString::number(static_cast<int>(pan * 100.0)) + "%");

        // VU meter area
        header.vuRect = QRect(w - 14, y + 4, 10, h - 8);
        painter.setPen(ThemeColors::border());
        painter.setBrush(QColor(20, 20, 22));
        painter.drawRect(header.vuRect);

        {
            int vh = header.vuRect.height() - 2;
            int barW = 3;
            int lx = header.vuRect.x() + 2;
            int rx = lx + barW + 1;

            auto drawVU = [&](int x, float level) {
                float db = 20.0f * std::log10(std::max(level, 0.0001f));
                float norm = std::max(0.0f, std::min(1.0f, (db + 60.0f) / 60.0f));
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
        painter.fillRect(QRect(0, std::max(totalH, static_cast<int>(rulerHeight)), w, height() - std::max(totalH, static_cast<int>(rulerHeight))), ThemeColors::bgWindow());

    // Draw top ruler header background overlay
    QRect topRulerHeader(0, 0, w, static_cast<int>(rulerHeight));
    painter.fillRect(topRulerHeader, ThemeColors::bgWindow());
    painter.setPen(QPen(ThemeColors::border(), 1));
    painter.drawLine(0, static_cast<int>(rulerHeight) - 1, w, static_cast<int>(rulerHeight) - 1);
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
    else if (type == 4) // Volume drag
    {
        dragTrack = trackIdx;
        dragStart = event->pos();
        dragStartValue = static_cast<float>(trackList.getChild(trackIdx).getProperty(IDs::volume));
    }
    else if (type == 5) // Pan drag
    {
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

            connect(header.nameEdit, &QLineEdit::editingFinished, this, [this, trackIdx]() {
                auto& hdr = headerFor(trackIdx);
                if (hdr.nameEdit)
                {
                    QString newName = hdr.nameEdit->text();
                    auto trackList = engine.getProjectModel().getTrackListTree();
                    if (trackIdx < trackList.getNumChildren())
                        trackList.getChild(trackIdx).setProperty(IDs::name, newName.toUtf8().constData(), &engine.getProjectModel().getUndoManager());
                    hdr.nameEdit->deleteLater();
                    hdr.nameEdit = nullptr;
                    update();
                }
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
            float newVol = std::max(0.0f, std::min(1.0f,
                dragStartValue + static_cast<float>(delta.x()) / range));
            commitVolume(dragTrack, newVol);
        }
    }
    else if (header.panRect.contains(dragStart))
    {
        float range = static_cast<float>(header.panRect.width() - 4);
        if (range > 0)
        {
            float newPan = std::max(-1.0f, std::min(1.0f,
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
    {
        QWidget::contextMenuEvent(event);
        return;
    }

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

    menu.exec(event->globalPos());
    event->accept();
}
