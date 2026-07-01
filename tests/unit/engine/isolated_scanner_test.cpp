#include <gtest/gtest.h>
#include <juce_core/juce_core.h>

// Test that the scanner exe path can be resolved
TEST(IsolatedScanner, ScannerExePathResolves)
{
    auto exeDir = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                      .getParentDirectory();
    auto scannerExe = exeDir.getChildFile("hdaw_plugin_scanner.exe");
    // The scanner should exist next to the test exe (deployed by build)
    // This may fail if the scanner wasn't built — that's OK, it's a build-config check.
    EXPECT_TRUE(scannerExe.existsAsFile())
        << "hdaw_plugin_scanner.exe not found at: " << scannerExe.getFullPathName().toRawUTF8();
}

// Test that a non-existent plugin path returns failure
TEST(IsolatedScanner, NonExistentPluginFailsGracefully)
{
    auto exeDir = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                      .getParentDirectory();
    auto scannerExe = exeDir.getChildFile("hdaw_plugin_scanner.exe");
    if (!scannerExe.existsAsFile())
        GTEST_SKIP() << "Scanner exe not built";

    auto pedalFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                         .getChildFile("test_pedal_scanner.txt");

    auto cmd = "\"" + scannerExe.getFullPathName() + "\""
             + " --plugin=\"C:\\nonexistent\\fake.vst3\""
             + " --pedal-file=\"" + pedalFile.getFullPathName() + "\"";

    juce::ChildProcess child;
    ASSERT_TRUE(child.start(cmd, 0));
    bool finished = child.waitForProcessToFinish(10000);
    EXPECT_TRUE(finished);

    int exitCode = child.getExitCode();

    // Should exit 1 (load failure, not crash)
    EXPECT_EQ(exitCode, 1);

    // Pedal file should be cleared (scanner writes plugin path then clears on exit)
    if (pedalFile.existsAsFile())
    {
        auto pedalContent = pedalFile.loadFileAsString();
        EXPECT_TRUE(pedalContent.isEmpty())
            << "Pedal file should be cleared after non-crash exit, got: "
            << pedalContent.toRawUTF8();
    }

    pedalFile.deleteFile();
}

// Test that blacklist reason round-trips through XML
TEST(IsolatedScanner, BlacklistReasonRoundTrip)
{
    // Create a temporary blacklist file
    auto tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    auto tempBlacklist = tempDir.getChildFile("test_blacklist_roundtrip.xml");

    // Write a blacklist with reason
    {
        juce::XmlElement root("BLACKLIST");
        auto* el = root.createNewChildElement("PLUGIN");
        el->setAttribute("id", "C:\\test\\crashed.vst3");
        el->setAttribute("reason", "crash");
        root.writeTo(tempBlacklist, {});
    }

    // Read it back
    {
        auto xml = juce::XmlDocument::parse(tempBlacklist);
        ASSERT_NE(xml, nullptr);
        // XmlDocument::parse returns the root element directly
        EXPECT_EQ(xml->getTagName(), "BLACKLIST");
        auto* el = xml->getChildElement(0);
        ASSERT_NE(el, nullptr);
        EXPECT_EQ(el->getStringAttribute("id"), "C:\\test\\crashed.vst3");
        EXPECT_EQ(el->getStringAttribute("reason"), "crash");
    }

    tempBlacklist.deleteFile();
}
