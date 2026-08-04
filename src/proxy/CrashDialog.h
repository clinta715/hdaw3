#pragma once
#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QString>
#include <functional>

namespace proxy {

class CrashDialog : public QDialog {
    Q_OBJECT
public:
    using RestartFn = std::function<void()>;
    explicit CrashDialog(const QString& pluginName, RestartFn onRestart = {}, QWidget* parent = nullptr);

    bool shouldRestart() const { return restartRequested; }

private:
    bool restartRequested = false;
    RestartFn restartFn;
};

} // namespace proxy
