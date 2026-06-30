#pragma once
#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <functional>

namespace proxy {

class CrashDialog : public QDialog {
    Q_OBJECT
public:
    explicit CrashDialog(const QString& pluginName, QWidget* parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle("Plugin Crashed");
        setMinimumWidth(300);

        auto* layout = new QVBoxLayout(this);

        auto* label = new QLabel(
            "The plugin <b>" + pluginName + "</b> has crashed.\n\n"
            "Would you like to restart it?");
        label->setWordWrap(true);
        layout->addWidget(label);

        auto* restartBtn = new QPushButton("Restart Plugin");
        restartBtn->setStyleSheet("background-color: #4a9eff; color: white; padding: 8px;");
        layout->addWidget(restartBtn);

        auto* dismissBtn = new QPushButton("Dismiss");
        layout->addWidget(dismissBtn);

        connect(restartBtn, &QPushButton::clicked, [this]() {
            accepted = true;
            accept();
        });
        connect(dismissBtn, &QPushButton::clicked, [this]() {
            accepted = false;
            reject();
        });
    }

    bool wasAccepted() const { return accepted; }

private:
    bool accepted = false;
};

} // namespace proxy
