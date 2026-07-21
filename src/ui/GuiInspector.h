#pragma once
#include <QJsonObject>
#include <QJsonArray>

class MainWindow;

namespace HDAW {

class GuiInspector {
public:
    explicit GuiInspector(MainWindow* mw);

    bool isAvailable() const;

    QJsonObject snapshot() const;
    QJsonArray clipGeometry(int clipId = -1) const;
    QJsonArray trackLayout() const;
    QJsonObject selectionState() const;
    QJsonObject scrollState() const;
    QJsonObject panelState() const;
    QJsonObject pianoRollState() const;
    QJsonObject hitTest(double sceneX, double sceneY) const;

private:
    MainWindow* mw;
};

} // namespace HDAW
