#pragma once
#include <QDialog>
#include <QProgressBar>
#include <QLabel>
#include <QPushButton>
#include <QString>
#include <thread>
#include <atomic>
#include "../engine/PluginManager.h"

class ScanProgressDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ScanProgressDialog(HDAW::PluginManager& pm, QWidget* parent = nullptr);
    ~ScanProgressDialog() override;

private slots:
    void onProgress(const QString& fileName, int done, int total);
    void onFinished();
    void onCancel();

private:
    HDAW::PluginManager& pluginManager;
    QProgressBar* bar;
    QLabel* statusLabel;
    QLabel* countLabel;
    QPushButton* cancelBtn;
    std::thread scanThread;
    std::atomic<bool> alive{true};
};
