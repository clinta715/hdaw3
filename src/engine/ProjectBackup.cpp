#include "ProjectBackup.h"
#include "../common/DebugLog.h"

namespace HDAW {

void backupProject(const juce::File& projectFile, int maxBackups)
{
    try
    {
        if (!projectFile.existsAsFile())
            return;

        auto projectDir = projectFile.getParentDirectory();
        auto stem = projectFile.getFileNameWithoutExtension();
        auto ext = projectFile.getFileExtension();

        auto backupDir = projectDir.getChildFile("auto-backups").getChildFile(stem);
        auto result = backupDir.createDirectory();
        if (result.failed())
        {
            HDAW_LOG("backup", "Failed to create backup directory: " + result.getErrorMessage().toStdString());
            return;
        }

        auto now = juce::Time::getCurrentTime();
        auto ms = now.toMilliseconds() % 1000;
        auto timestamp = now.formatted("%Y-%m-%d %H%M%S") + juce::String::formatted(".%03d", static_cast<int>(ms));
        auto backupName = stem + " [" + timestamp + "]" + ext;
        auto backupFile = backupDir.getChildFile(backupName);

        if (!projectFile.copyFileTo(backupFile))
        {
            HDAW_LOG("backup", "Failed to copy project file to backup: " + backupFile.getFullPathName().toStdString());
            return;
        }

        if (maxBackups <= 0)
            return;

        auto files = backupDir.findChildFiles(juce::File::findFiles, false, "*" + ext);
        if (files.size() <= maxBackups)
            return;

        files.sort();
        int toDelete = files.size() - maxBackups;
        for (int i = 0; i < toDelete; ++i)
        {
            if (!files[i].deleteFile())
                HDAW_LOG("backup", "Failed to prune old backup: " + files[i].getFullPathName().toStdString());
        }
    }
    catch (...)
    {
        HDAW_LOG("backup", "Unexpected error during backup");
    }
}

} // namespace HDAW