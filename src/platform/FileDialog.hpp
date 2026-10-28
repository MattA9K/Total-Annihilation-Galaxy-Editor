#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace galaxy {

std::optional<std::filesystem::path> openMapDialog(const std::filesystem::path& initialDirectory);

std::optional<std::filesystem::path> saveMapDialog(
    const std::filesystem::path& initialDirectory,
    const std::filesystem::path& suggestedName);

// Asks for a folder. Returns nothing when the user cancels.
std::optional<std::filesystem::path> chooseFolderDialog(
    const std::string& title,
    const std::filesystem::path& initialDirectory);

// Asks for one archive: an HPI, UFO, CCX, GP3 or GPF file.
std::optional<std::filesystem::path> openArchiveDialog(const std::filesystem::path& initialDirectory);

// Asks where to save an archive. The suggested name is used as given.
std::optional<std::filesystem::path> saveArchiveDialog(
    const std::filesystem::path& initialDirectory,
    const std::filesystem::path& suggestedName);

// Asks for any number of files. Where the system allows it, folders can be chosen as well.
// Returns nothing when the user cancels.
std::vector<std::filesystem::path> chooseFilesDialog(
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
