#pragma once
#include <QWidget>
#include <QTimer>
#include <QResizeEvent>
#include <juce_data_structures/juce_data_structures.h>
#include "../common/ProjectCommands.h"
#include "../common/TransportCommands.h"
#include "../common/AudioGraphCommands.h"
#include "../common/ReadModel.h"

class AudioEngine;

class MixerStripWidget : public QWidget, public juce::ValueTree::Listener
{
    Q_OBJECT
public:
    MixerStripWidget(int trackIndex, AudioEngine& engine, QWidget* parent = nullptr);
    ~MixerStripWidget() override;

    int getTrackIndex() const { return trackIndex; }

    void setTrackName(const QString& name);
    void setVolume(float vol);
    void setPan(float pan);
    void setMuted(bool muted);
    void setSoloed(bool soloed);

signals:
    void volumeChanged(int trackIndex, float vol);
    void panChanged(int trackIndex, float pan);
    void muteToggled(int trackIndex, bool muted);
    void soloToggled(int trackIndex, bool soloed);
    void fxButtonClicked(int trackIndex);
    void trackDeleted();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void leaveEvent(QEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

    // -- juce::ValueTree::Listener --
    // Registered on the project root tree (NOT the per-track child) so the
    // listener survives project rebuilds (see AGENTS.md "ValueTree listener
    // orphans after project rebuild"). Filters by identity against the
    // watched trackTree. Cache updates drive update() via the setXxx setters.
    void valueTreePropertyChanged(juce::ValueTree& treeWhosePropertyHasChanged,
                                  const juce::Identifier& property) override;
    void valueTreeChildAdded(juce::ValueTree& parentTree,
                             juce::ValueTree& childWhichHasBeenAdded) override;
    void valueTreeChildRemoved(juce::ValueTree& parentTree,
                               juce::ValueTree& childWhichHasBeenRemoved,
                               int indexFromWhichItWasRemoved) override;

private slots:
    void updateVU();

private:
    void layoutRects();

    int trackIndex = -1;
    AudioEngine& engine;
    juce::ValueTree trackTree;
    ProjectCommands* projectCmds = nullptr;
    TransportCommands* transportCmds = nullptr;
    AudioGraphCommands* audioGraphCmds = nullptr;
    ReadModel* readModel = nullptr;

    QTimer vuTimer;
    float currentLeft = 0.0f;
    float currentRight = 0.0f;

    QString name;
    float volume = 1.0f;
    float pan = 0.0f;
    bool muted = false;
    bool soloed = false;

    bool draggingVol = false;
    bool draggingPan = false;
    QRect nameRect;
    QRect vuLeftRect;
    QRect vuRightRect;
    QRect muteRect;
    QRect soloRect;
    QRect fxBtnRect;
    QRect faderRect;
    QRect faderTrackRect;
    QRect volLabelRect;
    QRect panTrackRect;
    QRect panLabelRect;

    bool destroyed_ = false;

    static constexpr int stripWidth = 60;
    static constexpr int stripHeight = 200;
};
