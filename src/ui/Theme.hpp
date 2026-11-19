#pragma once

#include <imgui.h>

#include <string>
#include <vector>

namespace galaxy::theme {

// Light window body, Lucida Grande 13 pt where the system has it, and the
// control sizes of the 2013 OS X guidelines. Call once after ImGui::CreateContext.
void setup(float contentScale);

// Switches every color between the light and the dark look, at any time after
// setup. The next frame is drawn in the new look.
void setDark(bool dark);
bool dark();

// How the frosted glass panels (toolbar, sidebar, minimap) are tinted and edged.
struct GlassStyle {
    ImVec4 tint;          // the color the blurred map is pushed toward
    float amount;         // how far: 0 keeps the map colors, 1 is the plain tint
    float saturation;     // color strength applied before the tint
    ImU32 border;         // line along the edge of a bar
    ImU32 outline;        // outline of the floating minimap
    ImU32 sheenTop;       // soft light-to-shade overlay on the toolbar
    ImU32 sheenBottom;
    ImVec4 titleText;     // minimap title
    ImU32 closeMark;      // minimap close mark, and when the pointer is over it
    ImU32 closeMarkHot;
};
const GlassStyle& glassStyle();
// Secondary text, such as the status bar.
ImVec4 secondaryTextColor();

// 11 pt system font for informative and status text. Never null after setup.
ImFont* smallFont();
// 13 pt bold for alert messages and titles. Falls back to the regular font.
ImFont* boldFont();

// 10 pt for the labels under toolbar icons.
ImFont* toolbarLabelFont();
// A monospaced font for code, loaded at each of these sizes in points so every
// size stays sharp: SF Mono or Menlo on macOS, Consolas on Windows, DejaVu Sans
// Mono on Linux, or the regular font when none of those is installed.
const std::vector<int>& codeFontSizes();
// The loaded size closest to points.
ImFont* codeFont(int points);

// Colours for code, after Xcode's Default (Light) theme in light mode and its
// Midnight theme in dark mode.
struct CodeColors {
    ImU32 background;
    ImU32 plain;
    ImU32 comment;
    ImU32 keyword;
    ImU32 preprocessor;
    ImU32 number;
    ImU32 function;
    ImU32 property;
    ImU32 string;
    ImU32 selection;
    ImU32 caret;
};
const CodeColors& codeColors();
// One layout unit: 1.0 at the 13 pt system size.
float unit();
float toolbarItemHeight();
float toolbarHeight();
float toolbarGap();

enum class ToolIcon {
    Select,
    Cut,
    Copy,
    CopyHeights,
    Paste,
    ModifyHeight,
    Blend,
    HeightMap,
    View3D,
    Undo,
    Redo,
    Opacity,
    HeightOffset,
    Delete,
    HpiEditor,
    NewArchive,
    OpenArchive,
    SaveArchive,
    SaveArchiveAs,
    Import,
    Export,
    NewFolder,
};

// A freestanding toolbar item: a 32 pt icon with its label underneath. When on is
// true the item shows the pressed look of a two-state control. widthLabel reserves
// room for the longest label an item can show so the toolbar does not shift.
bool toolbarItem(const char* id, ToolIcon icon, const char* label, bool on = false, const char* widthLabel = nullptr);

// A small round button that sits over the top-right corner of a picture while the
// pointer is over that picture. The caller finds out whether the pointer is over
// the button and decides what a click means. Hovering it needs a tooltip.
enum class BadgeIcon {
    AddToFavorites,
    Trash,
};
struct BadgeRect {
    ImVec2 min;
    ImVec2 max;
};
// The button's place for a picture that covers itemMin to itemMax.
BadgeRect badgeRect(ImVec2 itemMin, ImVec2 itemMax);
// hot is true while the pointer is over the button itself.
void drawBadge(const BadgeRect& rect, BadgeIcon icon, bool hot);

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

// Draws a picture centered on center, roughly size points across, in color.
using GlyphPainter = void (*)(ImDrawList* draw, ImVec2 center, float size, ImU32 color);
// A segmented control whose segments hold pictures. selected is the segment that looks pressed,
// or -1 for a control with no state, like Back and Forward. enabled may be null. tips may be null.
// Returns the segment that was clicked, or -1.
int iconSegmented(const char* id, const GlyphPainter* glyphs, int count, int selected, const bool* enabled, const char* const* tips);

// A row of picture tabs like the navigator bar at the top of Xcode's left sidebar: plain glyphs
// spread evenly across the width, the current one in the accent color, with a hairline under the
// row. tips name each tab on hover. Returns true when the selection changed.
bool navigatorBar(const char* id, const GlyphPainter* glyphs, const char* const* tips, int count, int& current);

// Height of the row of document tabs under the toolbar.
float documentTabsHeight();
// One document tab, as in Xcode's editor tabs: equal widths across the row, the
// current tab lighter than the rest, the name centered, and a close button at the
// left end. The close button shows on the current tab and on the tab under the
// pointer; a tab with unsaved changes shows a dot there otherwise. tooltip may be null.
struct DocumentTabEvent {
    bool select = false;
    bool close = false;
};
struct DocumentTab {
    const char* title = "";
    const char* tooltip = nullptr;
    bool dirty = false;
};
// Draws the whole row in the current window and reports what was clicked. A middle
// click on a tab asks to close it. closeTip names the close button. When glass is
// true the caller has already painted frosted glass behind the row, so the row
// leaves its background clear and tints the tabs lightly instead of filling them.
DocumentTabEvent documentTabs(const char* id, const DocumentTab* tabs, int count, int current, int& eventTab, const char* closeTip,
                              bool glass = false);

void toolbarBackground();
void toolbarSeparator();
void bottomBarBackground();
void sidebarBackground();
// Small bold gray title inside the sidebar.
void sourceListTitle(const char* text);
// The same title, with a trailing reload button. The clockwise arrow is the
// standard reload symbol: quiet until the pointer is over it, so a long list of
// archives stays readable. Returns true when that button is clicked. The button
// stays the current item, so the caller can add its tooltip.
bool sourceListHeading(const char* title, const char* reloadId);
// Window flags for an alert: no title bar, sized to the message.
ImGuiWindowFlags alertFlags();
// Bold message plus small informative text, wrapped to the alert width, then a gap before the buttons.
void alertText(const char* message, const char* informative = nullptr);
// Small gray help text that wraps.
void helpText(const char* text);

}  // namespace galaxy::theme
