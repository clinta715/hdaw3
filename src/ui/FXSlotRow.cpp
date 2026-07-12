#include "FXSlotRow.h"
#include "../model/ProjectModel.h"
#include <QHBoxLayout>
#include <QTimer>
#include <QMouseEvent>
#include <QDrag>
#include <QMimeData>
#include <QApplication>

FXSlotRow::FXSlotRow(juce::ValueTree tree, int index, int trackIdx,
                     PluginParamService* paramSvc, PluginService* plugSvc,
                     ProjectCommands* cmds, QWidget* parent)
    : QWidget(parent), slotTree(tree), slotIndex(index), trackIndex(trackIdx),
      paramService(paramSvc), pluginService(plugSvc), projectCmds(cmds)
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 2, 4, 2);
    mainLayout->setSpacing(2);

    filterEdit = new QLineEdit(this);
    filterEdit->setPlaceholderText("Search plugins...");
    filterEdit->setClearButtonEnabled(true);
    filterEdit->setFixedHeight(20);
    filterEdit->setStyleSheet("QLineEdit { font-size: 10px; padding: 1px 4px; }");
    mainLayout->addWidget(filterEdit);

    auto* topRow = new QWidget(this);
    auto* layout = new QHBoxLayout(topRow);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    auto* numLabel = new QLabel(QString("#%1").arg(index + 1), topRow);
    numLabel->setFixedWidth(24);
    layout->addWidget(numLabel);

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

    connect(filterEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        juce::String currentType = slotTree.getProperty(IDs::fxType).toString();
        populateTypeCombo(text);
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
    });

    connect(typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
        juce::String type = typeCombo->currentData().toString().toStdString();
        if (type.isEmpty())
            type = typeCombo->currentText().toLower().toStdString();
        onTypeChanged(type);
    });

    bypassed = slotTree.getProperty(IDs::bypassed);
    bypassBtn = new QPushButton(bypassed ? "Off" : "On", topRow);
    bypassBtn->setCheckable(true);
    bypassBtn->setChecked(!bypassed);
    bypassBtn->setFixedSize(32, 22);
    layout->addWidget(bypassBtn);

    connect(bypassBtn, &QPushButton::toggled, this, [this](bool checked) {
        bypassed = !checked;
        projectCmds->setFxSlotBypassed(trackIndex, slotIndex, bypassed);
        bypassBtn->setText(bypassed ? "Off" : "On");
        emit slotChanged();
    });

    auto* upBtn = new QPushButton("\xE2\x86\x91", topRow);
    upBtn->setFixedSize(20, 22);
    connect(upBtn, &QPushButton::clicked, this, [this]() { emit moveUpRequested(slotIndex); });
    layout->addWidget(upBtn);

    auto* downBtn = new QPushButton("\xE2\x86\x93", topRow);
    downBtn->setFixedSize(20, 22);
    connect(downBtn, &QPushButton::clicked, this, [this]() { emit moveDownRequested(slotIndex); });
    layout->addWidget(downBtn);

    auto* dragHandle = new QPushButton("\xE2\x96\xB6", topRow);
    dragHandle->setFixedSize(20, 22);
    dragHandle->setToolTip("Drag to reorder");
    dragHandle->setCursor(Qt::SizeVerCursor);
    dragHandle->installEventFilter(this);
    dragHandle->setProperty("dragHandleSlotIndex", slotIndex);
    layout->addWidget(dragHandle);

    auto* removeBtn = new QPushButton("X", topRow);
    removeBtn->setFixedSize(20, 22);
    connect(removeBtn, &QPushButton::clicked, this, [this]() { emit removeRequested(slotIndex); });
    layout->addWidget(removeBtn);

    editBtn = new QPushButton("Edit", topRow);
    editBtn->setFixedSize(32, 22);
    editBtn->setVisible(currentType == "plugin");
    connect(editBtn, &QPushButton::clicked, this, [this]() { emit editRequested(slotIndex); });
    layout->addWidget(editBtn);

    paramsBtn = new QPushButton("Params", topRow);
    paramsBtn->setFixedSize(42, 22);
    paramsBtn->setVisible(currentType == "plugin");
    paramsBtn->setCheckable(true);
    connect(paramsBtn, &QPushButton::toggled, this, [this](bool checked) {
        paramContainer->setVisible(checked);
    });
    layout->addWidget(paramsBtn);

    mainLayout->addWidget(topRow);

    paramContainer = new QWidget(this);
    paramContainer->setVisible(false);
    mainLayout->addWidget(paramContainer);

    paramRing = std::make_unique<ParamUpdateRing>();
    pollTimer = new QTimer(this);
    pollTimer->setTimerType(Qt::PreciseTimer);
    connect(pollTimer, &QTimer::timeout, this, &FXSlotRow::pollParamUpdates);

    if (currentType == "plugin")
        rebuildParamUI();
}

