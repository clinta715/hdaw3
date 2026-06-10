#include "MixerWidget.h"
#include <QPainter>
#include <QScrollBar>
#include <QLabel>
#include <cmath>

MixerWidget::MixerWidget(AudioEngine& ae, QWidget* parent)
    : QWidget(parent), engine(ae)
{
    auto* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Scroll area for strips
    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setFrameStyle(QFrame::NoFrame);
    scrollArea->setStyleSheet("QScrollArea { background-color: #121214; }");

    stripContainer = new QWidget();
    stripLayout = new QHBoxLayout(stripContainer);
    stripLayout->setContentsMargins(2, 2, 2, 2);
    stripLayout->setSpacing(2);

    scrollArea->setWidget(stripContainer);
    mainLayout->addWidget(scrollArea, 1);

    // Master strip (non-scrollable, right side)
    auto* masterWidget = new QWidget(this);
    masterWidget->setFixedWidth(70);
    masterWidget->setStyleSheet("background-color: #1c1c1f;");
    auto* masterLayout = new QVBoxLayout(masterWidget);
    masterLayout->setContentsMargins(4, 4, 4, 4);

    auto* masterLabel = new QLabel("Master", masterWidget);
    masterLabel->setStyleSheet("color: #e4e4e7; font-size: 8pt; font-weight: bold;");
    masterLabel->setAlignment(Qt::AlignCenter);
    masterLayout->addWidget(masterLabel);

    auto* masterVU = new HDAW::VUMeter(engine.getMainProcessor()->getMasterMeter(), masterWidget);
    masterVU->setFixedHeight(80);
    masterLayout->addWidget(masterVU);

    mainLayout->addWidget(masterWidget, 0, Qt::AlignRight);

    rebuild();
}

MixerWidget::~MixerWidget() = default;

void MixerWidget::rebuild()
{
    // Clear existing strips
    QLayoutItem* item;
    while ((item = stripLayout->takeAt(0)) != nullptr)
    {
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }

    // Add strips for each track
    auto trackList = engine.getProjectModel().getTrackListTree();
    for (int i = 0; i < trackList.getNumChildren(); ++i)
    {
        auto* strip = new MixerStripWidget(i, engine, stripContainer);

        connect(strip, &MixerStripWidget::volumeChanged, this,
            [this](int trackIdx, float vol) {
                auto trackList = engine.getProjectModel().getTrackListTree();
                if (trackIdx < trackList.getNumChildren())
                {
                    trackList.getChild(trackIdx).setProperty(IDs::volume, static_cast<double>(vol), &engine.getProjectModel().getUndoManager());
                }
            });

        connect(strip, &MixerStripWidget::muteToggled, this,
            [this](int trackIdx, bool muted) {
                engine.setTrackMuted(trackIdx, muted);
            });

        connect(strip, &MixerStripWidget::soloToggled, this,
            [this](int trackIdx, bool soloed) {
                auto trackList = engine.getProjectModel().getTrackListTree();
                if (trackIdx < trackList.getNumChildren())
                    trackList.getChild(trackIdx).setProperty(IDs::isSoloed, soloed, &engine.getProjectModel().getUndoManager());
            });

        connect(strip, &MixerStripWidget::panChanged, this,
            [this](int trackIdx, float pan) {
                auto trackList = engine.getProjectModel().getTrackListTree();
                if (trackIdx < trackList.getNumChildren())
                {
                    trackList.getChild(trackIdx).setProperty(IDs::pan, static_cast<double>(pan), &engine.getProjectModel().getUndoManager());
                }
            });

        connect(strip, &MixerStripWidget::fxButtonClicked, this,
            &MixerWidget::fxButtonClicked);

        connect(strip, &MixerStripWidget::trackDeleted, this,
            [this]() { rebuild(); });

        stripLayout->addWidget(strip);
    }

    stripLayout->addStretch();
}


