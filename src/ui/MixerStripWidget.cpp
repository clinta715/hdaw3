#include "MixerStripWidget.h"
#include "Theme.h"
#include "../engine/LevelMeter.h"
#include <QPainter>
#include <QMouseEvent>
#include <QMenu>
#include <QAction>
#include <QInputDialog>
#include <QApplication>
#include <cmath>

MixerStripWidget::MixerStripWidget(int idx, AudioEngine& ae, QWidget* parent)
    : QWidget(parent), trackIndex(idx), engine(ae)
{
    setFixedSize(stripWidth, stripHeight);
    setMinimumWidth(stripWidth);
    setMouseTracking(true);
    qApp->installEventFilter(this);

    layoutRects();

    connect(&vuTimer, &QTimer::timeout, this, &MixerStripWidget::updateVU);
    vuTimer.start(16);

    // Read initial values from ValueTree
    auto trackList = engine.getProjectModel().getTrackListTree();
    if (trackIndex < trackList.getNumChildren())
    {
        auto tree = trackList.getChild(trackIndex);
        name = QString::fromUtf8(tree.getProperty(IDs::name).toString().toRawUTF8());
        volume = tree.getProperty(IDs::volume);
        pan = tree.getProperty(IDs::pan);
        muted = tree.getProperty(IDs::isMuted);
        soloed = tree.getProperty(IDs::isSoloed);
    }
}

MixerStripWidget::~MixerStripWidget() = default;

void MixerStripWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    layoutRects();
}

void MixerStripWidget::layoutRects()
{
    int w = width();

    const int vuY = 22;
    const int vuH = 50;
    const int vuSpacing = 2;
    const int vuW = (w - vuSpacing * 3) / 2;
    const int btnSize = 12;
    const int btnX = 4;
    const int btnY = vuY + vuH + 4;
    const int faderY = btnY + btnSize + 6;
    const int faderH = w - 16;
    const int faderX = (w - 8) / 2;
    const int panH = 10;
    const int panMargin = 6;

    nameRect = QRect(0, 5, w, 14);
    vuLeftRect = QRect(vuSpacing, vuY, vuW, vuH);
    vuRightRect = QRect(vuSpacing * 2 + vuW, vuY, vuW, vuH);
    muteRect = QRect(btnX, btnY, btnSize, btnSize);
    soloRect = QRect(btnX + btnSize + 2, btnY, btnSize, btnSize);
    fxBtnRect = QRect(btnX + 2 * (btnSize + 2), btnY, btnSize, btnSize);
    faderRect = QRect(faderX - 2, faderY, 12, faderH);
    faderTrackRect = QRect(faderX, faderY, 8, faderH);
    volLabelRect = QRect(0, faderY + faderH + 2, w, 12);
    panTrackRect = QRect(panMargin, faderY + faderH + 16, w - panMargin * 2, panH);
    panLabelRect = QRect(0, panTrackRect.y() + panH + 1, w, 10);
}

void MixerStripWidget::setTrackName(const QString& n) { name = n; update(); }
void MixerStripWidget::setVolume(float vol) { volume = vol; update(); }
void MixerStripWidget::setPan(float p) { pan = p; update(); }
void MixerStripWidget::setMuted(bool m) { muted = m; update(); }
void MixerStripWidget::setSoloed(bool s) { soloed = s; update(); }

void MixerStripWidget::updateVU()
{
    auto* track = engine.getMainProcessor()->getTrack(trackIndex);
    if (track != nullptr)
    {
        auto& meter = track->getMeter();
        float left = meter.getLeftLevel();
        float right = meter.getRightLevel();

        if (left > currentLeft) currentLeft = left;
        else currentLeft *= 0.85f;

        if (right > currentRight) currentRight = right;
        else currentRight *= 0.85f;

        update();
    }
}

