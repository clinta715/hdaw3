#include "FXChainWidget.h"
#include "../engine/AudioEngine.h"
#include "../model/ProjectModel.h"
#include <QHBoxLayout>
#include <QMenu>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <algorithm>

FXChainWidget::FXChainWidget(AudioEngine& ae, QWidget* parent)
    : QWidget(parent), engine(ae)
{
    projectCmds = &engine.getProjectCommands();
    transportCmds = &engine.getTransportCommands();
    audioGraphCmds = &engine.getAudioGraphCommands();
    readModel = &engine.getReadModel();
    pluginService = &engine.getPluginService();
    paramService = &engine.getPluginParamService();
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

    // Enable drag-and-drop reordering on the slot container. The drop
    // position is determined by the drop Y coordinate, mapped to the
    // nearest slot boundary. FXChainWidget intercepts the events via
    // an event filter so we don't have to subclass QWidget.
    slotContainer->setAcceptDrops(true);
    slotContainer->installEventFilter(this);

    // Add FX button
    auto* addBtn = new QPushButton("+ Add FX", this);
    addBtn->setFixedHeight(28);
    connect(addBtn, &QPushButton::clicked, this, [this, addBtn]() {
        if (currentTrack < 0) return;

        QMenu menu;
        menu.addAction("EQ", this, [this]() { addFXSlot("eq"); });
        menu.addAction("Compressor", this, [this]() { addFXSlot("compressor"); });
        menu.addAction("Reverb", this, [this]() { addFXSlot("reverb"); });
        menu.addAction("Delay", this, [this]() { addFXSlot("delay"); });

        auto plugInfos = pluginService->getPlugins();
        if (!plugInfos.empty())
        {
            std::vector<const PluginInfo*> instruments, effects;
            for (const auto& info : plugInfos)
            {
                if (pluginService->isBlacklisted(info.fileOrIdentifier))
                    continue;
                if (info.isInstrument)
                    instruments.push_back(&info);
                else
                    effects.push_back(&info);
            }

            if (!instruments.empty())
            {
                menu.addSeparator();
                menu.addSection("Instruments");
                for (const auto* info : instruments)
                {
                    QString label = QString("[%1] %2")
                        .arg(QString::fromStdString(info->format))
                        .arg(QString::fromStdString(info->name));
                    QString pid = QString::fromStdString(info->fileOrIdentifier);
                    menu.addAction(label, this, [this, pid]() { addPluginSlot(pid); });
                }
            }

            if (!effects.empty())
            {
                menu.addSeparator();
                menu.addSection("Effects");
                for (const auto* info : effects)
                {
                    QString label = QString("[%1] %2")
                        .arg(QString::fromStdString(info->format))
                        .arg(QString::fromStdString(info->name));
                    QString pid = QString::fromStdString(info->fileOrIdentifier);
                    menu.addAction(label, this, [this, pid]() { addPluginSlot(pid); });
                }
            }
        }

        menu.exec(addBtn->mapToGlobal(QPoint(0, addBtn->height())));
    });
    mainLayout->addWidget(addBtn);

    engine.getProjectModel().getTree().addListener(this);

    clear();
}

FXChainWidget::~FXChainWidget()
{
    engine.getProjectModel().getTree().removeListener(this);
}

void FXChainWidget::loadTrack(int trackIndex)
{
    currentTrack = trackIndex;
    if (trackIndex < 0 || trackIndex >= readModel->getTrackCount())
    {
        clear();
        return;
    }

    auto trackList = engine.getProjectModel().getTrackListTree();
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
        if (auto* w = item->widget())
        {
            if (auto* row = dynamic_cast<FXSlotRow*>(w))
                row->cleanup();
            w->deleteLater();
        }
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
            auto* row = new FXSlotRow(slotTree, i, currentTrack, paramService, pluginService, projectCmds, slotContainer);
            int index = i;

            connect(row, &FXSlotRow::removeRequested, this, [this, index](int) {
                if (currentTrack < 0 || currentTrack >= readModel->getTrackCount()) return;
                projectCmds->removeFxSlot(currentTrack, index);
                rebuildUI();
                emit chainChanged();
            });

            connect(row, &FXSlotRow::moveUpRequested, this, [this, index](int) {
                if (currentTrack < 0 || currentTrack >= readModel->getTrackCount()) return;
                if (index > 0)
                {
                    projectCmds->reorderFxSlots(currentTrack, index, index - 1);
                    rebuildUI();
                    emit chainChanged();
                }
            });

            connect(row, &FXSlotRow::moveDownRequested, this, [this, index](int) {
                if (currentTrack < 0 || currentTrack >= readModel->getTrackCount()) return;
                auto trackList = engine.getProjectModel().getTrackListTree();
                auto c = trackList.getChild(currentTrack).getChildWithName(IDs::FX_CHAIN);
                if (c.isValid() && index < c.getNumChildren() - 1)
                {
                    projectCmds->reorderFxSlots(currentTrack, index, index + 1);
                    rebuildUI();
                    emit chainChanged();
                }
            });

            connect(row, &FXSlotRow::slotChanged, this, [this]() {
                emit chainChanged();
            });

            connect(row, &FXSlotRow::editRequested, this, [this, index](int) {
                audioGraphCmds->toggleFXEditor(currentTrack, index);
            });

            slotLayout->addWidget(row);
        }
    }

    slotLayout->addStretch();
}