void FXSlotRow::cleanup()
{
    if (pollTimer != nullptr)
    {
        pollTimer->stop();
        pollTimer = nullptr;
    }
    if (paramRing != nullptr)
        paramRing->clear();
    if (paramService != nullptr)
    {
        juce::String pluginID = slotTree.getProperty(IDs::pluginID).toString();
        paramService->setParamChangeCallback(trackIndex, pluginID.toStdString(), nullptr);
    }
}

FXSlotRow::~FXSlotRow()
{
    cleanup();
}

bool FXSlotRow::eventFilter(QObject* obj, QEvent* event)
{
    if (obj != nullptr && obj->property("dragHandleSlotIndex").isValid())
    {
        if (event->type() == QEvent::MouseButtonPress)
        {
            auto* e = static_cast<QMouseEvent*>(event);
            if (e->button() == Qt::LeftButton)
                dragStartPos = e->pos();
        }
        else if (event->type() == QEvent::MouseMove)
        {
            auto* e = static_cast<QMouseEvent*>(event);
            if ((e->buttons() & Qt::LeftButton)
                && (e->pos() - dragStartPos).manhattanLength() >= QApplication::startDragDistance())
            {
                startDrag();
                return true;
            }
        }
    }
    return QWidget::eventFilter(obj, event);
}

void FXSlotRow::startDrag()
{
    static const QString mimeType = "application/x-hdaw-fxslot";
    QMimeData* mime = new QMimeData();
    mime->setData(mimeType, QByteArray::number(slotIndex));
    QDrag* drag = new QDrag(this);
    drag->setMimeData(mime);
    drag->exec(Qt::MoveAction);
    drag->deleteLater();
}

void FXSlotRow::populateTypeCombo(const QString& filter)
{
    typeCombo->clear();
    typeCombo->addItem("EQ");
    typeCombo->addItem("Compressor");
    typeCombo->addItem("Reverb");
    typeCombo->addItem("Delay");

    auto plugInfos = pluginService->getPlugins();
    if (plugInfos.empty())
        return;

    std::vector<PluginInfo> instruments, effects;
    for (const auto& info : plugInfos)
    {
        if (pluginService->isBlacklisted(info.fileOrIdentifier))
            continue;
        if (info.isInstrument)
            instruments.push_back(info);
        else
            effects.push_back(info);
    }

    auto addPlugins = [&](const std::vector<PluginInfo>& list)
    {
        for (const auto& info : list)
        {
            QString label = QString("[%1] %2")
                .arg(QString::fromStdString(info.format))
                .arg(QString::fromStdString(info.name));

            if (!filter.isEmpty() && !label.contains(filter, Qt::CaseInsensitive))
                continue;

            QString pluginID = QString::fromStdString(info.fileOrIdentifier);
            typeCombo->addItem(label, QVariant(pluginID));
        }
    };

    if (!instruments.empty())
    {
        typeCombo->insertSeparator(typeCombo->count());
        addPlugins(instruments);
    }

    if (!effects.empty())
    {
        typeCombo->insertSeparator(typeCombo->count());
        addPlugins(effects);
    }
}

