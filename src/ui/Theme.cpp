#include "ui/Theme.hpp"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <string_view>
#include <vector>

namespace galaxy::theme {
namespace {

ImFont* gSmall = nullptr;
ImFont* gBold = nullptr;
bool gEllipsisGlyph = false;
bool gCommandGlyphs = false;

struct FontFile {
    std::filesystem::path path;
    int index = 0;
};

ImVec4 rgb(int r, int g, int b, int a = 255) {
    return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
}

ImU32 rgb32(int r, int g, int b, int a = 255) {
    return IM_COL32(r, g, b, a);
}

std::vector<FontFile> regularFonts() {
    std::vector<FontFile> fonts;
#if defined(__APPLE__)
    fonts.push_back({"/System/Library/Fonts/LucidaGrande.ttc", 0});
    fonts.push_back({"/System/Library/Fonts/Helvetica.ttc", 0});
#elif defined(_WIN32)
    const char* windir = std::getenv("WINDIR");
    const std::filesystem::path dir = std::filesystem::path(windir != nullptr ? windir : "C:\\Windows") / "Fonts";
    fonts.push_back({dir / "segoeui.ttf", 0});
    fonts.push_back({dir / "tahoma.ttf", 0});
#else
    fonts.push_back({"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 0});
    fonts.push_back({"/usr/share/fonts/TTF/DejaVuSans.ttf", 0});
    fonts.push_back({"/usr/share/fonts/dejavu/DejaVuSans.ttf", 0});
    fonts.push_back({"/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf", 0});
    fonts.push_back({"/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf", 0});
    fonts.push_back({"/usr/share/fonts/noto/NotoSans-Regular.ttf", 0});
    fonts.push_back({"/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf", 0});
#endif
    return fonts;
}

std::vector<FontFile> boldFonts() {
    std::vector<FontFile> fonts;
#if defined(__APPLE__)
    fonts.push_back({"/System/Library/Fonts/LucidaGrande.ttc", 1});
    fonts.push_back({"/System/Library/Fonts/Helvetica.ttc", 1});
#elif defined(_WIN32)
    const char* windir = std::getenv("WINDIR");
    const std::filesystem::path dir = std::filesystem::path(windir != nullptr ? windir : "C:\\Windows") / "Fonts";
    fonts.push_back({dir / "segoeuib.ttf", 0});
    fonts.push_back({dir / "tahomabd.ttf", 0});
#else
    fonts.push_back({"/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 0});
    fonts.push_back({"/usr/share/fonts/TTF/DejaVuSans-Bold.ttf", 0});
    fonts.push_back({"/usr/share/fonts/dejavu/DejaVuSans-Bold.ttf", 0});
    fonts.push_back({"/usr/share/fonts/dejavu-sans-fonts/DejaVuSans-Bold.ttf", 0});
    fonts.push_back({"/usr/share/fonts/truetype/noto/NotoSans-Bold.ttf", 0});
    fonts.push_back({"/usr/share/fonts/noto/NotoSans-Bold.ttf", 0});
    fonts.push_back({"/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf", 0});
#endif
    return fonts;
}

const ImWchar* glyphRanges() {
    static ImVector<ImWchar> ranges;
    if (ranges.empty()) {
        ImFontGlyphRangesBuilder builder;
        builder.AddRanges(ImGui::GetIO().Fonts->GetGlyphRangesDefault());
        for (const ImWchar extra : {0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2026, 0x21E7, 0x2318, 0x2325, 0x2303}) {
            builder.AddChar(extra);
        }
        builder.BuildRanges(&ranges);
    }
    return ranges.Data;
}

ImFont* addFont(const std::vector<FontFile>& candidates, float pixels) {
    for (const FontFile& file : candidates) {
        std::error_code error;
        if (!std::filesystem::is_regular_file(file.path, error)) {
            continue;
        }
        ImFontConfig config;
        config.FontNo = file.index;
        config.OversampleH = 2;
        config.OversampleV = 1;
        config.PixelSnapH = true;
        if (ImFont* font = ImGui::GetIO().Fonts->AddFontFromFileTTF(file.path.string().c_str(), pixels, &config, glyphRanges())) {
            return font;
        }
    }
    return nullptr;
}

void applyColors(ImGuiStyle& style) {
    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = rgb(28, 28, 28);
    c[ImGuiCol_TextDisabled] = rgb(140, 140, 140);
    c[ImGuiCol_WindowBg] = rgb(236, 236, 236);
    c[ImGuiCol_ChildBg] = rgb(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = rgb(248, 248, 248, 250);
    c[ImGuiCol_Border] = rgb(178, 178, 178);
    c[ImGuiCol_BorderShadow] = rgb(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = rgb(255, 255, 255);
    c[ImGuiCol_FrameBgHovered] = rgb(252, 252, 252);
    c[ImGuiCol_FrameBgActive] = rgb(255, 255, 255);
    c[ImGuiCol_TitleBg] = rgb(240, 240, 240);
    c[ImGuiCol_TitleBgActive] = rgb(218, 218, 218);
    c[ImGuiCol_TitleBgCollapsed] = rgb(240, 240, 240);
    c[ImGuiCol_MenuBarBg] = rgb(246, 246, 246);
    c[ImGuiCol_ScrollbarBg] = rgb(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = rgb(0, 0, 0, 70);
    c[ImGuiCol_ScrollbarGrabHovered] = rgb(0, 0, 0, 110);
    c[ImGuiCol_ScrollbarGrabActive] = rgb(0, 0, 0, 140);
    c[ImGuiCol_CheckMark] = rgb(30, 30, 30);
    c[ImGuiCol_SliderGrab] = rgb(150, 150, 150);
    c[ImGuiCol_SliderGrabActive] = rgb(54, 128, 236);
    c[ImGuiCol_Button] = rgb(252, 252, 252);
    c[ImGuiCol_ButtonHovered] = rgb(244, 244, 244);
    c[ImGuiCol_ButtonActive] = rgb(212, 212, 212);
    c[ImGuiCol_Header] = rgb(186, 208, 240);
    c[ImGuiCol_HeaderHovered] = rgb(214, 228, 247);
    c[ImGuiCol_HeaderActive] = rgb(164, 193, 236);
    c[ImGuiCol_Separator] = rgb(196, 196, 196);
    c[ImGuiCol_SeparatorHovered] = rgb(150, 150, 150);
    c[ImGuiCol_SeparatorActive] = rgb(54, 128, 236);
    c[ImGuiCol_ResizeGrip] = rgb(0, 0, 0, 25);
    c[ImGuiCol_ResizeGripHovered] = rgb(0, 0, 0, 60);
    c[ImGuiCol_ResizeGripActive] = rgb(0, 0, 0, 90);
    c[ImGuiCol_Tab] = rgb(228, 228, 228);
    c[ImGuiCol_TabHovered] = rgb(244, 244, 244);
    c[ImGuiCol_TabSelected] = rgb(255, 255, 255);
    c[ImGuiCol_TabSelectedOverline] = rgb(54, 128, 236);
    c[ImGuiCol_TabDimmed] = rgb(228, 228, 228);
    c[ImGuiCol_TabDimmedSelected] = rgb(248, 248, 248);
    c[ImGuiCol_TabDimmedSelectedOverline] = rgb(160, 160, 160);
    c[ImGuiCol_PlotLines] = rgb(90, 90, 90);
    c[ImGuiCol_PlotLinesHovered] = rgb(54, 128, 236);
    c[ImGuiCol_PlotHistogram] = rgb(54, 128, 236);
    c[ImGuiCol_PlotHistogramHovered] = rgb(36, 106, 212);
    c[ImGuiCol_TableHeaderBg] = rgb(236, 236, 236);
    c[ImGuiCol_TableBorderStrong] = rgb(190, 190, 190);
    c[ImGuiCol_TableBorderLight] = rgb(220, 220, 220);
    c[ImGuiCol_TableRowBg] = rgb(255, 255, 255);
    c[ImGuiCol_TableRowBgAlt] = rgb(243, 246, 250);
    c[ImGuiCol_TextLink] = rgb(36, 106, 212);
    c[ImGuiCol_TextSelectedBg] = rgb(164, 198, 247, 200);
    c[ImGuiCol_DragDropTarget] = rgb(54, 128, 236);
    c[ImGuiCol_NavCursor] = rgb(93, 154, 232);
    c[ImGuiCol_NavWindowingHighlight] = rgb(255, 255, 255, 180);
    c[ImGuiCol_NavWindowingDimBg] = rgb(0, 0, 0, 40);
    c[ImGuiCol_ModalWindowDimBg] = rgb(0, 0, 0, 40);
}

void applySizes(ImGuiStyle& style) {
    style.WindowPadding = ImVec2(12.0f, 10.0f);
    style.FramePadding = ImVec2(10.0f, 4.0f);
    style.CellPadding = ImVec2(6.0f, 3.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
    style.IndentSpacing = 18.0f;
    style.ScrollbarSize = 11.0f;
    style.GrabMinSize = 11.0f;
    style.WindowRounding = 5.0f;
    style.ChildRounding = 0.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 5.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 4.0f;
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.TabBorderSize = 0.0f;
    style.TabBarBorderSize = 1.0f;
    style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
    style.WindowMenuButtonPosition = ImGuiDir_None;
    style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
    style.SeparatorTextBorderSize = 1.0f;
    style.SeparatorTextAlign = ImVec2(0.0f, 0.5f);
    style.SeparatorTextPadding = ImVec2(0.0f, 6.0f);
    style.DisabledAlpha = 0.45f;
}

bool itemDisabled() {
    return (ImGui::GetCurrentContext()->CurrentItemFlags & ImGuiItemFlags_Disabled) != 0;
}

}  // namespace

void setup(float contentScale) {
    ImGuiIO& io = ImGui::GetIO();
    const float raster = std::max(contentScale, 1.0f);
#if defined(__APPLE__)
    // GLFW lays out macOS windows in points, so only the font texture needs the Retina scale.
    const float uiScale = 1.0f;
#else
    const float uiScale = raster;
#endif
    io.Fonts->Clear();
    ImFont* regular = addFont(regularFonts(), 13.0f * raster);
    if (regular == nullptr) {
        ImFontConfig config;
        config.SizePixels = 13.0f * raster;
        regular = io.Fonts->AddFontDefault(&config);
        gSmall = regular;
        gBold = regular;
    } else {
        gSmall = addFont(regularFonts(), 11.0f * raster);
        gBold = addFont(boldFonts(), 13.0f * raster);
        if (gSmall == nullptr) {
            gSmall = regular;
        }
        if (gBold == nullptr) {
            gBold = regular;
        }
    }
    io.FontDefault = regular;
    io.FontGlobalScale = uiScale / raster;
    io.Fonts->Build();
    gEllipsisGlyph = regular->FindGlyphNoFallback(0x2026) != nullptr;
    gCommandGlyphs = regular->FindGlyphNoFallback(0x2318) != nullptr && regular->FindGlyphNoFallback(0x21E7) != nullptr
        && regular->FindGlyphNoFallback(0x2325) != nullptr;

    ImGuiStyle& style = ImGui::GetStyle();
    style = ImGuiStyle();
    applySizes(style);
    applyColors(style);
    style.ScaleAllSizes(uiScale);
}

ImFont* smallFont() {
    return gSmall != nullptr ? gSmall : ImGui::GetFont();
}

ImFont* boldFont() {
    return gBold != nullptr ? gBold : ImGui::GetFont();
}

const char* ellipsis() {
    return gEllipsisGlyph ? "\xE2\x80\xA6" : "...";
}

std::string withEllipsis(const char* label) {
    return std::string(label) + ellipsis();
}

std::string shortcut(const char* keys) {
    std::string_view rest(keys);
    bool shift = false;
    bool alt = false;
    while (true) {
        const std::size_t plus = rest.find('+');
        if (plus == std::string_view::npos || plus + 1 >= rest.size()) {
            break;
        }
        const std::string_view modifier = rest.substr(0, plus);
        if (modifier == "Shift") {
            shift = true;
        } else if (modifier == "Alt") {
            alt = true;
        }
        rest.remove_prefix(plus + 1);
    }
    const std::string key(rest);
#if defined(__APPLE__)
    if (gCommandGlyphs) {
        std::string text;
        if (alt) {
            text += "\xE2\x8C\xA5";
        }
        if (shift) {
            text += "\xE2\x87\xA7";
        }
        text += "\xE2\x8C\x98";
        return text + key;
    }
    return std::string("Cmd+") + (alt ? "Option+" : "") + (shift ? "Shift+" : "") + key;
#else
    return std::string("Ctrl+") + (alt ? "Alt+" : "") + (shift ? "Shift+" : "") + key;
#endif
}

const char* quitLabel() {
#if defined(__APPLE__)
    return "Quit Galaxy Editor";
#else
    return "Exit";
#endif
}

ImU32 canvasColor() {
    return rgb32(78, 80, 84);
}

ImU32 accentColor() {
    return rgb32(54, 128, 236);
}

bool toggleButton(const char* label, bool on, const ImVec2& size) {
    if (on) {
        ImGui::PushStyleColor(ImGuiCol_Button, rgb(104, 104, 104));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, rgb(116, 116, 116));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, rgb(86, 86, 86));
        ImGui::PushStyleColor(ImGuiCol_Border, rgb(70, 70, 70));
        ImGui::PushStyleColor(ImGuiCol_Text, rgb(255, 255, 255));
    }
    const bool pressed = ImGui::Button(label, size);
    if (on) {
        ImGui::PopStyleColor(5);
    }
    return pressed;
}

bool defaultButton(const char* label, const ImVec2& size, bool acceptReturn) {
    const bool disabled = itemDisabled();
    if (!disabled) {
        ImGui::PushStyleColor(ImGuiCol_Button, rgb(54, 128, 236));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, rgb(70, 142, 244));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, rgb(36, 106, 212));
        ImGui::PushStyleColor(ImGuiCol_Border, rgb(28, 92, 190));
        ImGui::PushStyleColor(ImGuiCol_Text, rgb(255, 255, 255));
    }
    bool pressed = ImGui::Button(label, size);
    if (!disabled) {
        ImGui::PopStyleColor(5);
        if (acceptReturn && !pressed && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::IsAnyItemActive()
            && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))) {
            pressed = true;
        }
    }
    return pressed;
}

float pushButtonWidth(const char* label) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float text = ImGui::CalcTextSize(label, nullptr, true).x + style.FramePadding.x * 2.0f + 16.0f * style.FramePadding.x / 10.0f;
    return std::max(text, 84.0f * ImGui::GetFontSize() / 13.0f);
}

void alignRight(float width) {
    const float avail = ImGui::GetContentRegionAvail().x;
    if (avail > width) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - width);
    }
}

