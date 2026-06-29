#include "FXSlotRow.h"
#include "../engine/AudioEngine.h"
#include "../engine/PluginManager.h"
#include <QHBoxLayout>
#include <QTimer>

FXSlotRow::FXSlotRow(juce::ValueTree tree, int index, int trackIdx, AudioEngine& ae, QWidget* parent)
    : QWidget(parent), slotTree(tree), slotIndex(index), trackIndex(trackIdx), engine(ae)
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 2, 4, 2);
    mainLayout->setSpacing(2);

    auto* topRow = new QWidget(this);
    auto* layout = new QHBoxLayout(topRow);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    // Slot number label
    auto* numLabel = new QLabel(QString("#%1").arg(index + 1), topRow);
    numLabel->setFixedWidth(24);
    layout->addWidget(numLabel);

    // FX type combobox
    typeCombo = new QComboBox(topRow);
    populateTypeCombo();

    juce::String currentType = slotTree.getProperty(IDs::fxType).toString();
    if (currentType == "plugin")
    {
        juce::String pluginID = slotTree.getProperty(IDs::pluginID).toString();
        int ci = typeCombo->findData(QString::fromUtf8(pluginID.toRawUTF8()));
        if (ci >= 0) typeCombo->setCurrentIndex(ci);
    }
    else
    {
        int ci = typeCombo->findText(QString::fromUtf8(currentType.toRawUTF8()),
                                     Qt::MatchFixedString);
        if (ci >= 0) typeCombo->setCurrentIndex(ci);
    }

    typeCombo->setFixedHeight(22);
    layout->addWidget(typeCombo, 1);

    connect(typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
        juce::String type = typeCombo->currentData().toString().toStdString();
        if (type.isEmpty())
            type = typeCombo->currentText().toLower().toStdString();
        onTypeChanged(type);
    });

    // Bypass toggle
    bypassed = slotTree.getProperty(IDs::bypassed);
    bypassBtn = new QPushButton(bypassed ? "Off" : "On", topRow);
    bypassBtn->setCheckable(true);
    bypassBtn->setChecked(!bypassed);
    bypassBtn->setFixedSize(32, 22);
    layout->addWidget(bypassBtn);

    connect(bypassBtn, &QPushButton::toggled, this, [this](bool checked) {
        bypassed = !checked;
        slotTree.setProperty(IDs::bypassed, bypassed, &engine.getProjectModel().getUndoManager());
        bypassBtn->setText(bypassed ? "Off" : "On");
        emit slotChanged();
    });

    // Move up
    auto* upBtn = new QPushButton("\xE2\x86\x91", topRow);
    upBtn->setFixedSize(20, 22);
    connect(upBtn, &QPushButton::clicked, this, [this]() { emit moveUpRequested(slotIndex); });
    layout->addWidget(upBtn);

    // Move down
    auto* downBtn = new QPushButton("\xE2\x86\x93", topRow);
    downBtn->setFixedSize(20, 22);
    connect(downBtn, &QPushButton::clicked, this, [this]() { emit moveDownRequested(slotIndex); });
    layout->addWidget(downBtn);

    // Remove
    auto* removeBtn = new QPushButton("X", topRow);
    removeBtn->setFixedSize(20, 22);
    connect(removeBtn, &QPushButton::clicked, this, [this]() { emit removeRequested(slotIndex); });
    layout->addWidget(removeBtn);

    // Edit button (for plugins) — opens native GUI window
    editBtn = new QPushButton("Edit", topRow);
    editBtn->setFixedSize(32, 22);
    editBtn->setVisible(currentType == "plugin");
    connect(editBtn, &QPushButton::clicked, this, [this]() { emit editRequested(slotIndex); });
    layout->addWidget(editBtn);

    // Params button (for plugins) — shows/hides parameter sliders
    paramsBtn = new QPushButton("Params", topRow);
    paramsBtn->setFixedSize(42, 22);
    paramsBtn->setVisible(currentType == "plugin");
    paramsBtn->setCheckable(true);
    connect(paramsBtn, &QPushButton::toggled, this, [this](bool checked) {
        paramContainer->setVisible(checked);
    });
    layout->addWidget(paramsBtn);

    mainLayout->addWidget(topRow);

    // Parameter container (shown for plugins when Params is toggled)
    paramContainer = new QWidget(this);
    paramContainer->setVisible(false);
    mainLayout->addWidget(paramContainer);

    // Lock-free bridge: audio-thread listener pushes (idx,value); UI timer drains.
    paramRing = std::make_unique<ParamUpdateRing>();
    pollTimer = new QTimer(this);
    pollTimer->setTimerType(Qt::PreciseTimer);
    connect(pollTimer, &QTimer::timeout, this, &FXSlotRow::pollParamUpdates);

    if (currentType == "plugin")
        rebuildParamUI();
}

