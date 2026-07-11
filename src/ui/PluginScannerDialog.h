#pragma once
#include <QDialog>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include <QCheckBox>
#include <QWidget>
#include <thread>
#include <atomic>
#include "../engine/PluginManager.h"
#include "../common/ProjectCommands.h"
#include "../common/TransportCommands.h"
#include "../common/AudioGraphCommands.h"
#include "../common/ReadModel.h"

class AudioEngine;

class PluginScannerDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PluginScannerDialog(AudioEngine& engine, QWidget* parent = nullptr);
    ~PluginScannerDialog() override;

private slots:
    void onRescan();
    void onScanFinished();
    void onSelectionChanged();
    void onToggleBlacklist();
    void onShowBlacklistedToggled(bool show);

private:
    void refreshList();

    AudioEngine& engine;
    ProjectCommands* projectCmds = nullptr;
    TransportCommands* transportCmds = nullptr;
    AudioGraphCommands* audioGraphCmds = nullptr;
    ReadModel* readModel = nullptr;
    QListWidget* pluginList;
    QPushButton* rescanBtn;
    QPushButton* toggleBlacklistBtn;
    QPushButton* closeBtn;
    QLabel* statusLabel;
    QProgressBar* progressBar;
    QCheckBox* showBlacklistedCheck;

    std::thread scanThread;
    std::atomic<bool> dialogAlive{true};
    HDAW::PluginManager::ScanCallback previousCallback;
};
