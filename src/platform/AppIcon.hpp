#pragma once

struct GLFWwindow;

namespace galaxy {

// Gives the running application its icon: the Dock and Cmd-Tab icon on macOS,
// and the title bar, taskbar and Alt-Tab icon on Windows and Linux. Call it
// after the window exists. Wayland takes the icon from the installed desktop
// entry instead, so there it does nothing.
void applyApplicationIcon(GLFWwindow* window);

}  // namespace galaxy
