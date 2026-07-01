#include "ScanProgressDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QApplication>

ScanProgressDialog::ScanProgressDialog(HDAW::PluginManager& pm, QWidget* parent)
    : QDialog(parent), pluginManager(pm)
{
    setWindowTitle("Scanning Plugins");
    setFixedSize(420, 160);
    setModal(true);

    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(12);

    statusLabel = new QLabel("Scanning plugins...", this);
    auto exeDir = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                      .getParentDirectory();
    auto scannerExe = exeDir.getChildFile("hdaw_plugin_scanner.exe");
    if (scannerExe.existsAsFile())
        statusLabel->setText("Scanning plugins (isolated)...");
    QFont f = statusLabel->font();
    f.setPointSize(10);
    statusLabel->setFont(f);
    layout->addWidget(statusLabel);

    bar = new QProgressBar(this);
    bar->setRange(0, 0);
    bar->setMinimumWidth(360);
    layout->addWidget(bar);

    countLabel = new QLabel("", this);
    layout->addWidget(countLabel);

    layout->addStretch();

    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();

    cancelBtn = new QPushButton("Cancel", this);
    connect(cancelBtn, &QPushButton::clicked, this, &ScanProgressDialog::onCancel);
    btnLayout->addWidget(cancelBtn);

    layout->addLayout(btnLayout);

    // Start scanning on a background thread
    scanThread = std::thread([this]() {
        pluginManager.scanAll(
            [this](const juce::String& fileName, int done, int total) {
                QMetaObject::invokeMethod(this, "onProgress", Qt::QueuedConnection,
                    Q_ARG(QString, QString::fromUtf8(fileName.toRawUTF8())),
                    Q_ARG(int, done), Q_ARG(int, total));
            });
        if (alive.load())
            QMetaObject::invokeMethod(this, "onFinished", Qt::QueuedConnection);
    });
}

ScanProgressDialog::~ScanProgressDialog()
{
    alive = false;
    pluginManager.abortScan();
    if (scanThread.joinable())
        scanThread.join();
}

void ScanProgressDialog::onProgress(const QString& fileName, int done, int)
{
    statusLabel->setText("Scanning: " + fileName);
    countLabel->setText(QString("Found %1 plugins so far").arg(done));
}

void ScanProgressDialog::onFinished()
{
    if (alive.load())
    {
        int crashes = pluginManager.getLastScanCrashCount();
        if (crashes > 0)
            statusLabel->setText(
                QString("Scan complete — %1 plugin(s) crashed and were blacklisted").arg(crashes));
        else
            statusLabel->setText("Scan complete");
        accept();
    }
}

void ScanProgressDialog::onCancel()
{
    alive = false;
    pluginManager.abortScan();
    reject();
}
