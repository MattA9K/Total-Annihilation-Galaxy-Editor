#include "platform/FileDialog.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#include <commctrl.h>
#include <shobjidl.h>

#include <string>
#include <vector>

#if defined(_MSC_VER)
// Task dialogs need version 6 of the common controls.
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

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

std::optional<std::filesystem::path> chooseFolderDialog(
    const std::string& title,
    const std::filesystem::path& initialDirectory) {
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    std::optional<std::filesystem::path> result;

    IFileOpenDialog* dialog = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        DWORD options = 0;
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);

        const int titleLength = MultiByteToWideChar(CP_UTF8, 0, title.c_str(), -1, nullptr, 0);
        if (titleLength > 1) {
            std::wstring wideTitle(static_cast<std::size_t>(titleLength), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, title.c_str(), -1, wideTitle.data(), titleLength);
            dialog->SetTitle(wideTitle.c_str());
        }
        if (!initialDirectory.empty()) {
            IShellItem* folder = nullptr;
            if (SUCCEEDED(SHCreateItemFromParsingName(initialDirectory.c_str(), nullptr, IID_PPV_ARGS(&folder)))) {
                dialog->SetFolder(folder);
                folder->Release();
            }
        }
        if (SUCCEEDED(dialog->Show(nullptr))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dialog->GetResult(&item))) {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path != nullptr) {
                    result = std::filesystem::path(path);
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }
        dialog->Release();
    }
    if (SUCCEEDED(initialized)) {
        CoUninitialize();
    }
    return result;
}

namespace {

std::wstring wideFromUtf8(const std::string& text) {
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (length <= 1) {
        return {};
    }
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide.data(), length);
    wide.resize(static_cast<std::size_t>(length) - 1);
    return wide;
}

}  // namespace

std::optional<std::filesystem::path> openArchiveDialog(const std::filesystem::path& initialDirectory) {
    wchar_t buffer[MAX_PATH] = {};
    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFilter = L"Archives\0*.hpi;*.ufo;*.ccx;*.gp3;*.gpf\0All files\0*.*\0";
    dialog.lpstrFile = buffer;
    dialog.nMaxFile = MAX_PATH;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    dialog.lpstrTitle = L"Open archive";

    std::wstring initial = initialDirectory.wstring();
    if (!initial.empty()) {
        dialog.lpstrInitialDir = initial.c_str();
    }
    if (!GetOpenFileNameW(&dialog)) {
        return std::nullopt;
    }
    return std::filesystem::path(buffer);
}

std::optional<std::filesystem::path> saveArchiveDialog(
    const std::filesystem::path& initialDirectory,
    const std::filesystem::path& suggestedName) {
    wchar_t buffer[MAX_PATH] = {};
    const std::wstring suggested = suggestedName.filename().wstring();
    if (!suggested.empty()) {
        wcsncpy_s(buffer, suggested.c_str(), _TRUNCATE);
    }

    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFilter = L"HPI archive (*.hpi)\0*.hpi\0UFO archive (*.ufo)\0*.ufo\0CCX archive (*.ccx)\0*.ccx\0";
    dialog.lpstrFile = buffer;
    dialog.nMaxFile = MAX_PATH;
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    dialog.lpstrTitle = L"Save archive";
    dialog.lpstrDefExt = L"hpi";

    std::wstring initial = initialDirectory.wstring();
    if (!initial.empty()) {
        dialog.lpstrInitialDir = initial.c_str();
    }
    if (!GetSaveFileNameW(&dialog)) {
        return std::nullopt;
    }
    return std::filesystem::path(buffer);
}

std::vector<std::filesystem::path> chooseFilesDialog(
    const std::string& title,
    const std::filesystem::path& initialDirectory) {
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    std::vector<std::filesystem::path> result;

    IFileOpenDialog* dialog = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        DWORD options = 0;
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_ALLOWMULTISELECT | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_FILEMUSTEXIST);
        const std::wstring wideTitle = wideFromUtf8(title);
        if (!wideTitle.empty()) {
            dialog->SetTitle(wideTitle.c_str());
        }
        if (!initialDirectory.empty()) {
            IShellItem* folder = nullptr;
            if (SUCCEEDED(SHCreateItemFromParsingName(initialDirectory.c_str(), nullptr, IID_PPV_ARGS(&folder)))) {
                dialog->SetFolder(folder);
                folder->Release();
            }
        }
        if (SUCCEEDED(dialog->Show(nullptr))) {
            IShellItemArray* items = nullptr;
            if (SUCCEEDED(dialog->GetResults(&items))) {
                DWORD count = 0;
                items->GetCount(&count);
                for (DWORD i = 0; i < count; ++i) {
                    IShellItem* item = nullptr;
                    if (FAILED(items->GetItemAt(i, &item))) {
                        continue;
                    }
                    PWSTR path = nullptr;
                    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path != nullptr) {
                        result.emplace_back(path);
                        CoTaskMemFree(path);
                    }
                    item->Release();
                }
                items->Release();
            }
        }
        dialog->Release();
    }
    if (SUCCEEDED(initialized)) {
        CoUninitialize();
    }
    return result;
}

bool alertDialog(
    const std::string& message,
    const std::string& detail,
    const std::string& cancelLabel,
    const std::string& acceptLabel) {
    const std::wstring wideMessage = wideFromUtf8(message);
    const std::wstring wideDetail = wideFromUtf8(detail);
    const std::wstring wideCancel = wideFromUtf8(cancelLabel);
    const std::wstring wideAccept = wideFromUtf8(acceptLabel);

    // Task dialogs allow custom button names. They exist only when the program
    // uses the version 6 common controls, so look for one and fall back if it is missing.
    using TaskDialogFunction = HRESULT(WINAPI*)(const TASKDIALOGCONFIG*, int*, int*, BOOL*);
    TaskDialogFunction taskDialog = nullptr;
    if (HMODULE controls = LoadLibraryW(L"comctl32.dll")) {
        taskDialog = reinterpret_cast<TaskDialogFunction>(reinterpret_cast<void*>(GetProcAddress(controls, "TaskDialogIndirect")));
    }
    if (taskDialog != nullptr) {
        constexpr int kAccept = 1001;
        constexpr int kCancel = 1002;
        const TASKDIALOG_BUTTON buttons[] = {{kAccept, wideAccept.c_str()}, {kCancel, wideCancel.c_str()}};
        TASKDIALOGCONFIG config = {};
        config.cbSize = sizeof(config);
        config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
        config.pszWindowTitle = L"Galaxy Editor";
        config.pszMainIcon = TD_INFORMATION_ICON;
        config.pszMainInstruction = wideMessage.c_str();
        config.pszContent = wideDetail.c_str();
        config.pButtons = buttons;
        config.cButtons = 2;
        config.nDefaultButton = kAccept;
        int pressed = 0;
        if (SUCCEEDED(taskDialog(&config, &pressed, nullptr, nullptr))) {
            return pressed == kAccept;
        }
    }

    const std::wstring body = wideMessage + L"\n\n" + wideDetail + L"\n\nOK: " + wideAccept;
    return MessageBoxW(nullptr, body.c_str(), L"Galaxy Editor", MB_OKCANCEL | MB_ICONINFORMATION | MB_DEFBUTTON1) == IDOK;
}

}  // namespace galaxy