void FXSlotRow::onTypeChanged(const juce::String& type)
{
    paramSliders.clear();

    if (type == "eq" || type == "compressor" || type == "reverb" || type == "delay")
    {
        projectCmds->setFxSlotPlugin(trackIndex, slotIndex,
            type.toStdString(), "", "", "");
        paramContainer->setVisible(false);
        editBtn->setVisible(false);
        paramsBtn->setVisible(false);
    }
    else
    {
        auto plugins = pluginService->getPlugins();
        bool found = false;
        auto typeStr = type.toStdString();
        for (const auto& info : plugins)
        {
            if (info.fileOrIdentifier == typeStr)
            {
                projectCmds->setFxSlotPlugin(trackIndex, slotIndex,
                    "plugin", info.fileOrIdentifier, info.format, info.fileOrIdentifier);
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
    if (pollTimer != nullptr)
        pollTimer->stop();
    if (paramRing != nullptr)
        paramRing->clear();
    if (paramService != nullptr)
    {
        juce::String oldPluginID = slotTree.getProperty(IDs::pluginID).toString();
        paramService->setParamChangeCallback(trackIndex, oldPluginID.toStdString(), nullptr);
    }

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

    juce::String pluginID = slotTree.getProperty(IDs::pluginID).toString();
    auto params = paramService->getParams(trackIndex, pluginID.toStdString());
    if (params.empty()) return;

    auto* paramLayout = new QVBoxLayout(paramContainer);
    paramLayout->setContentsMargins(20, 2, 4, 2);
    paramLayout->setSpacing(2);

    for (const auto& snap : params)
    {
        auto* row = new QWidget(paramContainer);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(4);

        auto* nameLabel = new QLabel(QString::fromStdString(snap.name), row);
        nameLabel->setFixedWidth(80);
        rowLayout->addWidget(nameLabel);

        auto* slider = new QSlider(Qt::Horizontal, row);
        slider->setRange(0, 1000);
        slider->setValue(static_cast<int>(snap.value * 1000.0f));
        slider->setFixedHeight(16);
        rowLayout->addWidget(slider, 1);

        auto* valLabel = new QLabel(row);
        QString displayText = QString::fromStdString(snap.text);
        if (!snap.label.empty())
            displayText += " " + QString::fromStdString(snap.label);
        valLabel->setText(displayText);
        valLabel->setFixedWidth(80);
        rowLayout->addWidget(valLabel);

        paramLayout->addWidget(row);

        ParamSlider ps;
        ps.slider = slider;
        ps.valueLabel = valLabel;
        ps.paramIdx = snap.index;
        paramSliders.push_back(ps);

        int idx = snap.index;
        connect(slider, &QSlider::valueChanged, this,
            [this, idx](int val) {
                float normalized = val / 1000.0f;
                juce::String pluginID = slotTree.getProperty(IDs::pluginID).toString();
                std::string pid = pluginID.toStdString();
                paramService->setParam(trackIndex, pid, idx, normalized);
                std::string text = paramService->getParamText(trackIndex, pid, idx, normalized);
                for (const auto& ps : paramSliders)
                {
                    if (ps.paramIdx == idx)
                        ps.valueLabel->setText(QString::fromStdString(text));
                }
            });
    }

    // Register callback for audio-thread parameter changes via the service.
    // The callback pushes into the lock-free ring; the UI timer drains it.
    paramService->setParamChangeCallback(trackIndex, pluginID.toStdString(),
        [this](int idx, float val) {
            if (paramRing) paramRing->push(idx, val);
        });

    if (pollTimer != nullptr)
        pollTimer->start(33);
}

void FXSlotRow::pollParamUpdates()
{
    if (paramRing == nullptr || paramSliders.empty() || paramService == nullptr)
        return;

    juce::String pluginID = slotTree.getProperty(IDs::pluginID).toString();
    std::string pid = pluginID.toStdString();

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

            std::string text = paramService->getParamText(trackIndex, pid, e.idx, e.value);
            ps.valueLabel->setText(QString::fromStdString(text));
        }
    }
}
