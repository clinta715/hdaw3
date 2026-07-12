#include "PluginScannerDialog.h"
#include "../engine/AudioEngine.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QApplication>
#include <QFont>

PluginScannerDialog::PluginScannerDialog(AudioEngine& ae, QWidget* parent)
    : QDialog(parent), engine(ae)
{
    pluginService = &engine.getPluginService();
    projectCmds = &engine.getProjectCommands();
    transportCmds = &engine.getTransportCommands();
    audioGraphCmds = &engine.getAudioGraphCommands();
    readModel = &engine.getReadModel();
    setWindowTitle("Plugin Manager");
    setMinimumSize(550, 450);
    setModal(true);

    auto* layout = new QVBoxLayout(this);

    statusLabel = new QLabel("Scanned plugins:", this);
    layout->addWidget(statusLabel);

    progressBar = new QProgressBar(this);
    progressBar->setRange(0, 0);
    progressBar->hide();
    layout->addWidget(progressBar);

    showBlacklistedCheck = new QCheckBox("Show blacklisted", this);
    showBlacklistedCheck->setChecked(true);
    connect(showBlacklistedCheck, &QCheckBox::toggled, this, &PluginScannerDialog::onShowBlacklistedToggled);
    layout->addWidget(showBlacklistedCheck);

    pluginList = new QListWidget(this);
    connect(pluginList, &QListWidget::currentRowChanged, this, &PluginScannerDialog::onSelectionChanged);
    layout->addWidget(pluginList, 1);

    auto* btnLayout = new QHBoxLayout();

    toggleBlacklistBtn = new QPushButton("Blacklist", this);
    toggleBlacklistBtn->setEnabled(false);
    connect(toggleBlacklistBtn, &QPushButton::clicked, this, &PluginScannerDialog::onToggleBlacklist);
    btnLayout->addWidget(toggleBlacklistBtn);

    rescanBtn = new QPushButton("Re-scan VST3", this);
    connect(rescanBtn, &QPushButton::clicked, this, &PluginScannerDialog::onRescan);
    btnLayout->addWidget(rescanBtn);

    btnLayout->addStretch();

    closeBtn = new QPushButton("Close", this);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    btnLayout->addWidget(closeBtn);

    layout->addLayout(btnLayout);

    if (pluginService->isLoading())
    {
        statusLabel->setText("Scanning...");
        progressBar->show();
    }

    previousCallback = pluginService->getScanCompleteCallback();
    pluginService->setScanCompleteCallback([this]() {
        QMetaObject::invokeMethod(this, "onScanFinished", Qt::QueuedConnection);
    });

    refreshList();
}

PluginScannerDialog::~PluginScannerDialog()
{
    dialogAlive = false;
    if (scanThread.joinable())
        scanThread.join();
    pluginService->setScanCompleteCallback(previousCallback);
}

void PluginScannerDialog::refreshList()
{
    pluginList->blockSignals(true);
    pluginList->clear();

    auto plugins = pluginService->getPlugins();

    if (plugins.empty())
    {
        pluginList->addItem("(no plugins found)");
        pluginList->blockSignals(false);
        return;
    }

    bool showBlacklisted = showBlacklistedCheck->isChecked();

    for (const auto& info : plugins)
    {
        bool bl = pluginService->isBlacklisted(info.fileOrIdentifier);
        if (bl && !showBlacklisted)
            continue;

        auto displayName = QString::fromStdString(info.name) +
            " (" + QString::fromStdString(info.manufacturer) + ")";
        if (bl)
        {
            auto reason = pluginService->getBlacklistReason(info.fileOrIdentifier);
            if (reason == "crash")
                displayName += " — crashed during scan";
        }
        auto* item = new QListWidgetItem(displayName);
        item->setData(Qt::UserRole, QString::fromStdString(info.fileOrIdentifier));
        item->setData(Qt::UserRole + 1, bl);

        if (bl)
        {
            QFont f = item->font();
            f.setStrikeOut(true);
            item->setFont(f);
            item->setForeground(QColor(180, 60, 60));
        }

        pluginList->addItem(item);
    }

    statusLabel->setText(QString("Scanned plugins: %1").arg(plugins.size()));
    pluginList->blockSignals(false);
    onSelectionChanged();
}

void PluginScannerDialog::onRescan()
{
    if (scanThread.joinable())
        scanThread.join();

    statusLabel->setText("Scanning VST3 plugins...");
    progressBar->show();
    rescanBtn->setEnabled(false);

    scanThread = std::thread([this]() {
        pluginService->scanAll();
        if (dialogAlive.load())
            QMetaObject::invokeMethod(this, "onScanFinished", Qt::QueuedConnection);
    });
}

void PluginScannerDialog::onScanFinished()
{
    progressBar->hide();
    rescanBtn->setEnabled(true);
    refreshList();
}

void PluginScannerDialog::onSelectionChanged()
{
    auto* item = pluginList->currentItem();
    if (item == nullptr)
    {
        toggleBlacklistBtn->setEnabled(false);
        return;
    }
    bool isBlacklisted = item->data(Qt::UserRole + 1).toBool();
    toggleBlacklistBtn->setEnabled(true);
    toggleBlacklistBtn->setText(isBlacklisted ? "Unblacklist" : "Blacklist");
}

void PluginScannerDialog::onToggleBlacklist()
{
    auto* item = pluginList->currentItem();
    if (item == nullptr) return;

    juce::String pluginID = item->data(Qt::UserRole).toString().toUtf8().constData();
    bool isBlacklisted = item->data(Qt::UserRole + 1).toBool();

    if (isBlacklisted)
        pluginService->unblacklistPlugin(pluginID.toStdString());
    else
        pluginService->blacklistPlugin(pluginID.toStdString());

    refreshList();
}

void PluginScannerDialog::onShowBlacklistedToggled(bool)
{
    refreshList();
}
