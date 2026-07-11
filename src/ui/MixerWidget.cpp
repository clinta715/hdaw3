#include "MixerWidget.h"
#include "Theme.h"
#include <QPainter>
#include <QScrollBar>
#include <QLabel>
#include <cmath>

MixerWidget::MixerWidget(AudioEngine& ae, QWidget* parent)
    : QWidget(parent), engine(ae)
{
    projectCmds = &engine.getProjectCommands();
    audioGraphCmds = &engine.getAudioGraphCommands();
    readModel = &engine.getReadModel();
    auto* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Scroll area for strips
    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setFrameStyle(QFrame::NoFrame);
    scrollArea->setStyleSheet(QString("QScrollArea { background-color: %1; }").arg(ThemeColors::bgWindow().name()));

    stripContainer = new QWidget();
    stripLayout = new QHBoxLayout(stripContainer);
    stripLayout->setContentsMargins(2, 2, 2, 2);
    stripLayout->setSpacing(2);

    // Master strip (inside scroll area, right side, aligned with tracks)
    auto* masterWidget = new QWidget(stripContainer);
    masterWidget->setFixedWidth(70);
    masterWidget->setStyleSheet(QString("background-color: %1;").arg(ThemeColors::bgPanel().name()));
    auto* masterLayout = new QVBoxLayout(masterWidget);
    masterLayout->setContentsMargins(4, 4, 4, 4);

    auto* masterLabel = new QLabel("Master", masterWidget);
    masterLabel->setStyleSheet("color: #e4e4e7; font-size: 8pt; font-weight: bold;");
    masterLabel->setAlignment(Qt::AlignCenter);
    masterLayout->addWidget(masterLabel);

    // The master processor may not be initialized yet at construction time
    // (AudioEngine::initialize() runs after the UI is built). Use a static
    // fallback meter so VUMeter construction always succeeds; updateMasterMeter()
    // reassigns the real master meter once the engine is ready.
    static HDAW::LevelMeter fallbackMeter;
    auto* mainProc = engine.getMainProcessor();
    masterVU = new HDAW::VUMeter(
        mainProc ? mainProc->getMasterMeter() : fallbackMeter, masterWidget);
    masterVU->setFixedHeight(80);
    masterLayout->addWidget(masterVU);

    scrollArea->setWidget(stripContainer);
    mainLayout->addWidget(scrollArea, 1);

    rebuild();
}

MixerWidget::~MixerWidget() = default;

void MixerWidget::rebuild()
{
    // Clear existing strips (but keep master widget)
    QLayoutItem* item;
    while ((item = stripLayout->takeAt(0)) != nullptr)
    {
        if (item->widget() && item->widget() != masterVU->parentWidget())
            item->widget()->deleteLater();
        delete item;
    }

    // Add strips for each track
    int trackCount = readModel->getTrackCount();
    for (int i = 0; i < trackCount; ++i)
    {
        auto* strip = new MixerStripWidget(i, engine, stripContainer);

        connect(strip, &MixerStripWidget::volumeChanged, this,
            [this](int trackIdx, float vol) {
                int count = readModel->getTrackCount();
                if (trackIdx < count)
                    projectCmds->setTrackVolume(trackIdx, vol);
            });

        connect(strip, &MixerStripWidget::muteToggled, this,
            [this](int trackIdx, bool muted) {
                projectCmds->setTrackMuted(trackIdx, muted);
            });

        connect(strip, &MixerStripWidget::soloToggled, this,
            [this](int trackIdx, bool soloed) {
                int count = readModel->getTrackCount();
                if (trackIdx < count)
                    projectCmds->setTrackSoloed(trackIdx, soloed);
            });

        connect(strip, &MixerStripWidget::panChanged, this,
            [this](int trackIdx, float pan) {
                int count = readModel->getTrackCount();
                if (trackIdx < count)
                    projectCmds->setTrackPan(trackIdx, pan);
            });

        connect(strip, &MixerStripWidget::fxButtonClicked, this,
            &MixerWidget::fxButtonClicked);

        connect(strip, &MixerStripWidget::trackDeleted, this,
            [this]() { rebuild(); });

        stripLayout->addWidget(strip);
    }

    // Add stretch before master so it stays at the right end
    stripLayout->addStretch();

    // Re-add master widget at the end
    if (masterVU != nullptr && masterVU->parentWidget() != nullptr)
        stripLayout->addWidget(masterVU->parentWidget());
}

void MixerWidget::updateMasterMeter()
{
    if (masterVU != nullptr)
    {
        if (auto* mainProc = engine.getMainProcessor())
            masterVU->setMeter(&mainProc->getMasterMeter());
    }
}


