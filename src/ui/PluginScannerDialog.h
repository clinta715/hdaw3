#pragma once
#include <QDialog>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include <thread>
#include <atomic>
#include "../engine/AudioEngine.h"

class PluginScannerDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PluginScannerDialog(AudioEngine& engine, QWidget* parent = nullptr);
    ~PluginScannerDialog() override;

private slots:
    void onRescan();
    void onScanFinished();

private:
    void refreshList();

    AudioEngine& engine;
    QListWidget* pluginList;
    QPushButton* rescanBtn;
    QPushButton* closeBtn;
    QLabel* statusLabel;
    QProgressBar* progressBar;

    std::thread scanThread;
    std::atomic<bool> dialogAlive{true};
    HDAW::PluginManager::ScanCallback previousCallback;
};
