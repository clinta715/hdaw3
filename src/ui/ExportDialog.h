#pragma once
#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QProgressBar>
#include <QLabel>
#include <QComboBox>
#include "../common/ReadModel.h"
#include "../engine/ExportManager.h"

class AudioEngine;

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
