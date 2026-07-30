#pragma once
#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QString>

namespace proxy {

class CrashDialog : public QDialog {
    Q_OBJECT
public:
    explicit CrashDialog(const QString& pluginName, QWidget* parent = nullptr);

    bool shouldRestart() const { return restartRequested; }

private:
    bool restartRequested = false;
};

} // namespace proxy