void MixerStripWidget::paintEvent(QPaintEvent*)
{
    auto trackList = engine.getProjectModel().getTrackListTree();
    if (trackIndex < trackList.getNumChildren())
    {
        auto tree = trackList.getChild(trackIndex);
        name = QString::fromUtf8(tree.getProperty(IDs::name).toString().toRawUTF8());
        volume = tree.getProperty(IDs::volume);
        pan = tree.getProperty(IDs::pan);
        muted = tree.getProperty(IDs::isMuted);
        soloed = tree.getProperty(IDs::isSoloed);
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    int w = width();

    // Background
    painter.fillRect(rect(), ThemeColors::bgPanel());

    // Color strip at top
    painter.fillRect(QRect(0, 0, w, 3), ThemeColors::accent());

    // Name
    painter.setPen(Qt::white);
    QFont f = painter.font();
    f.setPointSize(7);
    painter.setFont(f);
    painter.drawText(nameRect, Qt::AlignCenter, name);

    // VU Meter
    auto drawVUBar = [&](const QRect& slot, float level) {
        float db = 20.0f * std::log10((std::max)(level, 0.0001f));
        float normalized = (db + 60.0f) / 60.0f;
        normalized = (std::max)(0.0f, (std::min)(1.0f, normalized));
        int barH = static_cast<int>(normalized * slot.height());

        QColor color;
        if (db > -3.0f) color = ThemeColors::vuRed();
        else if (db > -12.0f) color = ThemeColors::vuYellow();
        else color = ThemeColors::vuGreen();

        QRect r(slot.x(), slot.y() + slot.height() - barH, slot.width(), barH);
        painter.fillRect(r, color);
    };

    painter.fillRect(QRect(vuLeftRect.x(), vuLeftRect.y(),
                           vuRightRect.right() - vuLeftRect.x() + 1, vuLeftRect.height()),
                     QColor(20, 20, 22));
    drawVUBar(vuLeftRect, currentLeft);
    drawVUBar(vuRightRect, currentRight);

    // Mute / Solo buttons
    auto drawBtn = [&](const QRect& rect, QColor onColor, bool active, const QString& label) {
        painter.setPen(QPen(active ? onColor.lighter(130) : ThemeColors::borderLight(), 1));
        painter.setBrush(active ? onColor : ThemeColors::bgWidget());
        painter.drawRoundedRect(rect, 2, 2);
        painter.setPen(active ? Qt::white : ThemeColors::textSecondary());
        QFont sf = painter.font();
        sf.setPointSize(7);
        sf.setBold(true);
        painter.setFont(sf);
        painter.drawText(rect, Qt::AlignCenter, label);
    };

    drawBtn(muteRect, ThemeColors::danger(), muted, "M");
    drawBtn(soloRect, ThemeColors::warning(), soloed, "S");

    // FX button
    painter.setPen(QPen(ThemeColors::borderLight(), 1));
    painter.setBrush(ThemeColors::bgWidget());
    painter.drawRoundedRect(fxBtnRect, 2, 2);
    painter.setPen(ThemeColors::accentBright());
    QFont sf2 = painter.font();
    sf2.setPointSize(7);
    sf2.setBold(true);
    painter.setFont(sf2);
    painter.drawText(fxBtnRect, Qt::AlignCenter, "FX");

    // Volume fader

    // Draw groove
    painter.setPen(ThemeColors::border());
    painter.setBrush(ThemeColors::bgWidget());
    painter.drawRoundedRect(faderTrackRect, 3, 3);

    // Draw handle (1.0 at the top, 0.0 at the bottom)
    float volPos = (1.0f - volume) * (faderTrackRect.height() - 8);
    painter.setPen(Qt::NoPen);
    painter.setBrush(ThemeColors::accent());
    painter.drawRoundedRect(QRect(faderTrackRect.x() - 2,
                                  faderTrackRect.y() + static_cast<int>(volPos), 12, 8), 2, 2);

    // Volume db label
    int db = static_cast<int>(20.0 * std::log10((std::max)(volume, 0.001f)));
    painter.setPen(ThemeColors::textSecondary());
    f.setPointSize(6);
    painter.setFont(f);
    painter.drawText(volLabelRect, Qt::AlignCenter, QString::number(db) + "dB");

    // Pan control
    painter.setPen(ThemeColors::border());
    painter.setBrush(ThemeColors::bgPanel());
    painter.drawRoundedRect(panTrackRect, 2, 2);

    float panPos = (pan * 0.5f + 0.5f) * (panTrackRect.width() - 4) + 2;
    painter.setPen(Qt::NoPen);
    painter.setBrush(ThemeColors::textSecondary());
    painter.drawEllipse(QPointF(panTrackRect.x() + panPos, panTrackRect.center().y()), 3, 3);

    painter.setPen(ThemeColors::textSecondary());
    painter.drawText(panLabelRect, Qt::AlignCenter,
                     QString::number(static_cast<int>(pan * 100.0f)) + "%");
}

void MixerStripWidget::mousePressEvent(QMouseEvent* event)
{
    QPoint pos = event->pos();

    if (fxBtnRect.contains(pos))
    {
        emit fxButtonClicked(trackIndex);
        event->accept();
        return;
    }

    if (muteRect.contains(pos))
    {
        muted = !muted;
        emit muteToggled(trackIndex, muted);
        update();
        event->accept();
    }
    else if (soloRect.contains(pos))
    {
        soloed = !soloed;
        emit soloToggled(trackIndex, soloed);
        update();
        event->accept();
    }
    else if (faderRect.contains(pos))
    {
        draggingVol = true;
        event->accept();
    }
    else if (panTrackRect.contains(pos))
    {
        draggingPan = true;
        event->accept();
    }
    else
    {
        // Click on empty strip area — let the parent (scroll area) handle it.
        QWidget::mousePressEvent(event);
    }
}

void MixerStripWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (draggingVol)
    {
        int y = event->pos().y();
        float range = static_cast<float>(faderTrackRect.height());

        if (range > 0)
        {
            float newVol = 1.0f - (static_cast<float>(y - faderTrackRect.y()) / range);
            newVol = (std::max)(0.0f, (std::min)(1.0f, newVol));
            volume = newVol;
            emit volumeChanged(trackIndex, newVol);
            update();
        }
        event->accept();
    }
    else if (draggingPan)
    {
        int x = event->pos().x();
        float range = static_cast<float>(panTrackRect.width() - 6);
        if (range > 0)
        {
            float newPan = (std::max)(-1.0f, (std::min)(1.0f,
                (static_cast<float>(x - panTrackRect.x() - 3) / range) * 2.0f - 1.0f));
            pan = newPan;
            emit panChanged(trackIndex, newPan);
            update();
        }
        event->accept();
    }
    else
    {
        QWidget::mouseMoveEvent(event);
    }
}