FXSlotRow::~FXSlotRow()
{
    if (pollTimer != nullptr)
        pollTimer->stop();
    if (registeredInstance && paramListener)
        registeredInstance->removeListener(paramListener.get());
}

void FXSlotRow::populateTypeCombo()
{
    typeCombo->clear();
    typeCombo->addItem("EQ");
    typeCombo->addItem("Compressor");
    typeCombo->addItem("Reverb");
    typeCombo->addItem("Delay");

    auto& pluginManager = engine.getPluginManager();
    const auto& plugins = pluginManager.getPlugins();
    if (!plugins.empty())
    {
        typeCombo->insertSeparator(typeCombo->count());
        for (const auto& desc : plugins)
        {
            if (pluginManager.isBlacklisted(desc.fileOrIdentifier))
                continue;

            QString label = QString("[%1] %2")
                .arg(QString::fromUtf8(desc.pluginFormatName.toRawUTF8()))
                .arg(QString::fromUtf8(desc.name.toRawUTF8()));
            QString pluginID = QString::fromUtf8(desc.fileOrIdentifier.toRawUTF8());
            typeCombo->addItem(label, QVariant(pluginID));
        }
    }
}

void FXSlotRow::onTypeChanged(const juce::String& type)
{
    // Clear existing param UI
    paramSliders.clear();

    if (type == "eq" || type == "compressor" || type == "reverb" || type == "delay")
    {
        slotTree.setProperty(IDs::fxType, type, &engine.getProjectModel().getUndoManager());
        slotTree.removeProperty(IDs::pluginID, &engine.getProjectModel().getUndoManager());
        slotTree.removeProperty(IDs::pluginFormat, &engine.getProjectModel().getUndoManager());
        slotTree.removeProperty(IDs::pluginPath, &engine.getProjectModel().getUndoManager());
        paramContainer->setVisible(false);
        editBtn->setVisible(false);
        paramsBtn->setVisible(false);
    }
    else
    {
        // It's a plugin — find the description
        auto& pluginManager = engine.getPluginManager();
        const auto& plugins = pluginManager.getPlugins();
        bool found = false;
        for (const auto& desc : plugins)
        {
            if (desc.fileOrIdentifier == type)
            {
                slotTree.setProperty(IDs::fxType, "plugin", &engine.getProjectModel().getUndoManager());
                slotTree.setProperty(IDs::pluginID, desc.fileOrIdentifier, &engine.getProjectModel().getUndoManager());
                slotTree.setProperty(IDs::pluginFormat, desc.pluginFormatName, &engine.getProjectModel().getUndoManager());
                slotTree.setProperty(IDs::pluginPath, desc.fileOrIdentifier, &engine.getProjectModel().getUndoManager());
                found = true;
                break;
            }
        }
        if (found)
        {
            rebuildParamUI();
            paramContainer->setVisible(false);
            editBtn->setVisible(true);
            paramsBtn->setVisible(true);
        }
        else
        {
            paramContainer->setVisible(false);
            editBtn->setVisible(false);
            paramsBtn->setVisible(false);
        }
    }

    emit slotChanged();
}