bool segmented(const char* id, const char* const* labels, int count, int& current, bool fill) {
    if (count <= 0) {
        return false;
    }
    ImGui::PushID(id);
    const ImGuiStyle& style = ImGui::GetStyle();
    const float height = ImGui::GetFrameHeight();
    std::vector<float> widths(static_cast<std::size_t>(count));
    float total = 0.0f;
    for (int i = 0; i < count; ++i) {
        widths[static_cast<std::size_t>(i)] = ImGui::CalcTextSize(labels[i], nullptr, true).x + style.FramePadding.x * 2.0f + 6.0f;
        total += widths[static_cast<std::size_t>(i)];
    }
    const float avail = ImGui::GetContentRegionAvail().x;
    float offset = 0.0f;
    if (total > avail || (fill && total < avail)) {
        const float ratio = avail / total;
        for (float& width : widths) {
            width *= ratio;
        }
        total = avail;
    } else {
        offset = std::floor((avail - total) * 0.5f);
    }
    ImVec2 origin = ImGui::GetCursorScreenPos();
    origin.x += offset;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float rounding = style.FrameRounding;
    bool changed = false;
    float x = origin.x;
    for (int i = 0; i < count; ++i) {
        const float width = widths[static_cast<std::size_t>(i)];
        ImGui::PushID(i);
        ImGui::SetCursorScreenPos(ImVec2(x, origin.y));
        if (ImGui::InvisibleButton("segment", ImVec2(width, height)) && current != i) {
            current = i;
            changed = true;
        }
        const bool hovered = ImGui::IsItemHovered();
        const bool held = ImGui::IsItemActive();
        const bool selected = current == i;
        ImU32 fillColor = rgb32(252, 252, 252);
        if (selected) {
            fillColor = held ? rgb32(36, 106, 212) : accentColor();
        } else if (held) {
            fillColor = rgb32(212, 212, 212);
        } else if (hovered) {
            fillColor = rgb32(244, 244, 244);
        }
        ImDrawFlags corners = ImDrawFlags_RoundCornersNone;
        if (count == 1) {
            corners = ImDrawFlags_RoundCornersAll;
        } else if (i == 0) {
            corners = ImDrawFlags_RoundCornersLeft;
        } else if (i == count - 1) {
            corners = ImDrawFlags_RoundCornersRight;
        }
        const ImVec2 min(x, origin.y);
        const ImVec2 max(x + width, origin.y + height);
        draw->AddRectFilled(min, max, fillColor, rounding, corners);
        if (i > 0 && !selected && current != i - 1) {
            draw->AddLine(ImVec2(x, origin.y + 1.0f), ImVec2(x, origin.y + height - 1.0f), ImGui::GetColorU32(ImGuiCol_Border));
        }
        const char* end = ImGui::FindRenderedTextEnd(labels[i]);
        const ImVec2 text = ImGui::CalcTextSize(labels[i], end);
        draw->PushClipRect(min, max, true);
        draw->AddText(
            ImVec2(x + std::floor((width - text.x) * 0.5f), origin.y + std::floor((height - text.y) * 0.5f)),
            selected ? rgb32(255, 255, 255) : ImGui::GetColorU32(ImGuiCol_Text),
            labels[i],
            end);
        draw->PopClipRect();
        ImGui::PopID();
        x += width;
    }
    draw->AddRect(origin, ImVec2(origin.x + total, origin.y + height), ImGui::GetColorU32(ImGuiCol_Border), rounding);
    ImGui::SetCursorScreenPos(ImVec2(origin.x - offset, origin.y));
    ImGui::Dummy(ImVec2(total + offset, height));
    ImGui::PopID();
    return changed;
}