void MixerStripWidget::mouseReleaseEvent(QMouseEvent* event)
{
    // We only get here when a press was accepted by this widget (started a
    // fader/pan drag). Consume the matching release so it doesn't propagate
    // to the parent scroll area.
    draggingVol = false;
    draggingPan = false;
    event->accept();
}

void MixerStripWidget::contextMenuEvent(QContextMenuEvent* event)
{
    QMenu menu;

    auto* renameAction = menu.addAction("Rename Track");
    connect(renameAction, &QAction::triggered, this, [this]() {
        auto trackList = engine.getProjectModel().getTrackListTree();
        if (trackIndex >= trackList.getNumChildren()) return;
        auto tree = trackList.getChild(trackIndex);
        QString current = QString::fromUtf8(tree.getProperty(IDs::name).toString().toRawUTF8());
        bool ok = false;
        QString newName = QInputDialog::getText(this, "Rename Track", "Track name:",
            QLineEdit::Normal, current, &ok);
        if (ok && !newName.isEmpty())
        {
            tree.setProperty(IDs::name, newName.toUtf8().constData(),
                &engine.getProjectModel().getUndoManager());
            update();
        }
    });

    auto* fxAction = menu.addAction("FX Chain");
    connect(fxAction, &QAction::triggered, this, [this]() {
        emit fxButtonClicked(trackIndex);
    });

    menu.addSeparator();

    auto* deleteAction = menu.addAction("Delete Track");
    connect(deleteAction, &QAction::triggered, this, [this]() {
        auto& model = engine.getProjectModel();
        auto trackList = model.getTrackListTree();
        if (trackIndex < trackList.getNumChildren())
        {
            trackList.removeChild(trackList.getChild(trackIndex),
                &model.getUndoManager());
            engine.getMainProcessor()->rebuildRoutingGraph();
            emit trackDeleted();
        }
    });

    menu.exec(event->globalPos());
    event->accept();
}

void MixerStripWidget::focusOutEvent(QFocusEvent* event)
{
    QWidget::focusOutEvent(event);
    if (draggingVol || draggingPan)
    {
        draggingVol = false;
        draggingPan = false;
        update();
    }
}

void MixerStripWidget::leaveEvent(QEvent* event)
{
    QWidget::leaveEvent(event);
    if (draggingVol || draggingPan)
    {
        draggingVol = false;
        draggingPan = false;
        update();
    }
}

bool MixerStripWidget::eventFilter(QObject* obj, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonRelease && (draggingVol || draggingPan) && obj != this)
    {
        draggingVol = false;
        draggingPan = false;
        update();
    }
    return QWidget::eventFilter(obj, event);
}
