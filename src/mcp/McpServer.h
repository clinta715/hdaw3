#pragma once
#include <QObject>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
class McpServer : public QObject {
    Q_OBJECT
public:
    explicit McpServer(QObject* parent = nullptr) : QObject(parent) {}
public slots:
    void handleRequestOnTestThread(QJsonValue id, QString method, QJsonValue params) {
        (void)id; (void)method; (void)params;
    }
};