void toolbarBackground() {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    const ImVec2 max(min.x + size.x, min.y + size.y);
    draw->AddRectFilledMultiColor(min, max, rgb32(234, 234, 234), rgb32(234, 234, 234), rgb32(208, 208, 208), rgb32(208, 208, 208));
    draw->AddLine(ImVec2(min.x, max.y - 1.0f), ImVec2(max.x, max.y - 1.0f), rgb32(160, 160, 160));
}

void toolbarSeparator() {
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    ImGui::SameLine(0.0f, gap + 2.0f);
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float height = ImGui::GetFrameHeight();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(at.x, at.y + 3.0f), ImVec2(at.x, at.y + height - 3.0f), rgb32(168, 168, 168));
    ImGui::Dummy(ImVec2(1.0f, height));
    ImGui::SameLine(0.0f, gap + 2.0f);
}

void bottomBarBackground() {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    const ImVec2 max(min.x + size.x, min.y + size.y);
    draw->AddRectFilledMultiColor(min, max, rgb32(228, 228, 228), rgb32(228, 228, 228), rgb32(212, 212, 212), rgb32(212, 212, 212));
    draw->AddLine(min, ImVec2(max.x, min.y), rgb32(170, 170, 170));
}

void sidebarBackground() {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    const ImVec2 max(min.x + size.x, min.y + size.y);
    draw->AddRectFilled(min, max, rgb32(223, 229, 236));
    draw->AddLine(ImVec2(max.x - 1.0f, min.y), ImVec2(max.x - 1.0f, max.y), rgb32(170, 170, 170));
}

void sourceListTitle(const char* text) {
    ImGui::PushFont(smallFont());
    ImGui::PushStyleColor(ImGuiCol_Text, rgb(112, 120, 130));
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

ImGuiWindowFlags alertFlags() {
    return ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
}

void alertText(const char* message, const char* informative) {
    const float width = 380.0f * ImGui::GetFontSize() / 13.0f;
    ImGui::Dummy(ImVec2(width, 4.0f));
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
    ImGui::PushFont(boldFont());
    ImGui::TextWrapped("%s", message);
    ImGui::PopFont();
    if (informative != nullptr && informative[0] != '\0') {
        ImGui::Spacing();
        ImGui::PushFont(smallFont());
        ImGui::TextWrapped("%s", informative);
        ImGui::PopFont();
    }
    ImGui::PopTextWrapPos();
    ImGui::Dummy(ImVec2(width, 12.0f));
}

void helpText(const char* text) {
    ImGui::PushFont(smallFont());
    ImGui::PushStyleColor(ImGuiCol_Text, rgb(110, 110, 110));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

}  // namespace galaxy::theme
