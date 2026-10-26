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

std::size_t valueStart(std::string_view text, std::string_view key) {
    const std::string token = "\"" + std::string(key) + "\"";
    std::size_t pos = 0;
    while ((pos = text.find(token, pos)) != std::string_view::npos) {
        std::size_t cursor = pos + token.size();
        while (cursor < text.size() && std::isspace(static_cast<unsigned char>(text[cursor])) != 0) {
            ++cursor;
        }
        if (cursor < text.size() && text[cursor] == ':') {
            ++cursor;
            while (cursor < text.size() && std::isspace(static_cast<unsigned char>(text[cursor])) != 0) {
                ++cursor;
            }
            return cursor;
        }
        pos += token.size();
    }
    return std::string_view::npos;
}

bool jsonBoolOr(std::string_view text, std::string_view key, bool fallback) {
    const std::size_t start = valueStart(text, key);
    if (start == std::string_view::npos) {
        return fallback;
    }
    if (text.substr(start, 4) == "true") {
        return true;
    }
    if (text.substr(start, 5) == "false") {
        return false;
    }
    return fallback;
}

int hexDigit(char character) {
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}

std::optional<unsigned> hexQuad(std::string_view text, std::size_t at) {
    if (at + 4 > text.size()) {
        return std::nullopt;
    }
    unsigned value = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        const int digit = hexDigit(text[at + i]);
        if (digit < 0) {
            return std::nullopt;
        }
        value = value * 16 + static_cast<unsigned>(digit);
    }
    return value;
}

void appendUtf8(std::string& out, unsigned codePoint) {
    if (codePoint < 0x80) {
        out.push_back(static_cast<char>(codePoint));
    } else if (codePoint < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
        out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    } else if (codePoint < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
        out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
        out.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    }
}

// Reads a JSON string value and undoes its escapes. Returns an empty string when
// the key is missing or its value is not a string.
std::string jsonString(std::string_view text, std::string_view key) {
    std::size_t cursor = valueStart(text, key);
    if (cursor == std::string_view::npos || cursor >= text.size() || text[cursor] != '"') {
        return {};
    }
    ++cursor;
    std::string out;
    while (cursor < text.size() && text[cursor] != '"') {
        const char character = text[cursor++];
        if (character != '\\') {
            out.push_back(character);
            continue;
        }
        if (cursor >= text.size()) {
            return {};
        }
        const char escape = text[cursor++];
        switch (escape) {
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case 'u': {
            const auto first = hexQuad(text, cursor);
            if (!first) {
                return {};
            }
            cursor += 4;
            unsigned codePoint = *first;
            if (codePoint >= 0xD800 && codePoint < 0xDC00 && cursor + 1 < text.size() && text[cursor] == '\\' && text[cursor + 1] == 'u') {
                if (const auto second = hexQuad(text, cursor + 2); second && *second >= 0xDC00 && *second < 0xE000) {
                    codePoint = 0x10000 + ((codePoint - 0xD800) << 10) + (*second - 0xDC00);
                    cursor += 6;
                }
            }
            appendUtf8(out, codePoint);
            break;
        }
        default: out.push_back(escape); break;  // covers \\ \" and \/
        }
    }
    if (cursor >= text.size()) {
        return {};  // the closing quote is missing
    }
    return out;
}

std::string jsonQuoted(const std::string& text) {
    std::string out = "\"";
    for (const char character : text) {
        switch (character) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(character) < 0x20) {
                static const char* const kHex = "0123456789abcdef";
                out += "\\u00";
                out.push_back(kHex[(character >> 4) & 0xF]);
                out.push_back(kHex[character & 0xF]);
            } else {
                out.push_back(character);
            }
        }
    }
    out += "\"";
    return out;
}

}  // namespace

std::string pathToUtf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

std::filesystem::path pathFromUtf8(const std::string& text) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

bool shouldAskForGameFolder(const EditorSettings& settings) {
    if (settings.gameFolder.empty()) {
        return !settings.gameFolderAsked;
    }
    std::error_code error;
    return !std::filesystem::is_directory(settings.gameFolder, error);
}

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
    settings.appearance = jsonOr(text, "appearance", settings.appearance, 0, 2);
    settings.gameFolder = pathFromUtf8(jsonString(text, "gameFolder"));
    settings.gameFolderAsked = jsonBoolOr(text, "gameFolderAsked", settings.gameFolderAsked);
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
    out << "  \"autosaveBackups\": " << settings.autosaveBackups << ",\n";
    out << "  \"appearance\": " << settings.appearance << ",\n";
    out << "  \"gameFolder\": " << jsonQuoted(pathToUtf8(settings.gameFolder)) << ",\n";
    out << "  \"gameFolderAsked\": " << (settings.gameFolderAsked ? "true" : "false") << "\n";
    out << "}\n";
}

}  // namespace galaxy
