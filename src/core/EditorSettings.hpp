#pragma once

#include <filesystem>

namespace galaxy {

struct EditorSettings {
    int previewSize = 128;
    int heightGridSize = 32;
    int sidebarFontPercent = 100;
    int autosaveMinutes = 10;
    int autosaveBackups = 5;
};

// Per-user file. A missing or unreadable file returns the defaults above.
std::filesystem::path editorSettingsPath();
EditorSettings loadEditorSettings(const std::filesystem::path& path);
void saveEditorSettings(const std::filesystem::path& path, const EditorSettings& settings);

}  // namespace galaxy
