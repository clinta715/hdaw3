#include "PluginScannerDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QApplication>

PluginScannerDialog::PluginScannerDialog(AudioEngine& ae, QWidget* parent)
    : QDialog(parent), engine(ae)
{
    setWindowTitle("Plugin Manager");
    setMinimumSize(500, 400);
    setModal(true);

    auto* layout = new QVBoxLayout(this);

    statusLabel = new QLabel("Scanned plugins:", this);
    layout->addWidget(statusLabel);

    progressBar = new QProgressBar(this);
    progressBar->setRange(0, 0);
    progressBar->hide();
    layout->addWidget(progressBar);

    pluginList = new QListWidget(this);
    layout->addWidget(pluginList, 1);

    auto* btnLayout = new QHBoxLayout();

    rescanBtn = new QPushButton("Re-scan VST3", this);
    connect(rescanBtn, &QPushButton::clicked, this, &PluginScannerDialog::onRescan);
    btnLayout->addWidget(rescanBtn);

    btnLayout->addStretch();

    closeBtn = new QPushButton("Close", this);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    btnLayout->addWidget(closeBtn);

    layout->addLayout(btnLayout);

    if (engine.getPluginManager().isLoading())
    {
        statusLabel->setText("Scanning...");
        progressBar->show();
    }

    previousCallback = engine.getPluginManager().getScanCompleteCallback();
    engine.getPluginManager().setScanCompleteCallback([this]() {
        QMetaObject::invokeMethod(this, "onScanFinished", Qt::QueuedConnection);
    });

    refreshList();
}

PluginScannerDialog::~PluginScannerDialog()
{
    dialogAlive = false;
    if (scanThread.joinable())
        scanThread.join();
    engine.getPluginManager().setScanCompleteCallback(previousCallback);
}

void PluginScannerDialog::refreshList()
{
    pluginList->clear();

    const auto& plugins = engine.getPluginManager().getPlugins();
    if (plugins.empty())
    {
        pluginList->addItem("(no plugins found)");
        return;
    }

    for (const auto& desc : plugins)
    {
        auto name = juce::String(desc.name) + " (" + juce::String(desc.manufacturerName) + ")";
        pluginList->addItem(QString::fromUtf8(name.toRawUTF8()));
    }

    statusLabel->setText(QString("Scanned plugins: %1").arg(plugins.size()));
}

void PluginScannerDialog::onRescan()
{
    if (scanThread.joinable())
        scanThread.join();

    statusLabel->setText("Scanning VST3 plugins...");
    progressBar->show();
    rescanBtn->setEnabled(false);

    scanThread = std::thread([this]() {
        engine.getPluginManager().scanAll();
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
