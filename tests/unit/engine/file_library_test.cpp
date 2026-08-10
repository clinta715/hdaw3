#include <gtest/gtest.h>
#include <juce_core/juce_core.h>
#include "engine/FileLibraryManager.h"

class FileLibraryTest : public ::testing::Test {
protected:
    void SetUp() override {
        tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                      .getChildFile("hdaw_file_library_test");
        tempDir.createDirectory();
    }
    void TearDown() override {
        tempDir.deleteRecursively();
    }
    juce::File tempDir;
};

TEST_F(FileLibraryTest, AddLibraryPersistsToRegistry) {
    HDAW::FileLibraryManager mgr;
    auto id = mgr.addLibrary("Test MIDI", "/some/path", "midi");
    EXPECT_FALSE(id.isEmpty());
    auto info = mgr.getLibraryInfo(id);
    EXPECT_EQ(info.name, "Test MIDI");
    EXPECT_EQ(info.path, "/some/path");
    EXPECT_EQ(info.type, "midi");
    EXPECT_FALSE(info.autoScan);
}

TEST_F(FileLibraryTest, RemoveLibraryDeletesFromRegistry) {
    HDAW::FileLibraryManager mgr;
    auto id = mgr.addLibrary("To Remove", "/tmp/test", "audio");
    mgr.removeLibrary(id);
    auto ids = mgr.getLibraryIds();
    EXPECT_TRUE(std::find(ids.begin(), ids.end(), id) == ids.end());
}

TEST_F(FileLibraryTest, SetAutoScanTogglesFlag) {
    HDAW::FileLibraryManager mgr;
    auto id = mgr.addLibrary("Auto", "/tmp/test", "midi");
    mgr.setAutoScan(id, true);
    auto info = mgr.getLibraryInfo(id);
    EXPECT_TRUE(info.autoScan);
}
