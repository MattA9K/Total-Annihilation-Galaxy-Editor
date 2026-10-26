#pragma once

#include <filesystem>
#include <string>

namespace galaxy {

struct EditorSettings {
    int previewSize = 128;
    int heightGridSize = 32;
    int sidebarFontPercent = 100;
    int autosaveMinutes = 10;
    int autosaveBackups = 5;
    // 0 follows the system, 1 is light, 2 is dark.
    int appearance = 0;
    // Folder where Total Annihilation is installed. Empty means it is not set.
    std::filesystem::path gameFolder;
    // True once the startup prompt for the folder has been answered, including
    // by skipping it, so a skipped prompt is not repeated at every launch.
    bool gameFolderAsked = false;
};

// Settings keep paths as UTF-8 text so one file reads the same on every platform.
std::string pathToUtf8(const std::filesystem::path& path);
std::filesystem::path pathFromUtf8(const std::string& text);

// True when the startup prompt should ask for the Total Annihilation folder:
// the folder is unset and was never asked about, or the saved folder is gone.
bool shouldAskForGameFolder(const EditorSettings& settings);

// Per-user file. A missing or unreadable file returns the defaults above.
std::filesystem::path editorSettingsPath();
EditorSettings loadEditorSettings(const std::filesystem::path& path);
void saveEditorSettings(const std::filesystem::path& path, const EditorSettings& settings);

}  // namespace galaxy
