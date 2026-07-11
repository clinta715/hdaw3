#include "ExportDialog.h"
#include "../engine/AudioEngine.h"
#include "PreferencesDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QFileDialog>
#include <QFileInfo>
#include <QSettings>
#include <QMessageBox>

ExportDialog::ExportDialog(AudioEngine& ae, QWidget* parent)
    : QDialog(parent), engine(ae)
{
    projectCmds = &engine.getProjectCommands();
    transportCmds = &engine.getTransportCommands();
    audioGraphCmds = &engine.getAudioGraphCommands();
    readModel = &engine.getReadModel();
    setWindowTitle("Export Audio");
    setMinimumWidth(420);
    setModal(true);

    auto* mainLayout = new QVBoxLayout(this);

    // File path
    auto* fileLayout = new QHBoxLayout();
    pathEdit = new QLineEdit(this);
    pathEdit->setPlaceholderText("Select output file...");
    fileLayout->addWidget(pathEdit, 1);

    browseBtn = new QPushButton("Browse...", this);
    connect(browseBtn, &QPushButton::clicked, this, &ExportDialog::onBrowse);
    fileLayout->addWidget(browseBtn);
    mainLayout->addLayout(fileLayout);

    // Format options
    auto* optionsLayout = new QHBoxLayout();

    auto* fmtLabel = new QLabel("Format:", this);
    optionsLayout->addWidget(fmtLabel);

    formatCombo = new QComboBox(this);
    formatCombo->addItems({"WAV", "AIFF", "FLAC"});
    formatCombo->setCurrentIndex(0);
    connect(formatCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
        [this](int) {
            QString path = pathEdit->text();
            if (!path.isEmpty())
            {
                QFileInfo fi(path);
                QString newPath = fi.path() + "/" + fi.completeBaseName() + "." + defaultExtension();
                pathEdit->setText(newPath);
            }
        });
    optionsLayout->addWidget(formatCombo);

    auto* depthLabel = new QLabel("Bit depth:", this);
    optionsLayout->addWidget(depthLabel);

    bitDepthCombo = new QComboBox(this);
    bitDepthCombo->addItems({"16", "24", "32"});
    bitDepthCombo->setCurrentIndex(1);
    optionsLayout->addWidget(bitDepthCombo);

    optionsLayout->addStretch();
    mainLayout->addLayout(optionsLayout);

    // Progress bar
    progressBar = new QProgressBar(this);
    progressBar->setRange(0, 100);
    progressBar->setValue(0);
    mainLayout->addWidget(progressBar);

    // Status label
    statusLabel = new QLabel("Ready to export", this);
    mainLayout->addWidget(statusLabel);

    // Buttons
    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();

    exportBtn = new QPushButton("Export", this);
    connect(exportBtn, &QPushButton::clicked, this, &ExportDialog::onExport);
    buttonLayout->addWidget(exportBtn);

    cancelBtn = new QPushButton("Cancel", this);
    connect(cancelBtn, &QPushButton::clicked, this, &ExportDialog::onCancel);
    buttonLayout->addWidget(cancelBtn);

    mainLayout->addLayout(buttonLayout);
}

ExportDialog::~ExportDialog()
{
    auto& exportMgr = engine.getMainProcessor()->getExportManager();
    exportMgr.onProgress = nullptr;
    exportMgr.onComplete = nullptr;
}

QString ExportDialog::defaultExtension() const
{
    switch (formatCombo->currentIndex())
    {
        case 0: return "wav";
        case 1: return "aiff";
        case 2: return "flac";
        default: return "wav";
    }
}

void ExportDialog::onBrowse()
{
    auto& settings = PreferencesDialog::settings();
    QString filter = QString("%1 Files (*.%2)")
        .arg(formatCombo->currentText())
        .arg(defaultExtension());
    auto path = QFileDialog::getSaveFileName(this, "Export Audio",
        settings.value(PreferencesDialog::kKeyLastExportDir).toString(), filter);
    if (!path.isEmpty())
    {
        QFileInfo fi(path);
        if (fi.suffix().isEmpty())
            path += "." + defaultExtension();
        pathEdit->setText(path);
        settings.setValue(PreferencesDialog::kKeyLastExportDir,
            fi.absolutePath());
    }
}

void ExportDialog::onExport()
{
    auto path = pathEdit->text();
    if (path.isEmpty())
    {
        QMessageBox::warning(this, "Error", "Please select an output file.");
        return;
    }

    setExporting(true);

    auto& projectModel = engine.getProjectModel();
    double sampleRate = engine.getMainProcessor()->getSampleRate();
    if (sampleRate <= 0.0) sampleRate = 44100.0;
    double duration = HDAW::ExportManager::calculateProjectDuration(projectModel);

    auto* proc = engine.getMainProcessor();
    auto& fmtManager = engine.getProjectPool().getFormatManager();
    auto* pluginManager = &engine.getPluginManager();

    auto& exportMgr = proc->getExportManager();

    exportMgr.onProgress = [this](float p) {
        QMetaObject::invokeMethod(this, [this, p]() {
            progressBar->setValue(static_cast<int>(p * 100.0f));
            statusLabel->setText(QString("Exporting... %1%").arg(static_cast<int>(p * 100.0f)));
        });
    };

    exportMgr.onComplete = [this](bool success, const juce::String& msg) {
        QMetaObject::invokeMethod(this, [this, success, msg]() {
            setExporting(false);
            if (success)
            {
                statusLabel->setText("Export complete!");
                cancelBtn->setText("Close");
            }
            else
            {
                statusLabel->setText(QString::fromUtf8(msg.toRawUTF8()));
                exportBtn->setEnabled(true);
                cancelBtn->setText("Close");
            }
        });
    };

    auto exportFormat = static_cast<HDAW::ExportManager::Format>(formatCombo->currentIndex());
    int bitDepth = bitDepthCombo->currentText().toInt();

    if (!exportMgr.startExport(projectModel.getTree(), fmtManager, pluginManager,
                               juce::File(path.toUtf8().constData()),
                               sampleRate, 0.0, duration, exportFormat, bitDepth))
    {
        setExporting(false);
        QMessageBox::warning(this, "Error", "Failed to start export.");
    }
}

void ExportDialog::onCancel()
{
    if (engine.getMainProcessor()->isExporting())
    {
        engine.getMainProcessor()->getExportManager().cancel();
        statusLabel->setText("Cancelling...");
    }
    else
    {
        reject();
    }
}

void ExportDialog::setExporting(bool exporting)
{
    exportBtn->setEnabled(!exporting);
    browseBtn->setEnabled(!exporting);
    pathEdit->setEnabled(!exporting);
    if (!exporting)
        cancelBtn->setText("Close");
    else
    {
        cancelBtn->setText("Cancel");
        progressBar->setValue(0);
    }
}
