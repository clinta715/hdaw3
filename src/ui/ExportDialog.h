#pragma once
#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QProgressBar>
#include <QLabel>
#include <QComboBox>
#include "../common/ProjectCommands.h"
#include "../common/TransportCommands.h"
#include "../common/AudioGraphCommands.h"
#include "../common/ReadModel.h"

class AudioEngine;
#include "../engine/ExportManager.h"

class ExportDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ExportDialog(AudioEngine& engine, QWidget* parent = nullptr);
    ~ExportDialog() override;

private slots:
    void onBrowse();
    void onExport();
    void onCancel();

private:
    void setExporting(bool exporting);
    QString defaultExtension() const;

    AudioEngine& engine;
    ProjectCommands* projectCmds = nullptr;
    TransportCommands* transportCmds = nullptr;
    AudioGraphCommands* audioGraphCmds = nullptr;
    ReadModel* readModel = nullptr;
    QLineEdit* pathEdit;
    QComboBox* formatCombo;
    QComboBox* bitDepthCombo;
    QPushButton* browseBtn;
    QProgressBar* progressBar;
    QPushButton* exportBtn;
    QPushButton* cancelBtn;
    QLabel* statusLabel;
};
