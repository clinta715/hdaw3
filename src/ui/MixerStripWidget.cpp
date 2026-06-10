#include "MixerStripWidget.h"
#include "Theme.h"
#include "../engine/LevelMeter.h"
#include <QPainter>
#include <QMouseEvent>
#include <QMenu>
#include <QAction>
#include <QInputDialog>
#include <cmath>

MixerStripWidget::MixerStripWidget(int idx, AudioEngine& ae, QWidget* parent)
    : QWidget(parent), trackIndex(idx), engine(ae)
{
    setFixedSize(stripWidth, stripHeight);
    setMinimumWidth(stripWidth);

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
    int h = height();

    // Background
    painter.fillRect(rect(), ThemeColors::bgPanel());

    // Color strip at top
    painter.fillRect(QRect(0, 0, w, 3), ThemeColors::accent());

    // Name
    painter.setPen(Qt::white);
    QFont f = painter.font();
    f.setPointSize(7);
    painter.setFont(f);
    painter.drawText(QRect(0, 5, w, 14), Qt::AlignCenter, name);

    // VU Meter
    int vuY = 22;
    int vuH = 50;
    int vuSpacing = 2;
    int vuW = (w - vuSpacing * 3) / 2;

    auto drawVUBar = [&](int x, float level) {
        float db = 20.0f * std::log10((std::max)(level, 0.0001f));
        float normalized = (db + 60.0f) / 60.0f;
        normalized = (std::max)(0.0f, (std::min)(1.0f, normalized));
        int barH = static_cast<int>(normalized * vuH);

        QColor color;
        if (db > -3.0f) color = ThemeColors::vuRed();
        else if (db > -12.0f) color = ThemeColors::vuYellow();
        else color = ThemeColors::vuGreen();

        QRect r(x, vuY + vuH - barH, vuW, barH);
        painter.fillRect(r, color);
    };

    painter.fillRect(QRect(vuSpacing, vuY, w - vuSpacing * 2, vuH), QColor(20, 20, 22));
    drawVUBar(vuSpacing, currentLeft);
    drawVUBar(vuSpacing * 2 + vuW, currentRight);

    // Mute / Solo buttons
    int btnY = vuY + vuH + 4;
    int btnSize = 12;
    int btnX = 4;

    auto drawBtn = [&](QRect rect, QColor onColor, bool active, const QString& label) {
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

    drawBtn(QRect(btnX, btnY, btnSize, btnSize), ThemeColors::danger(), muted, "M");
    drawBtn(QRect(btnX + btnSize + 2, btnY, btnSize, btnSize), ThemeColors::warning(), soloed, "S");

    // FX button
    fxBtnRect = QRect(btnX + 2 * (btnSize + 2), btnY, btnSize, btnSize);
    painter.setPen(QPen(ThemeColors::borderLight(), 1));
    painter.setBrush(ThemeColors::bgWidget());
    painter.drawRoundedRect(fxBtnRect, 2, 2);
    painter.setPen(ThemeColors::accentBright());
    QFont sf2 = painter.font();
    sf2.setPointSize(6);
    sf2.setBold(true);
    painter.setFont(sf2);
    painter.drawText(fxBtnRect, Qt::AlignCenter, "FX");

    // Volume fader
    int faderY = btnY + btnSize + 6;
    int faderH = w - 16;
    int faderX = (w - 8) / 2;

    // Draw groove
    painter.setPen(ThemeColors::border());
    painter.setBrush(ThemeColors::bgWidget());
    painter.drawRoundedRect(QRect(faderX, faderY, 8, faderH), 3, 3);

    // Draw handle (1.0 at the top, 0.0 at the bottom)
    float volPos = (1.0f - volume) * (faderH - 8);
    painter.setPen(Qt::NoPen);
    painter.setBrush(ThemeColors::accent());
    painter.drawRoundedRect(QRect(faderX - 2, faderY + static_cast<int>(volPos), 12, 8), 2, 2);

    // Volume db label
    int db = static_cast<int>(20.0 * std::log10((std::max)(volume, 0.001f)));
    painter.setPen(ThemeColors::textSecondary());
    f.setPointSize(6);
    painter.setFont(f);
    painter.drawText(QRect(0, faderY + faderH + 2, w, 12), Qt::AlignCenter, QString::number(db) + "dB");

    // Pan control
    int panY = faderY + faderH + 16;
    int panH = 10;
    int panMargin = 6;
    panRect = QRect(panMargin, panY, w - panMargin * 2, panH);

    painter.setPen(ThemeColors::border());
    painter.setBrush(ThemeColors::bgPanel());
    painter.drawRoundedRect(panRect, 2, 2);

    float panPos = (pan * 0.5f + 0.5f) * (panRect.width() - 6) + 3;
    painter.setPen(Qt::NoPen);
    painter.setBrush(ThemeColors::textSecondary());
    painter.drawEllipse(QPointF(panRect.x() + panPos, panRect.center().y()), 3, 3);

    painter.setPen(ThemeColors::textSecondary());
    painter.drawText(QRect(0, panY + panH + 1, w, 10), Qt::AlignCenter,
                     QString::number(static_cast<int>(pan * 100.0f)) + "%");
}

void MixerStripWidget::mousePressEvent(QMouseEvent* event)
{
    QPoint pos = event->pos();
    int w = width();

    int vuY = 22;
    int vuH = 50;
    int btnY = vuY + vuH + 4;
    int btnSize = 12;

    QRect muteRect(4, btnY, btnSize, btnSize);
    QRect soloRect(4 + btnSize + 2, btnY, btnSize, btnSize);

    int faderY = btnY + btnSize + 6;
    int faderH = w - 16;
    int faderX = (w - 8) / 2;
    QRect faderRect(faderX - 2, faderY, 12, faderH);

    if (fxBtnRect.contains(pos))
    {
        emit fxButtonClicked(trackIndex);
        return;
    }

    if (muteRect.contains(pos))
    {
        muted = !muted;
        emit muteToggled(trackIndex, muted);
        update();
    }
    else if (soloRect.contains(pos))
    {
        soloed = !soloed;
        emit soloToggled(trackIndex, soloed);
        update();
    }
    else if (faderRect.contains(pos))
    {
        draggingVol = true;
    }
    else if (panRect.contains(pos))
    {
        draggingPan = true;
    }
}

void MixerStripWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (draggingVol)
    {
        int w = width();
        int faderH = w - 16;
        int y = event->pos().y();
        int vuY = 22;
        int vuH = 50;
        int btnY = vuY + vuH + 4;
        int btnSize = 12;
        int faderY = btnY + btnSize + 6;
        float range = static_cast<float>(faderH);

        if (range > 0)
        {
            float newVol = 1.0f - (static_cast<float>(y - faderY) / range);
            newVol = (std::max)(0.0f, (std::min)(1.0f, newVol));
            volume = newVol;
            emit volumeChanged(trackIndex, newVol);
            update();
        }
    }
    else if (draggingPan)
    {
        int x = event->pos().x();
        float range = static_cast<float>(panRect.width() - 6);
        if (range > 0)
        {
            float newPan = (std::max)(-1.0f, (std::min)(1.0f,
                (static_cast<float>(x - panRect.x() - 3) / range) * 2.0f - 1.0f));
            pan = newPan;
            emit panChanged(trackIndex, newPan);
            update();
        }
    }
}

void MixerStripWidget::mouseReleaseEvent(QMouseEvent*)
{
    draggingVol = false;
    draggingPan = false;
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
