#include "CrashDialog.h"

namespace proxy {

CrashDialog::CrashDialog(const QString& pluginName, RestartFn onRestart, QWidget* parent)
    : QDialog(parent), restartFn(std::move(onRestart))
{
    setWindowTitle("Plugin Crashed");
    setMinimumWidth(350);

    auto* layout = new QVBoxLayout(this);

    auto* label = new QLabel(
        QString("<b>%1</b> has crashed and was stopped.<br><br>"
                "The plugin ran in an isolated process, so the DAW was not affected.")
            .arg(pluginName));
    label->setWordWrap(true);
    layout->addWidget(label);

    auto* restartBtn = new QPushButton("Restart Plugin");
    connect(restartBtn, &QPushButton::clicked, this, [this]() {
        restartRequested = true;
        if (restartFn) restartFn();
        accept();
    });
    layout->addWidget(restartBtn);

    auto* dismissBtn = new QPushButton("Dismiss");
    connect(dismissBtn, &QPushButton::clicked, this, &QDialog::reject);
    layout->addWidget(dismissBtn);
}

} // namespace proxy
