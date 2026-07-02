#include <gtest/gtest.h>
#include "model/ProjectModel.h"
#include "engine/ProjectSerializer.h"
#include <QDir>
#include <QFile>
#include <juce_core/juce_core.h>

// Verifies the marker data path: markers are stored as ValueTree
// children of MARKER_LIST, which is a child of the PROJECT root.
// They survive a project save/load round-trip.

TEST(Markers, DefaultProjectHasNoMarkers)
{
    ProjectModel model;
    model.createDefaultProject();
    auto list = model.getTree().getChildWithName(IDs::MARKER_LIST);
    EXPECT_FALSE(list.isValid());
}

TEST(Markers, AddMarkerToProject)
{
    ProjectModel model;
    model.createDefaultProject();
    auto projectTree = model.getTree();

    auto markerList = juce::ValueTree(IDs::MARKER_LIST);
    projectTree.addChild(markerList, -1, nullptr);

    juce::ValueTree marker(IDs::MARKER);
    marker.setProperty(IDs::markerTime, 4.0, nullptr);
    marker.setProperty(IDs::markerName, juce::String("Verse"), nullptr);
    marker.setProperty(IDs::markerColor, static_cast<int>(0xFF59e0c4), nullptr);
    markerList.addChild(marker, -1, nullptr);

    auto list = projectTree.getChildWithName(IDs::MARKER_LIST);
    ASSERT_TRUE(list.isValid());
    ASSERT_EQ(list.getNumChildren(), 1);
    auto m = list.getChild(0);
    EXPECT_DOUBLE_EQ(m.getProperty(IDs::markerTime), 4.0);
    EXPECT_EQ(m.getProperty(IDs::markerName).toString(), juce::String("Verse"));
}

TEST(Markers, RoundTripPreservesMarkers)
{
    // 1. Build a project with two markers.
    ProjectModel model;
    model.createDefaultProject();
    auto projectTree = model.getTree();

    auto markerList = juce::ValueTree(IDs::MARKER_LIST);
    projectTree.addChild(markerList, -1, nullptr);

    {
        juce::ValueTree m(IDs::MARKER);
        m.setProperty(IDs::markerTime, 1.5, nullptr);
        m.setProperty(IDs::markerName, juce::String("Intro"), nullptr);
        markerList.addChild(m, -1, nullptr);
    }
    {
        juce::ValueTree m(IDs::MARKER);
        m.setProperty(IDs::markerTime, 8.0, nullptr);
        m.setProperty(IDs::markerName, juce::String("Chorus"), nullptr);
        markerList.addChild(m, -1, nullptr);
    }

    // 2. Save.
    QString tempPath = QDir::tempPath() + "/hdaw_markers_test.hdap";
    QFile::remove(tempPath);
    juce::File saveFile(tempPath.toStdString());
    ASSERT_TRUE(HDAW::ProjectSerializer::save(model, saveFile));

    // 3. Load into a fresh model.
    ProjectModel loaded;
    ASSERT_TRUE(HDAW::ProjectSerializer::load(loaded, saveFile));

    auto loadedList = loaded.getTree().getChildWithName(IDs::MARKER_LIST);
    ASSERT_TRUE(loadedList.isValid());
    ASSERT_EQ(loadedList.getNumChildren(), 2);

    auto first = loadedList.getChild(0);
    EXPECT_DOUBLE_EQ(first.getProperty(IDs::markerTime), 1.5);
    EXPECT_EQ(first.getProperty(IDs::markerName).toString(), juce::String("Intro"));

    auto second = loadedList.getChild(1);
    EXPECT_DOUBLE_EQ(second.getProperty(IDs::markerTime), 8.0);
    EXPECT_EQ(second.getProperty(IDs::markerName).toString(), juce::String("Chorus"));

    QFile::remove(tempPath);
}

TEST(Markers, RemoveMarkerUpdatesList)
{
    ProjectModel model;
    model.createDefaultProject();
    auto projectTree = model.getTree();

    auto markerList = juce::ValueTree(IDs::MARKER_LIST);
    projectTree.addChild(markerList, -1, nullptr);

    juce::ValueTree m(IDs::MARKER);
    m.setProperty(IDs::markerTime, 2.0, nullptr);
    m.setProperty(IDs::markerName, juce::String("To be removed"), nullptr);
    markerList.addChild(m, -1, nullptr);
    ASSERT_EQ(markerList.getNumChildren(), 1);

    markerList.removeChild(m, nullptr);
    EXPECT_EQ(markerList.getNumChildren(), 0);
}
