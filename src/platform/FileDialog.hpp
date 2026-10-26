#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace galaxy {

std::optional<std::filesystem::path> openMapDialog(const std::filesystem::path& initialDirectory);

std::optional<std::filesystem::path> saveMapDialog(
    const std::filesystem::path& initialDirectory,
    const std::filesystem::path& suggestedName);

// Asks for a folder. Returns nothing when the user cancels.
std::optional<std::filesystem::path> chooseFolderDialog(
    const std::string& title,
    const std::filesystem::path& initialDirectory);

// Shows a system alert with a short message, a longer explanation, and two buttons.
// Returns true when the user pressed the accept button.
bool alertDialog(
    const std::string& message,
    const std::string& detail,
    const std::string& cancelLabel,
    const std::string& acceptLabel);

}  // namespace galaxy
