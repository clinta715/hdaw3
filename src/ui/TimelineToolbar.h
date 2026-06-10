#pragma once
#include <QWidget>
#include <QPushButton>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QTimer>

class TimelineToolbar : public QWidget
{
    Q_OBJECT
public:
    explicit TimelineToolbar(QWidget* parent = nullptr);
    ~TimelineToolbar() override;

public slots:
    void setPlaying(bool playing);
    void setTimecode(const QString& text);
    void setBPM(double bpm);
    void setMetronomeEnabled(bool enabled);
    void setDefaultClipLen(double beats);

signals:
    void addTrackClicked();
    void snapToggleChanged(bool enabled);
    void snapDivisionChanged(int index);
    void zoomInClicked();
    void zoomOutClicked();
    void gridTypeChanged(bool showBeats);
    void loopToggleChanged(bool enabled);
    void followPlayheadChanged(bool enabled);
    void recordClicked();
    void playClicked();
    void stopClicked();
    void rewindClicked();
    void bpmChanged(double bpm);
    void metronomeToggled(bool enabled);
    void defaultClipLenChanged(double beats);

private:
    QPushButton* addTrackBtn;
    QPushButton* snapBtn;
    QComboBox* snapCombo;
    QPushButton* zoomInBtn;
    QPushButton* zoomOutBtn;
    QComboBox* gridCombo;
    QPushButton* loopBtn;
    QPushButton* followBtn;
    QPushButton* playBtn;
    QPushButton* stopBtn;
    QPushButton* rewindBtn;
    QPushButton* recordBtn;
    QLabel* timecodeLabel;
    QDoubleSpinBox* bpmSpinBox;
    QLabel* timeSigLabel;
    QPushButton* metronomeBtn;
    QDoubleSpinBox* defaultClipLenSpinBox;
};