void FXChainWidget::addFXSlot(const juce::String& type)
{
    if (currentTrack < 0) return;

    int typeInt = (type == "eq") ? 0 : (type == "compressor") ? 1
                  : (type == "reverb") ? 2 : (type == "delay") ? 3 : 4;
    projectCmds->addFxSlot(currentTrack, typeInt, -1, "");

    rebuildUI();
    emit chainChanged();
}

void FXChainWidget::addPluginSlot(const QString& pluginID)
{
    if (currentTrack < 0) return;

    projectCmds->addFxSlot(currentTrack, "plugin", -1, pluginID.toStdString());

    rebuildUI();
    emit chainChanged();
}

int FXChainWidget::indexAtDropY(int y) const
{
    // Walk the layout's child widgets in order, accumulating heights.
    // The drop index is the slot whose top-to-midpoint the y crosses.
    if (slotLayout == nullptr) return -1;
    int cumulative = 0;
    int count = slotLayout->count();
    int result = 0;
    for (int i = 0; i < count; ++i)
    {
        QLayoutItem* item = slotLayout->itemAt(i);
        QWidget* w = item ? item->widget() : nullptr;
        if (w == nullptr) continue; // skip the trailing stretch
        int h = w->height();
        if (h == 0) h = 60; // not laid out yet — assume ~60px
        int mid = cumulative + h / 2;
        if (y < mid)
            return i;
        cumulative += h + slotLayout->spacing();
        result = i + 1;
    }
    return result;
}

bool FXChainWidget::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == slotContainer)
    {
        if (event->type() == QEvent::DragEnter)
        {
            static const QString mimeType = "application/x-hdaw-fxslot";
            auto* e = static_cast<QDragEnterEvent*>(event);
            if (e->mimeData()->hasFormat(mimeType))
                e->acceptProposedAction();
        }
        else if (event->type() == QEvent::DragMove)
        {
            auto* e = static_cast<QDragMoveEvent*>(event);
            e->acceptProposedAction();
        }
        else if (event->type() == QEvent::Drop)
        {
            static const QString mimeType = "application/x-hdaw-fxslot";
            auto* e = static_cast<QDropEvent*>(event);
            if (!e->mimeData()->hasFormat(mimeType))
            {
                e->ignore();
                return QWidget::eventFilter(obj, event);
            }
            int fromIndex = e->mimeData()->data(mimeType).toInt();
            int toIndex = indexAtDropY(e->pos().y());
            if (fromIndex >= 0 && toIndex >= 0 && fromIndex != toIndex)
            {
                if (currentTrack >= 0 && currentTrack < readModel->getTrackCount())
                {
                    if (fromIndex < toIndex)
                        toIndex--;
                    projectCmds->reorderFxSlots(currentTrack, fromIndex, toIndex);
                    rebuildUI();
                    emit chainChanged();
                }
            }
            e->acceptProposedAction();
        }
    }
    return QWidget::eventFilter(obj, event);
}

bool FXChainWidget::isCurrentTrackFxChain(const juce::ValueTree& tree) const
{
    if (currentTrack < 0)
        return false;

    if (tree.hasType(IDs::FX_CHAIN))
    {
        auto trackTree = tree.getParent();
        if (!trackTree.isValid() || !trackTree.hasType(IDs::TRACK))
            return false;
        auto trackList = engine.getProjectModel().getTrackListTree();
        return trackList.indexOf(trackTree) == currentTrack;
    }

    if (tree.hasType(IDs::FX_SLOT))
    {
        auto fxChain = tree.getParent();
        if (!fxChain.isValid() || !fxChain.hasType(IDs::FX_CHAIN))
            return false;
        auto trackTree = fxChain.getParent();
        if (!trackTree.isValid() || !trackTree.hasType(IDs::TRACK))
            return false;
        auto trackList = engine.getProjectModel().getTrackListTree();
        return trackList.indexOf(trackTree) == currentTrack;
    }

    return false;
}

void FXChainWidget::valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier& property)
{
    if (property == IDs::pluginState)
        return;

    if (isCurrentTrackFxChain(tree))
        rebuildUI();
}

void FXChainWidget::valueTreeChildAdded(juce::ValueTree& parentTree, juce::ValueTree& childWhichHasBeenAdded)
{
    if (childWhichHasBeenAdded.hasType(IDs::FX_SLOT) && isCurrentTrackFxChain(parentTree))
        rebuildUI();
    else if (childWhichHasBeenAdded.hasType(IDs::FX_CHAIN) && isCurrentTrackFxChain(childWhichHasBeenAdded))
        rebuildUI();
}

void FXChainWidget::valueTreeChildRemoved(juce::ValueTree& parentTree, juce::ValueTree& childWhichHasBeenRemoved, int)
{
    if (childWhichHasBeenRemoved.hasType(IDs::FX_SLOT) && isCurrentTrackFxChain(parentTree))
        rebuildUI();
    else if (childWhichHasBeenRemoved.hasType(IDs::FX_CHAIN) && isCurrentTrackFxChain(childWhichHasBeenRemoved))
        rebuildUI();
}

void FXChainWidget::valueTreeChildOrderChanged(juce::ValueTree& parentTree, int, int)
{
    if (parentTree.hasType(IDs::FX_CHAIN) && isCurrentTrackFxChain(parentTree))
        rebuildUI();
}
