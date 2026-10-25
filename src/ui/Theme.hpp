#pragma once

#include <imgui.h>

#include <string>

namespace galaxy::theme {

// Light window body, Lucida Grande 13 pt where the system has it, and the
// control sizes of the 2013 OS X guidelines. Call once after ImGui::CreateContext.
void setup(float contentScale);

// 11 pt system font for informative and status text. Never null after setup.
ImFont* smallFont();
// 13 pt bold for alert messages and titles. Falls back to the regular font.
ImFont* boldFont();

// Menu and button labels that open a dialog end with this.
const char* ellipsis();
std::string withEllipsis(const char* label);
// keys is written like "S" or "Shift+S". Shows Command symbols on macOS.
std::string shortcut(const char* keys);
const char* quitLabel();

ImU32 canvasColor();
ImU32 accentColor();

bool toggleButton(const char* label, bool on, const ImVec2& size = ImVec2(0.0f, 0.0f));
// Colored button that also answers Return. Leave acceptReturn off when the action is not safe.
bool defaultButton(const char* label, const ImVec2& size = ImVec2(0.0f, 0.0f), bool acceptReturn = true);
// Width that fits label text with the standard push button padding.
float pushButtonWidth(const char* label);
// Moves the cursor so buttons of this total width end at the right edge.
void alignRight(float width);

// Segmented control used in place of tab bars. Returns true when the selection changed.
bool segmented(const char* id, const char* const* labels, int count, int& current, bool fill = false);

void toolbarBackground();
void toolbarSeparator();
void bottomBarBackground();
void sidebarBackground();
// Small bold gray title inside the sidebar.
void sourceListTitle(const char* text);
// Window flags for an alert: no title bar, sized to the message.
ImGuiWindowFlags alertFlags();
// Bold message plus small informative text, wrapped to the alert width, then a gap before the buttons.
void alertText(const char* message, const char* informative = nullptr);
// Small gray help text that wraps.
void helpText(const char* text);

}  // namespace galaxy::theme
