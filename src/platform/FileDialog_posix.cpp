#include "platform/FileDialog.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace galaxy {

std::optional<std::filesystem::path> openMapDialog(const std::filesystem::path& initialDirectory) {
    std::string command = "zenity --file-selection --title=\"Open Total Annihilation map\""
                          " --file-filter=\"TA maps | *.ufo *.hpi *.tnt\" 2>/dev/null";
    if (!initialDirectory.empty()) {
        command += " --filename=" + initialDirectory.string() + "/";
    }

    FILE* pipe = ::popen(command.c_str(), "r");
    if (pipe == nullptr) {
        return std::nullopt;
    }
    std::string selected;
    char buffer[512];
    while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        selected += buffer;
    }
    const int rc = ::pclose(pipe);
    while (!selected.empty() && (selected.back() == '\n' || selected.back() == '\r')) {
        selected.pop_back();
    }
    if (rc != 0 || selected.empty()) {
        return std::nullopt;
    }
    return std::filesystem::path(selected);
}

std::string shellQuote(const std::string& text) {
    std::string quoted = "'";
    for (char character : text) {
        if (character == '\'') {
            quoted += "'\\''";
        } else {
            quoted += character;
        }
    }
    quoted += "'";
    return quoted;
}

std::optional<std::filesystem::path> saveMapDialog(
    const std::filesystem::path& initialDirectory,
    const std::filesystem::path& suggestedName) {
    std::filesystem::path suggestion = suggestedName.empty() ? std::filesystem::path("map.ufo") : suggestedName.filename();
    if (!initialDirectory.empty()) {
        suggestion = initialDirectory / suggestion;
    }
    const std::string command = "zenity --file-selection --save --confirm-overwrite"
                                " --title=\"Save Total Annihilation map\""
                                " --filename="
        + shellQuote(suggestion.string()) + " 2>/dev/null";

    FILE* pipe = ::popen(command.c_str(), "r");
    if (pipe == nullptr) {
        return std::nullopt;
    }
    std::string selected;
    char buffer[512];
    while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        selected += buffer;
    }
    const int rc = ::pclose(pipe);
    while (!selected.empty() && (selected.back() == '\n' || selected.back() == '\r')) {
        selected.pop_back();
    }
    if (rc != 0 || selected.empty()) {
        return std::nullopt;
    }
    return std::filesystem::path(selected);
}

std::optional<std::filesystem::path> chooseFolderDialog(
    const std::string& title,
    const std::filesystem::path& initialDirectory) {
    std::string command = "zenity --file-selection --directory --title=" + shellQuote(title);
    if (!initialDirectory.empty()) {
        command += " --filename=" + shellQuote(initialDirectory.string() + "/");
    }
    command += " 2>/dev/null";

    FILE* pipe = ::popen(command.c_str(), "r");
    if (pipe == nullptr) {
        return std::nullopt;
    }
    std::string selected;
    char buffer[512];
    while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        selected += buffer;
    }
    const int rc = ::pclose(pipe);
    while (!selected.empty() && (selected.back() == '\n' || selected.back() == '\r')) {
        selected.pop_back();
    }
    if (rc != 0 || selected.empty()) {
        return std::nullopt;
    }
    return std::filesystem::path(selected);
}

namespace {

std::vector<std::string> runZenity(const std::string& command) {
    std::vector<std::string> lines;
    FILE* pipe = ::popen(command.c_str(), "r");
    if (pipe == nullptr) {
        return lines;
    }
    std::string text;
    char buffer[512];
    while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        text += buffer;
    }
    if (::pclose(pipe) != 0) {
        return lines;
    }
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos) {
            end = text.size();
        }
        if (end > start) {
            lines.push_back(text.substr(start, end - start));
        }
        start = end + 1;
    }
    return lines;
}

}  // namespace

std::optional<std::filesystem::path> openArchiveDialog(const std::filesystem::path& initialDirectory) {
    std::string command = "zenity --file-selection --title='Open archive'"
                          " --file-filter='Archives | *.hpi *.ufo *.ccx *.gp3 *.gpf *.HPI *.UFO *.CCX *.GP3 *.GPF'";
    if (!initialDirectory.empty()) {
        command += " --filename=" + shellQuote(initialDirectory.string() + "/");
    }
    command += " 2>/dev/null";
    const std::vector<std::string> lines = runZenity(command);
    if (lines.empty()) {
        return std::nullopt;
    }
    return std::filesystem::path(lines.front());
}

std::optional<std::filesystem::path> saveArchiveDialog(
    const std::filesystem::path& initialDirectory,
    const std::filesystem::path& suggestedName) {
    std::filesystem::path suggestion = suggestedName.empty() ? std::filesystem::path("archive.hpi") : suggestedName.filename();
    if (!initialDirectory.empty()) {
        suggestion = initialDirectory / suggestion;
    }
    const std::string command = "zenity --file-selection --save --confirm-overwrite --title='Save archive' --filename="
        + shellQuote(suggestion.string()) + " 2>/dev/null";
    const std::vector<std::string> lines = runZenity(command);
    if (lines.empty()) {
        return std::nullopt;
    }
    return std::filesystem::path(lines.front());
}

std::vector<std::filesystem::path> chooseFilesDialog(
    const std::string& title,
    const std::filesystem::path& initialDirectory) {
    std::string command = "zenity --file-selection --multiple --separator='\n' --title=" + shellQuote(title);
    if (!initialDirectory.empty()) {
        command += " --filename=" + shellQuote(initialDirectory.string() + "/");
    }
    command += " 2>/dev/null";
    std::vector<std::filesystem::path> result;
    for (const std::string& line : runZenity(command)) {
        result.emplace_back(line);
    }
    return result;
}

bool alertDialog(
    const std::string& message,
    const std::string& detail,
    const std::string& cancelLabel,
    const std::string& acceptLabel) {
    const std::string text = detail.empty() ? message : message + "\n\n" + detail;
    const std::string command = "zenity --question --no-markup --title='Galaxy Editor' --text=" + shellQuote(text)
        + " --ok-label=" + shellQuote(acceptLabel) + " --cancel-label=" + shellQuote(cancelLabel) + " 2>/dev/null";
    // zenity exits with 0 for the accept button and 1 for cancel. A missing zenity counts as cancel.
    const int status = std::system(command.c_str());
    return status == 0;
}

}  // namespace galaxy