void FXSlotRow::rebuildParamUI()
{
    // Stop polling and tear down the old listener before rebuilding.
    if (pollTimer != nullptr)
        pollTimer->stop();
    if (registeredInstance && paramListener)
        registeredInstance->removeListener(paramListener.get());
    registeredInstance = nullptr;
    paramListener.reset();
    if (paramRing != nullptr)
        paramRing->clear();

    // Clear existing param widgets
    if (auto* layout = paramContainer->layout())
    {
        QLayoutItem* child;
        while ((child = layout->takeAt(0)) != nullptr)
        {
            delete child->widget();
            delete child;
        }
        delete layout;
    }
    paramSliders.clear();

    auto* proc = engine.getMainProcessor();
    auto* track = proc->getTrack(trackIndex);
    if (track == nullptr) return;

    // Find the matching slot in the track's FX chain
    auto& fxChain = track->getFXChain();
    for (const auto& slot : fxChain)
    {
        if (slot && slot->isPlugin() && slot->getPluginID() == slotTree.getProperty(IDs::pluginID).toString())
        {
            auto* instance = slot->getPluginInstance();
            if (instance == nullptr) return;

            auto params = instance->getParameters();
            auto* paramLayout = new QVBoxLayout(paramContainer);
            paramLayout->setContentsMargins(20, 2, 4, 2);
            paramLayout->setSpacing(2);

            for (int i = 0; i < params.size(); ++i)
            {
                auto* p = params[i];
                auto* row = new QWidget(paramContainer);
                auto* rowLayout = new QHBoxLayout(row);
                rowLayout->setContentsMargins(0, 0, 0, 0);
                rowLayout->setSpacing(4);

                juce::String pName = p->getName(128);
                auto* nameLabel = new QLabel(QString::fromUtf8(pName.toRawUTF8()), row);
                nameLabel->setFixedWidth(80);
                rowLayout->addWidget(nameLabel);

                auto* slider = new QSlider(Qt::Horizontal, row);
                slider->setRange(0, 1000);
                slider->setValue(static_cast<int>(p->getValue() * 1000.0f));
                slider->setFixedHeight(16);
                rowLayout->addWidget(slider, 1);

                juce::String pLabel = p->getLabel();
                auto* valLabel = new QLabel(row);
                QString displayText = QString::fromUtf8(p->getText(p->getValue(), 128).toRawUTF8());
                if (pLabel.isNotEmpty())
                    displayText += " " + QString::fromUtf8(pLabel.toRawUTF8());
                valLabel->setText(displayText);
                valLabel->setFixedWidth(80);
                rowLayout->addWidget(valLabel);

                paramLayout->addWidget(row);

                ParamSlider ps;
                ps.slider = slider;
                ps.valueLabel = valLabel;
                ps.paramIdx = i;
                paramSliders.push_back(ps);

                int idx = i;
                connect(slider, &QSlider::valueChanged, this,
                    [this, idx](int val) {
                        auto* proc = engine.getMainProcessor();
                        auto* track = proc->getTrack(trackIndex);
                        if (track == nullptr) return;
                        auto& fxChain = track->getFXChain();
                        juce::AudioPluginInstance* liveInstance = nullptr;
                        for (const auto& slot : fxChain)
                        {
                            if (slot && slot->isPlugin() &&
                                slot->getPluginID() == slotTree.getProperty(IDs::pluginID).toString())
                            {
                                liveInstance = slot->getPluginInstance();
                                break;
                            }
                        }
                        if (liveInstance == nullptr) return;
                        auto params = liveInstance->getParameters();
                        if (idx < 0 || idx >= params.size()) return;
                        auto* p = params[idx];
                        float normalized = val / 1000.0f;
                        p->beginChangeGesture();
                        p->setValueNotifyingHost(normalized);
                        p->endChangeGesture();
                        for (const auto& ps : paramSliders)
                        {
                            if (ps.paramIdx == idx)
                            {
                                juce::String label = p->getLabel();
                                QString txt = QString::fromUtf8(p->getText(normalized, 128).toRawUTF8());
                                if (label.isNotEmpty())
                                    txt += " " + QString::fromUtf8(label.toRawUTF8());
                                ps.valueLabel->setText(txt);
                            }
                        }
                    });
            }

            // Register AudioProcessorListener. The callback may fire on the audio
            // thread (see juce_AudioProcessorListener.h), so it MUST NOT allocate
            // or lock — it only pushes into the lock-free ring. A UI timer drains it.
            registeredInstance = instance;
            paramListener = std::make_unique<ParamListener>();
            paramListener->onChanged = [this](int idx, float val) {
                if (paramRing) paramRing->push(idx, val);
            };
            instance->addListener(paramListener.get());
            if (pollTimer != nullptr)
                pollTimer->start(33); // ~30 Hz UI refresh

            break;
        }
    }
}

void FXSlotRow::pollParamUpdates()
{
    if (paramRing == nullptr || paramSliders.empty())
        return;

    // Resolve the live plugin instance once per tick for text formatting.
    juce::AudioPluginInstance* liveInstance = nullptr;
    auto* proc = engine.getMainProcessor();
    if (auto* track = (proc != nullptr) ? proc->getTrack(trackIndex) : nullptr)
    {
        auto& fxChain = track->getFXChain();
        for (const auto& slot : fxChain)
        {
            if (slot && slot->isPlugin() &&
                slot->getPluginID() == slotTree.getProperty(IDs::pluginID).toString())
            {
                liveInstance = slot->getPluginInstance();
                break;
            }
        }
    }

    ParamUpdateRing::Entry e;
    while (paramRing->pop(e))
    {
        for (auto& ps : paramSliders)
        {
            if (ps.paramIdx != e.idx)
                continue;

            ps.slider->blockSignals(true);
            ps.slider->setValue(static_cast<int>(e.value * 1000.0f));
            ps.slider->blockSignals(false);

            if (liveInstance != nullptr && e.idx < liveInstance->getParameters().size())
            {
                auto* p = liveInstance->getParameters()[e.idx];
                juce::String label = p->getLabel();
                QString txt = QString::fromUtf8(p->getText(e.value, 128).toRawUTF8());
                if (label.isNotEmpty())
                    txt += " " + QString::fromUtf8(label.toRawUTF8());
                ps.valueLabel->setText(txt);
            }
        }
    }
}
