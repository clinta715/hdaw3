#pragma once
#include "common/ReadModel.h"
#include <juce_data_structures/juce_data_structures.h>

class ProjectModel;

class ReadModelImpl : public ReadModel {
public:
    explicit ReadModelImpl(ProjectModel& model);

    ProjectSnapshot snapshot() const override;
    int getTrackCount() const override;
    TrackSnapshot getTrack(int index) const override;
    ClipSnapshot getClip(int clipId) const override;
    std::vector<NoteSnapshot> getNotes(int clipId) const override;
    TransportSnapshot getTransport() const override;
    int getScaleRoot() const override;
    int getScaleMode() const override;

private:
    ProjectModel& model_;
};
