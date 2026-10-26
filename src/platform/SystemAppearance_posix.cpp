#include "platform/SystemAppearance.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

namespace galaxy {
namespace {

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

// Output of a command, or an empty string if it is missing or fails.
std::string run(const char* command) {
    std::string output;
    if (FILE* pipe = popen(command, "r")) {
        char buffer[256];
        while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            output += buffer;
        }
        if (pclose(pipe) != 0) {
            output.clear();
        }
    }
    return lower(output);
}

// KDE keeps its color scheme name in kdeglobals.
std::string kdeColorScheme() {
    std::string home;
    if (const char* config = std::getenv("XDG_CONFIG_HOME"); config != nullptr && config[0] != '\0') {
        home = config;
    } else if (const char* user = std::getenv("HOME"); user != nullptr && user[0] != '\0') {
        home = std::string(user) + "/.config";
    } else {
        return {};
    }
    std::ifstream in(home + "/kdeglobals");
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("ColorScheme=", 0) == 0) {
            return lower(line.substr(12));
        }
    }
    return {};
}

}  // namespace

bool systemPrefersDarkMode() {
    // GNOME 42 and later (and most current desktops through the settings portal) report a preference.
    const std::string scheme = run("gsettings get org.gnome.desktop.interface color-scheme 2>/dev/null");
    if (scheme.find("prefer-dark") != std::string::npos) {
        return true;
    }
    if (scheme.find("prefer-light") != std::string::npos) {
        return false;
    }
    // Older GNOME and GTK based desktops use the theme name.
    const std::string gtkTheme = run("gsettings get org.gnome.desktop.interface gtk-theme 2>/dev/null");
    if (gtkTheme.find("dark") != std::string::npos) {
        return true;
    }
    if (!gtkTheme.empty()) {
        return false;
    }
    if (const char* forced = std::getenv("GTK_THEME"); forced != nullptr && lower(forced).find("dark") != std::string::npos) {
        return true;
    }
    return kdeColorScheme().find("dark") != std::string::npos;
}

void applyNativeAppearance(int) {}

}  // namespace galaxy
