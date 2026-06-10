#include "FXChainWidget.h"
#include <QHBoxLayout>

FXChainWidget::FXChainWidget(AudioEngine& ae, QWidget* parent)
    : QWidget(parent), engine(ae)
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Header
    auto* header = new QWidget(this);
    header->setFixedHeight(28);
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(8, 2, 8, 2);

    headerLabel = new QLabel("FX Chain", header);
    headerLayout->addWidget(headerLabel);
    headerLayout->addStretch();

    mainLayout->addWidget(header);

    // Scroll area for slots
    scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameStyle(QFrame::NoFrame);

    slotContainer = new QWidget();
    slotLayout = new QVBoxLayout(slotContainer);
    slotLayout->setContentsMargins(8, 8, 8, 8);
    slotLayout->setSpacing(4);
    slotLayout->addStretch();

    scrollArea->setWidget(slotContainer);
    mainLayout->addWidget(scrollArea, 1);

    // Add FX button
    auto* addBtn = new QPushButton("+ Add FX", this);
    addBtn->setFixedHeight(28);
    connect(addBtn, &QPushButton::clicked, this, [this]() { addFXSlot(); });
    mainLayout->addWidget(addBtn);

    clear();
}

FXChainWidget::~FXChainWidget() = default;

void FXChainWidget::loadTrack(int trackIndex)
{
    currentTrack = trackIndex;
    auto trackList = engine.getProjectModel().getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren())
    {
        clear();
        return;
    }

    auto trackTree = trackList.getChild(trackIndex);
    QString name = QString::fromUtf8(trackTree.getProperty(IDs::name).toString().toRawUTF8());
    headerLabel->setText(QString("FX Chain - %1").arg(name));

    rebuildUI();
}

void FXChainWidget::clear()
{
    currentTrack = -1;
    headerLabel->setText("FX Chain - No track selected");

    // Clear existing slot rows
    QLayoutItem* item;
    while ((item = slotLayout->takeAt(0)) != nullptr)
    {
        if (item->widget())
        {
            item->widget()->deleteLater();
        }
        delete item;
    }
    slotLayout->addStretch();
}

void FXChainWidget::rebuildUI()
{
    // Clear existing
    QLayoutItem* item;
    while ((item = slotLayout->takeAt(0)) != nullptr)
    {
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }

    if (currentTrack < 0)
    {
        slotLayout->addStretch();
        return;
    }

    auto trackList = engine.getProjectModel().getTrackListTree();
    auto trackTree = trackList.getChild(currentTrack);
    auto fxChain = trackTree.getChildWithName(IDs::FX_CHAIN);

    if (fxChain.isValid())
    {
        for (int i = 0; i < fxChain.getNumChildren(); ++i)
        {
            auto slotTree = fxChain.getChild(i);
            auto* row = new FXSlotRow(slotTree, i, currentTrack, engine, slotContainer);
            int index = i;

            connect(row, &FXSlotRow::removeRequested, this, [this, index](int) {
                auto trackList = engine.getProjectModel().getTrackListTree();
                if (currentTrack < 0 || currentTrack >= trackList.getNumChildren()) return;
                auto c = trackList.getChild(currentTrack).getChildWithName(IDs::FX_CHAIN);
                if (c.isValid() && index < c.getNumChildren())
                {
                    c.removeChild(index, &engine.getProjectModel().getUndoManager());
                    rebuildUI();
                    emit chainChanged();
                }
            });

            connect(row, &FXSlotRow::moveUpRequested, this, [this, index](int) {
                auto trackList = engine.getProjectModel().getTrackListTree();
                if (currentTrack < 0 || currentTrack >= trackList.getNumChildren()) return;
                auto c = trackList.getChild(currentTrack).getChildWithName(IDs::FX_CHAIN);
                if (c.isValid() && index > 0)
                {
                    c.moveChild(index, index - 1, &engine.getProjectModel().getUndoManager());
                    rebuildUI();
                    emit chainChanged();
                }
            });

            connect(row, &FXSlotRow::moveDownRequested, this, [this, index](int) {
                auto trackList = engine.getProjectModel().getTrackListTree();
                if (currentTrack < 0 || currentTrack >= trackList.getNumChildren()) return;
                auto c = trackList.getChild(currentTrack).getChildWithName(IDs::FX_CHAIN);
                if (c.isValid() && index < c.getNumChildren() - 1)
                {
                    c.moveChild(index, index + 1, &engine.getProjectModel().getUndoManager());
                    rebuildUI();
                    emit chainChanged();
                }
            });

            connect(row, &FXSlotRow::slotChanged, this, [this]() {
                emit chainChanged();
            });

            connect(row, &FXSlotRow::editRequested, this, [this, index](int) {
                engine.getMainProcessor()->toggleFXEditor(currentTrack, index);
            });

            slotLayout->addWidget(row);
        }
    }

    slotLayout->addStretch();
}

void FXChainWidget::addFXSlot(const juce::String& type)
{
    if (currentTrack < 0) return;

    auto trackList = engine.getProjectModel().getTrackListTree();
    auto trackTree = trackList.getChild(currentTrack);
    auto fxChain = trackTree.getChildWithName(IDs::FX_CHAIN);

    if (!fxChain.isValid())
    {
        fxChain = juce::ValueTree(IDs::FX_CHAIN);
        trackTree.addChild(fxChain, -1, &engine.getProjectModel().getUndoManager());
    }

    juce::ValueTree slot(IDs::FX_SLOT);
    slot.setProperty(IDs::fxType, type, &engine.getProjectModel().getUndoManager());
    slot.setProperty(IDs::bypassed, false, &engine.getProjectModel().getUndoManager());
    fxChain.addChild(slot, -1, &engine.getProjectModel().getUndoManager());

    rebuildUI();
    emit chainChanged();
}
