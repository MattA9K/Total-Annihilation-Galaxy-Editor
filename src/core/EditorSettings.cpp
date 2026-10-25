#include "core/EditorSettings.hpp"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>

namespace galaxy {
namespace {

int clampInt(int value, int min, int max) {
    if (value < min) {
        return min;
    }
    if (value > max) {
        return max;
    }
    return value;
}

int snapHeightGrid(int value) {
    value = clampInt(value, 16, 256);
    value = (value + 8) / 16 * 16;
    return clampInt(value, 16, 256);
}

std::optional<int> jsonInt(std::string_view text, std::string_view key) {
    const std::string token = "\"" + std::string(key) + "\"";
    std::size_t pos = 0;
    while ((pos = text.find(token, pos)) != std::string_view::npos) {
        std::size_t cursor = pos + token.size();
        while (cursor < text.size() && std::isspace(static_cast<unsigned char>(text[cursor])) != 0) {
            ++cursor;
        }
        if (cursor >= text.size() || text[cursor] != ':') {
            pos += token.size();
            continue;
        }
        ++cursor;
        while (cursor < text.size() && std::isspace(static_cast<unsigned char>(text[cursor])) != 0) {
            ++cursor;
        }
        int sign = 1;
        if (cursor < text.size() && text[cursor] == '-') {
            sign = -1;
            ++cursor;
        }
        if (cursor >= text.size() || std::isdigit(static_cast<unsigned char>(text[cursor])) == 0) {
            return std::nullopt;
        }
        int value = 0;
        while (cursor < text.size() && std::isdigit(static_cast<unsigned char>(text[cursor])) != 0) {
            value = value * 10 + (text[cursor] - '0');
            ++cursor;
        }
        return value * sign;
    }
    return std::nullopt;
}

int jsonOr(std::string_view text, std::string_view key, int fallback, int min, int max) {
    const auto value = jsonInt(text, key);
    return value ? clampInt(*value, min, max) : fallback;
}

}  // namespace

std::filesystem::path editorSettingsPath() {
#if defined(_WIN32)
    if (const char* appData = std::getenv("APPDATA"); appData != nullptr && appData[0] != '\0') {
        return std::filesystem::path(appData) / "Galaxy Editor" / "settings.json";
    }
#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {
        return std::filesystem::path(home) / "Library" / "Application Support" / "Galaxy Editor" / "settings.json";
    }
#else
    if (const char* config = std::getenv("XDG_CONFIG_HOME"); config != nullptr && config[0] != '\0') {
        return std::filesystem::path(config) / "Galaxy Editor" / "settings.json";
    }
    if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {
        return std::filesystem::path(home) / ".config" / "Galaxy Editor" / "settings.json";
    }
#endif
    return std::filesystem::current_path() / "galaxy-editor-settings.json";
}

EditorSettings loadEditorSettings(const std::filesystem::path& path) {
    EditorSettings settings;
    std::ifstream in(path);
    if (!in) {
        return settings;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    settings.previewSize = jsonOr(text, "previewSize", settings.previewSize, 32, 256);
    settings.heightGridSize = snapHeightGrid(jsonOr(text, "heightGridSize", settings.heightGridSize, 16, 256));
    settings.sidebarFontPercent = jsonOr(text, "sidebarFontPercent", settings.sidebarFontPercent, 75, 200);
    settings.autosaveMinutes = jsonOr(text, "autosaveMinutes", settings.autosaveMinutes, 1, 120);
    settings.autosaveBackups = jsonOr(text, "autosaveBackups", settings.autosaveBackups, 1, 20);
    return settings;
}

void saveEditorSettings(const std::filesystem::path& path, const EditorSettings& settings) {
    std::error_code error;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), error);
    }
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        return;
    }
    out << "{\n";
    out << "  \"previewSize\": " << settings.previewSize << ",\n";
    out << "  \"heightGridSize\": " << settings.heightGridSize << ",\n";
    out << "  \"sidebarFontPercent\": " << settings.sidebarFontPercent << ",\n";
    out << "  \"autosaveMinutes\": " << settings.autosaveMinutes << ",\n";
    out << "  \"autosaveBackups\": " << settings.autosaveBackups << "\n";
    out << "}\n";
}

}  // namespace galaxy
