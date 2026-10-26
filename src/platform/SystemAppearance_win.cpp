#include "platform/SystemAppearance.hpp"

#include <windows.h>

namespace galaxy {

bool systemPrefersDarkMode() {
    // Windows 10 and 11 keep this switch in the user's registry hive. 0 means apps use the dark theme.
    DWORD value = 1;
    DWORD size = sizeof(value);
    const LSTATUS status = RegGetValueW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme",
        RRF_RT_REG_DWORD,
        nullptr,
        &value,
        &size);
    return status == ERROR_SUCCESS && value == 0;
}

void applyNativeAppearance(int) {}

}  // namespace galaxy
