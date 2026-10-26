#include "platform/FileDialog.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

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
