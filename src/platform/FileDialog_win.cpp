#include "platform/FileDialog.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>

namespace galaxy {

std::optional<std::filesystem::path> openMapDialog(const std::filesystem::path& initialDirectory) {
    wchar_t buffer[MAX_PATH] = {};
    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFilter = L"Total Annihilation maps\0*.ufo;*.hpi;*.tnt\0All files\0*.*\0";
    dialog.lpstrFile = buffer;
    dialog.nMaxFile = MAX_PATH;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    dialog.lpstrTitle = L"Open Total Annihilation map";

    std::wstring initial = initialDirectory.wstring();
    if (!initial.empty()) {
        dialog.lpstrInitialDir = initial.c_str();
    }
    if (!GetOpenFileNameW(&dialog)) {
        return std::nullopt;
    }
    return std::filesystem::path(buffer);
}

std::optional<std::filesystem::path> saveMapDialog(
    const std::filesystem::path& initialDirectory,
    const std::filesystem::path& suggestedName) {
    wchar_t buffer[MAX_PATH] = {};
    const std::wstring suggested = suggestedName.filename().wstring();
    if (!suggested.empty()) {
        wcsncpy_s(buffer, suggested.c_str(), _TRUNCATE);
    }

    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFilter = L"UFO map (*.ufo)\0*.ufo\0HPI archive (*.hpi)\0*.hpi\0TNT map (*.tnt)\0*.tnt\0";
    dialog.lpstrFile = buffer;
    dialog.nMaxFile = MAX_PATH;
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    dialog.lpstrTitle = L"Save Total Annihilation map";
    dialog.lpstrDefExt = L"ufo";

    std::wstring initial = initialDirectory.wstring();
    if (!initial.empty()) {
        dialog.lpstrInitialDir = initial.c_str();
    }
    if (!GetSaveFileNameW(&dialog)) {
        return std::nullopt;
    }
    return std::filesystem::path(buffer);
}

}  // namespace galaxy
