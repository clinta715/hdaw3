#include "FXSlotRow.h"
#include "../engine/AudioEngine.h"
#include "../engine/PluginManager.h"
#include <QHBoxLayout>

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

    // Edit button (for plugins)
    editBtn = new QPushButton("Edit", topRow);
    editBtn->setFixedSize(32, 22);
    editBtn->setVisible(currentType == "plugin");
    connect(editBtn, &QPushButton::clicked, this, [this]() { emit editRequested(slotIndex); });
    layout->addWidget(editBtn);

    mainLayout->addWidget(topRow);

    // Parameter container (shown for plugins)
    paramContainer = new QWidget(this);
    paramContainer->setVisible(false);
    mainLayout->addWidget(paramContainer);

    if (currentType == "plugin")
        rebuildParamUI();
}

FXSlotRow::~FXSlotRow() = default;

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
    }
    else
    {
        // It's a plugin — find the description
        auto& pluginManager = engine.getPluginManager();
        const auto& plugins = pluginManager.getPlugins();
        for (const auto& desc : plugins)
        {
            if (desc.fileOrIdentifier == type)
            {
                slotTree.setProperty(IDs::fxType, "plugin", &engine.getProjectModel().getUndoManager());
                slotTree.setProperty(IDs::pluginID, desc.fileOrIdentifier, &engine.getProjectModel().getUndoManager());
                slotTree.setProperty(IDs::pluginFormat, desc.pluginFormatName, &engine.getProjectModel().getUndoManager());
                slotTree.setProperty(IDs::pluginPath, desc.fileOrIdentifier, &engine.getProjectModel().getUndoManager());
                break;
            }
        }
        rebuildParamUI();
        paramContainer->setVisible(true);
        editBtn->setVisible(true);
    }

    emit slotChanged();
}

void FXSlotRow::rebuildParamUI()
{
    // Clear existing param widgets
    QLayoutItem* child;
    while ((child = paramContainer->layout()->takeAt(0)) != nullptr)
    {
        delete child->widget();
        delete child;
    }
    paramSliders.clear();

    // Use the stored trackIndex (passed at construction)
    auto* proc = engine.getMainProcessor();
    auto* track = proc->getTrack(trackIndex);
    if (track == nullptr) return;

    // Find the matching slot in the track's FX chain
    auto& fxChain = track->getFXChain();
    for (auto& slot : fxChain)
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

                auto* nameLabel = new QLabel(QString::fromUtf8(p->getName(128).toRawUTF8()), row);
                nameLabel->setFixedWidth(80);
                rowLayout->addWidget(nameLabel);

                auto* slider = new QSlider(Qt::Horizontal, row);
                slider->setRange(0, 1000);
                slider->setValue(static_cast<int>(p->getValue() * 1000.0f));
                slider->setFixedHeight(16);
                rowLayout->addWidget(slider, 1);

                auto* valLabel = new QLabel(QString::number(p->getValue(), 'f', 2), row);
                valLabel->setFixedWidth(36);
                rowLayout->addWidget(valLabel);

                paramLayout->addWidget(row);

                ParamSlider ps;
                ps.slider = slider;
                ps.valueLabel = valLabel;
                ps.paramIdx = i;
                paramSliders.push_back(ps);

                int idx = i;
                connect(slider, &QSlider::valueChanged, this,
                    [this, idx, instance, p](int val) {
                        float normalized = val / 1000.0f;
                        p->beginChangeGesture();
                        p->setValueNotifyingHost(normalized);
                        p->endChangeGesture();
                        for (auto& ps : paramSliders)
                        {
                            if (ps.paramIdx == idx)
                                ps.valueLabel->setText(QString::number(normalized, 'f', 2));
                        }
                    });
            }
            break;
        }
    }
}
