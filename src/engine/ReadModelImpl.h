#pragma once
#include "common/ReadModel.h"
#include <juce_data_structures/juce_data_structures.h>

class ProjectModel;
class AudioEngine;

class ReadModelImpl : public ReadModel {
public:
    explicit ReadModelImpl(ProjectModel& model);
    void setEngine(AudioEngine* engine) { engine_ = engine; }

    ProjectSnapshot snapshot() const override;
    int getTrackCount() const override;
    TrackSnapshot getTrack(int index) const override;
    ClipSnapshot getClip(int clipId) const override;
    std::vector<NoteSnapshot> getNotes(int clipId) const override;
    std::vector<ClipSnapshot::GainEnvelopePoint> getClipGainEnvelope(int clipId) const override;
    TransportSnapshot getTransport() const override;
    int getScaleRoot() const override;
    int getScaleMode() const override;

    std::vector<FxSlotSnapshot> getFxSlots(int trackIndex) const override;
    std::vector<AutomationLaneSnapshot> getAutomationLanes(int trackIndex) const override;
    std::vector<AutomationPointSnapshot> getAutomationPoints(int trackIndex,
        const std::string& laneName) const override;
    std::vector<MarkerSnapshot> getMarkers() const override;
    std::vector<TempoPointSnapshot> getTempoPoints() const override;
    std::vector<AutomatableParamSnapshot> getAutomatableParams(int trackIndex) const override;
    MeterSnapshot getTrackMeter(int trackIndex) const override;
    MeterSnapshot getMasterMeter() const override;
    bool isDirty() const override;

private:
    ProjectModel& model_;
    class AudioEngine* engine_ = nullptr;
};
