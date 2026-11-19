#include "ui/Theme.hpp"

#include "core/I18n.hpp"
#include "ui/EmbeddedData.hpp"

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
ImFont* gLabel = nullptr;
std::vector<ImFont*> gCode;
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

// Colors for the parts the theme draws itself. ImGui's own widgets use the style colors.
struct Colors {
    ImU32 toolbarTop;
    ImU32 toolbarBottom;
    ImU32 toolbarLine;
    ImU32 separatorShade;
    ImU32 separatorLight;
    ImU32 bottomTop;
    ImU32 bottomBottom;
    ImU32 bottomLine;
    ImU32 sidebar;
    ImU32 sidebarLine;
    ImVec4 sourceTitle;
    ImVec4 help;
    ImVec4 secondary;
    ImVec4 toggleOn;
    ImVec4 toggleOnHover;
    ImVec4 toggleOnActive;
    ImVec4 toggleOnBorder;
    ImU32 segmentFill;
    ImU32 segmentHover;
    ImU32 segmentHeld;
    ImU32 canvas;
    GlassStyle glass;
};

const Colors& lightColors() {
    static const Colors colors{
        rgb32(234, 234, 234), rgb32(208, 208, 208), rgb32(160, 160, 160),
        rgb32(160, 160, 160), rgb32(250, 250, 250, 200),
        rgb32(228, 228, 228), rgb32(212, 212, 212), rgb32(170, 170, 170),
        rgb32(223, 229, 236), rgb32(170, 170, 170),
        rgb(112, 120, 130), rgb(110, 110, 110), rgb(69, 69, 69),
        rgb(104, 104, 104), rgb(116, 116, 116), rgb(86, 86, 86), rgb(70, 70, 70),
        rgb32(252, 252, 252), rgb32(244, 244, 244), rgb32(212, 212, 212),
        rgb32(78, 80, 84),
        GlassStyle{
            ImVec4(1.0f, 1.0f, 1.0f, 1.0f), 0.52f, 1.5f,
            rgb32(150, 154, 160), rgb32(120, 124, 130),
            rgb32(255, 255, 255, 70), rgb32(0, 0, 0, 22),
            rgb(51, 54, 59), rgb32(90, 94, 100), rgb32(20, 20, 22)},
    };
    return colors;
}

const Colors& darkColors() {
    static const Colors colors{
        rgb32(68, 68, 71), rgb32(52, 52, 55), rgb32(20, 20, 22),
        rgb32(20, 20, 22), rgb32(255, 255, 255, 30),
        rgb32(54, 54, 57), rgb32(46, 46, 49), rgb32(20, 20, 22),
        rgb32(36, 39, 44), rgb32(18, 18, 20),
        rgb(150, 158, 170), rgb(160, 160, 164), rgb(178, 178, 182),
        rgb(120, 120, 126), rgb(132, 132, 138), rgb(100, 100, 106), rgb(158, 158, 164),
        rgb32(72, 72, 76), rgb32(84, 84, 88), rgb32(58, 58, 62),
        rgb32(30, 31, 34),
        GlassStyle{
            ImVec4(0.085f, 0.09f, 0.105f, 1.0f), 0.62f, 1.4f,
            rgb32(14, 14, 16), rgb32(104, 106, 112),
            rgb32(255, 255, 255, 24), rgb32(0, 0, 0, 40),
            rgb(232, 232, 234), rgb32(176, 180, 186), rgb32(255, 255, 255)},
    };
    return colors;
}

bool gDark = false;

const Colors& colors() {
    return gDark ? darkColors() : lightColors();
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

std::vector<FontFile> codeFontFiles() {
    std::vector<FontFile> fonts;
#if defined(__APPLE__)
    fonts.push_back({"/System/Library/Fonts/SFNSMono.ttf", 0});
    fonts.push_back({"/System/Library/Fonts/Menlo.ttc", 0});
#elif defined(_WIN32)
    const char* windir = std::getenv("WINDIR");
    const std::filesystem::path dir = std::filesystem::path(windir != nullptr ? windir : "C:\\Windows") / "Fonts";
    fonts.push_back({dir / "consola.ttf", 0});
    fonts.push_back({dir / "cour.ttf", 0});
#else
    fonts.push_back({"/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", 0});
    fonts.push_back({"/usr/share/fonts/TTF/DejaVuSansMono.ttf", 0});
    fonts.push_back({"/usr/share/fonts/dejavu/DejaVuSansMono.ttf", 0});
    fonts.push_back({"/usr/share/fonts/dejavu-sans-mono-fonts/DejaVuSansMono.ttf", 0});
    fonts.push_back({"/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf", 0});
    fonts.push_back({"/usr/share/fonts/truetype/noto/NotoSansMono-Regular.ttf", 0});
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
        // Latin Extended-A and Cyrillic cover the European locales. The
        // translation catalog adds the Chinese and Korean characters it uses.
        static const ImWchar blocks[] = {
            0x0100, 0x017F, 0x0400, 0x04FF, 0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2022, 0x2026, 0x2026, 0x21E7, 0x21E7, 0x2303, 0x2303, 0x2318, 0x2318, 0x2325, 0x2325, 0};
        builder.AddRanges(blocks);
        builder.AddText(translationCorpus().c_str());
        builder.BuildRanges(&ranges);
    }
    return ranges.Data;
}

void mergeEmbedded(const unsigned char* data, unsigned long size, float pixels) {
    if (data == nullptr || size == 0 || size > 16u * 1024u * 1024u) {
        return;
    }
    ImFontConfig config;
    config.MergeMode = true;
    config.FontDataOwnedByAtlas = false;
    config.PixelSnapH = true;
    config.OversampleH = 1;
    config.OversampleV = 1;
    ImGui::GetIO().Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(data), static_cast<int>(size), pixels, &config, glyphRanges());
}

// The system font draws the Latin UI. These subsets fill any glyph it lacks,
// including Chinese and Korean, so a language change does not rebuild the atlas.
void mergeFallback(float pixels) {
    mergeEmbedded(kGalaxyLatinData, kGalaxyLatinSize, pixels);
    mergeEmbedded(kGalaxyCjkData, kGalaxyCjkSize, pixels);
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

void applyLightColors(ImGuiStyle& style) {
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

void applyDarkColors(ImGuiStyle& style) {
    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = rgb(232, 232, 232);
    c[ImGuiCol_TextDisabled] = rgb(128, 128, 128);
    c[ImGuiCol_WindowBg] = rgb(38, 38, 40);
    c[ImGuiCol_ChildBg] = rgb(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = rgb(46, 46, 49, 250);
    c[ImGuiCol_Border] = rgb(78, 78, 82);
    c[ImGuiCol_BorderShadow] = rgb(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = rgb(26, 26, 28);
    c[ImGuiCol_FrameBgHovered] = rgb(32, 32, 34);
    c[ImGuiCol_FrameBgActive] = rgb(26, 26, 28);
    c[ImGuiCol_TitleBg] = rgb(44, 44, 46);
    c[ImGuiCol_TitleBgActive] = rgb(58, 58, 61);
    c[ImGuiCol_TitleBgCollapsed] = rgb(44, 44, 46);
    c[ImGuiCol_MenuBarBg] = rgb(46, 46, 48);
    c[ImGuiCol_ScrollbarBg] = rgb(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = rgb(255, 255, 255, 70);
    c[ImGuiCol_ScrollbarGrabHovered] = rgb(255, 255, 255, 110);
    c[ImGuiCol_ScrollbarGrabActive] = rgb(255, 255, 255, 140);
    c[ImGuiCol_CheckMark] = rgb(235, 235, 235);
    c[ImGuiCol_SliderGrab] = rgb(150, 150, 155);
    c[ImGuiCol_SliderGrabActive] = rgb(54, 128, 236);
    c[ImGuiCol_Button] = rgb(70, 70, 74);
    c[ImGuiCol_ButtonHovered] = rgb(82, 82, 86);
    c[ImGuiCol_ButtonActive] = rgb(56, 56, 60);
    c[ImGuiCol_Header] = rgb(46, 84, 150);
    c[ImGuiCol_HeaderHovered] = rgb(58, 100, 172);
    c[ImGuiCol_HeaderActive] = rgb(40, 76, 138);
    c[ImGuiCol_Separator] = rgb(72, 72, 76);
    c[ImGuiCol_SeparatorHovered] = rgb(130, 130, 135);
    c[ImGuiCol_SeparatorActive] = rgb(54, 128, 236);
    c[ImGuiCol_ResizeGrip] = rgb(255, 255, 255, 25);
    c[ImGuiCol_ResizeGripHovered] = rgb(255, 255, 255, 60);
    c[ImGuiCol_ResizeGripActive] = rgb(255, 255, 255, 90);
    c[ImGuiCol_Tab] = rgb(52, 52, 55);
    c[ImGuiCol_TabHovered] = rgb(70, 70, 74);
    c[ImGuiCol_TabSelected] = rgb(30, 30, 32);
    c[ImGuiCol_TabSelectedOverline] = rgb(54, 128, 236);
    c[ImGuiCol_TabDimmed] = rgb(52, 52, 55);
    c[ImGuiCol_TabDimmedSelected] = rgb(44, 44, 46);
    c[ImGuiCol_TabDimmedSelectedOverline] = rgb(120, 120, 125);
    c[ImGuiCol_PlotLines] = rgb(170, 170, 175);
    c[ImGuiCol_PlotLinesHovered] = rgb(54, 128, 236);
    c[ImGuiCol_PlotHistogram] = rgb(54, 128, 236);
    c[ImGuiCol_PlotHistogramHovered] = rgb(70, 142, 244);
    c[ImGuiCol_TableHeaderBg] = rgb(46, 46, 48);
    c[ImGuiCol_TableBorderStrong] = rgb(84, 84, 88);
    c[ImGuiCol_TableBorderLight] = rgb(62, 62, 66);
    c[ImGuiCol_TableRowBg] = rgb(30, 30, 32);
    c[ImGuiCol_TableRowBgAlt] = rgb(36, 36, 39);
    c[ImGuiCol_TextLink] = rgb(100, 160, 250);
    c[ImGuiCol_TextSelectedBg] = rgb(54, 110, 206, 170);
    c[ImGuiCol_DragDropTarget] = rgb(54, 128, 236);
    c[ImGuiCol_NavCursor] = rgb(93, 154, 232);
    c[ImGuiCol_NavWindowingHighlight] = rgb(255, 255, 255, 120);
    c[ImGuiCol_NavWindowingDimBg] = rgb(0, 0, 0, 100);
    c[ImGuiCol_ModalWindowDimBg] = rgb(0, 0, 0, 120);
}

void applyColors(ImGuiStyle& style) {
    if (gDark) {
        applyDarkColors(style);
    } else {
        applyLightColors(style);
    }
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
        mergeFallback(13.0f * raster);
        gSmall = regular;
        gBold = regular;
        gLabel = regular;
    } else {
        mergeFallback(13.0f * raster);
        gSmall = addFont(regularFonts(), 11.0f * raster);
        if (gSmall != nullptr) {
            mergeFallback(11.0f * raster);
        }
        gBold = addFont(boldFonts(), 13.0f * raster);
        if (gBold != nullptr) {
            mergeFallback(13.0f * raster);
        }
        gLabel = addFont(regularFonts(), 10.0f * raster);
        if (gLabel != nullptr) {
            mergeFallback(10.0f * raster);
        }
        if (gLabel == nullptr) {
            gLabel = regular;
        }
        if (gSmall == nullptr) {
            gSmall = regular;
        }
        if (gBold == nullptr) {
            gBold = regular;
        }
    }
    // Code takes Latin text only, so the many sizes stay small in the atlas.
    gCode.clear();
    for (const int points : codeFontSizes()) {
        ImFont* code = nullptr;
        for (const FontFile& file : codeFontFiles()) {
            std::error_code error;
            if (!std::filesystem::is_regular_file(file.path, error)) {
                continue;
            }
            ImFontConfig config;
            config.FontNo = file.index;
            config.OversampleH = 2;
            config.OversampleV = 1;
            config.PixelSnapH = true;
            code = io.Fonts->AddFontFromFileTTF(file.path.string().c_str(), static_cast<float>(points) * raster, &config,
                                                io.Fonts->GetGlyphRangesDefault());
            if (code != nullptr) {
                break;
            }
        }
        gCode.push_back(code != nullptr ? code : regular);
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

void setDark(bool dark) {
    gDark = dark;
    applyColors(ImGui::GetStyle());
}

bool dark() {
    return gDark;
}

const GlassStyle& glassStyle() {
    return colors().glass;
}

ImVec4 secondaryTextColor() {
    return colors().secondary;
}

ImFont* smallFont() {
    return gSmall != nullptr ? gSmall : ImGui::GetFont();
}

ImFont* boldFont() {
    return gBold != nullptr ? gBold : ImGui::GetFont();
}

ImFont* toolbarLabelFont() {
    return gLabel != nullptr ? gLabel : ImGui::GetFont();
}

const std::vector<int>& codeFontSizes() {
    static const std::vector<int> sizes = {9, 10, 11, 12, 13, 14, 16, 18, 20, 22, 24, 28, 32};
    return sizes;
}

ImFont* codeFont(int points) {
    const std::vector<int>& sizes = codeFontSizes();
    std::size_t best = 0;
    for (std::size_t i = 0; i < sizes.size(); ++i) {
        if (std::abs(sizes[i] - points) < std::abs(sizes[best] - points)) {
            best = i;
        }
    }
    return best < gCode.size() && gCode[best] != nullptr ? gCode[best] : ImGui::GetFont();
}

const CodeColors& codeColors() {
    // Xcode's Default (Light): near-black text on white, grey comments, purple
    // keywords, brown preprocessor, blue numbers, red strings.
    static const CodeColors light{
        rgb32(255, 255, 255), rgb32(38, 38, 38), rgb32(93, 108, 121), rgb32(155, 35, 147), rgb32(100, 56, 32),
        rgb32(28, 0, 207), rgb32(15, 104, 160), rgb32(50, 109, 116), rgb32(196, 26, 22), rgb32(164, 205, 255), rgb32(0, 0, 0)};
    // Xcode's Midnight: white on black, green comments, magenta keywords, orange
    // preprocessor, violet numbers, red strings.
    static const CodeColors midnight{
        rgb32(0, 0, 0), rgb32(255, 255, 255), rgb32(65, 204, 69), rgb32(211, 24, 149), rgb32(230, 122, 60),
        rgb32(120, 109, 255), rgb32(0, 160, 255), rgb32(35, 255, 131), rgb32(255, 44, 56), rgb32(60, 74, 107), rgb32(255, 255, 255)};
    return dark() ? midnight : light;
}

float unit() {
    return ImGui::GetFontSize() / 13.0f;
}

float toolbarItemHeight() {
    return std::floor(56.0f * unit());
}

float toolbarHeight() {
    return toolbarItemHeight() + std::floor(10.0f * unit());
}

float toolbarGap() {
    return std::floor(2.0f * unit());
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
    return text(Text::QuitApp);
#else
    return text(Text::Exit);
#endif
}

ImU32 canvasColor() {
    return colors().canvas;
}

ImU32 accentColor() {
    return rgb32(54, 128, 236);
}

bool toggleButton(const char* label, bool on, const ImVec2& size) {
    if (on) {
        ImGui::PushStyleColor(ImGuiCol_Button, colors().toggleOn);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colors().toggleOnHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, colors().toggleOnActive);
        ImGui::PushStyleColor(ImGuiCol_Border, colors().toggleOnBorder);
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
    // A window that sizes itself to its content reports no room on its first frame.
    const float avail = std::max(ImGui::GetContentRegionAvail().x, 1.0f);
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
        ImU32 fillColor = colors().segmentFill;
        if (selected) {
            fillColor = held ? rgb32(36, 106, 212) : accentColor();
        } else if (held) {
            fillColor = colors().segmentHeld;
        } else if (hovered) {
            fillColor = colors().segmentHover;
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

int iconSegmented(const char* id, const GlyphPainter* glyphs, int count, int selected, const bool* enabled, const char* const* tips) {
    ImGui::PushID(id);
    const float u = unit();
    const float height = ImGui::GetFrameHeight();
    const float width = std::floor(34.0f * u);
    const float rounding = ImGui::GetStyle().FrameRounding;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    int clicked = -1;
    for (int i = 0; i < count; ++i) {
        const bool on = enabled == nullptr || enabled[i];
        const float x = origin.x + width * static_cast<float>(i);
        ImGui::PushID(i);
        ImGui::SetCursorScreenPos(ImVec2(x, origin.y));
        ImGui::BeginDisabled(!on);
        if (ImGui::InvisibleButton("segment", ImVec2(width, height))) {
            clicked = i;
        }
        ImGui::EndDisabled();
        const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && on;
        const bool held = ImGui::IsItemActive() && on;
        const bool pressed = selected == i;
        ImU32 fillColor = colors().segmentFill;
        if (pressed) {
            fillColor = held ? rgb32(36, 106, 212) : accentColor();
        } else if (held) {
            fillColor = colors().segmentHeld;
        } else if (hovered) {
            fillColor = colors().segmentHover;
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
        if (i > 0 && !pressed && selected != i - 1) {
            draw->AddLine(ImVec2(x, origin.y + 1.0f), ImVec2(x, origin.y + height - 1.0f), ImGui::GetColorU32(ImGuiCol_Border));
        }
        ImU32 ink = pressed ? rgb32(255, 255, 255) : ImGui::GetColorU32(ImGuiCol_Text);
        if (!on) {
            ink = ImGui::GetColorU32(ImGuiCol_TextDisabled);
        }
        if (glyphs[i] != nullptr) {
            glyphs[i](draw, ImVec2(std::floor(x + width * 0.5f) + 0.5f, std::floor(origin.y + height * 0.5f) + 0.5f), std::floor(height * 0.5f), ink);
        }
        if (tips != nullptr && tips[i] != nullptr && hovered) {
            ImGui::SetTooltip("%s", tips[i]);
        }
        ImGui::PopID();
    }
    draw->AddRect(origin, ImVec2(origin.x + width * static_cast<float>(count), origin.y + height), ImGui::GetColorU32(ImGuiCol_Border), rounding);
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(ImVec2(width * static_cast<float>(count), height));
    ImGui::PopID();
    return clicked;
}

bool navigatorBar(const char* id, const GlyphPainter* glyphs, const char* const* tips, int count, int& current) {
    if (count <= 0) {
        return false;
    }
    ImGui::PushID(id);
    const float u = unit();
    const float height = std::floor(28.0f * u);
    const float slot = std::floor(30.0f * u);
    const float avail = std::max(ImGui::GetContentRegionAvail().x, 1.0f);
    // Xcode keeps the icons together at their natural spacing and centers the group; a narrow
    // sidebar squeezes the slots so every tab stays visible.
    const float width = std::min(slot, std::floor(avail / static_cast<float>(count)));
    const float group = width * static_cast<float>(count);
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const ImVec2 origin(std::floor(start.x + (avail - group) * 0.5f), start.y);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    bool changed = false;
    for (int i = 0; i < count; ++i) {
        const float x = origin.x + width * static_cast<float>(i);
        ImGui::PushID(i);
        ImGui::SetCursorScreenPos(ImVec2(x, origin.y));
        if (ImGui::InvisibleButton("tab", ImVec2(width, height)) && current != i) {
            current = i;
            changed = true;
        }
        const bool hovered = ImGui::IsItemHovered();
        const bool held = ImGui::IsItemActive();
        const bool selected = current == i;
        if (held && !selected) {
            const int shade = dark() ? 255 : 0;
            draw->AddRectFilled(ImVec2(x + 2.0f * u, origin.y + 2.0f * u), ImVec2(x + width - 2.0f * u, origin.y + height - 2.0f * u),
                                IM_COL32(shade, shade, shade, 30), std::floor(5.0f * u));
        }
        ImU32 ink = ImGui::GetColorU32(ImGuiCol_TextDisabled);
        if (selected) {
            ink = accentColor();
        } else if (hovered) {
            ink = ImGui::GetColorU32(ImGuiCol_Text);
        }
        if (glyphs[i] != nullptr) {
            glyphs[i](draw, ImVec2(std::floor(x + width * 0.5f) + 0.5f, std::floor(origin.y + height * 0.5f) + 0.5f), std::floor(16.0f * u), ink);
        }
        if (tips != nullptr && tips[i] != nullptr && hovered) {
            ImGui::SetTooltip("%s", tips[i]);
        }
        ImGui::PopID();
    }
    // The hairline runs the full width of the sidebar, under the whole row.
    const float lineY = std::floor(origin.y + height) + 0.5f;
    draw->AddLine(ImVec2(start.x - ImGui::GetStyle().WindowPadding.x, lineY),
                  ImVec2(start.x + avail + ImGui::GetStyle().WindowPadding.x, lineY), ImGui::GetColorU32(ImGuiCol_Separator));
    ImGui::SetCursorScreenPos(start);
    ImGui::Dummy(ImVec2(avail, height + 1.0f));
    ImGui::PopID();
    return changed;
}

float documentTabsHeight() {
    return std::floor(28.0f * unit());
}

DocumentTabEvent documentTabs(const char* id, const DocumentTab* tabs, int count, int current, int& eventTab, const char* closeTip,
                              bool glass) {
    DocumentTabEvent event;
    eventTab = -1;
    ImGui::PushID(id);
    const float u = unit();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float avail = std::max(ImGui::GetContentRegionAvail().x, 1.0f);
    const float height = documentTabsHeight();
    const bool isDark = dark();
    // The row sits a shade darker than the current tab, the way Xcode sets the
    // current tab apart from the others.
    // On glass the current tab is a light wash over the blur, so the map still shows through.
    const ImU32 rowFill = isDark ? rgb32(36, 36, 39) : rgb32(218, 218, 218);
    // On glass the other tabs sit under a gray wash, so the current one reads as lit.
    const ImU32 currentFill = glass ? (isDark ? rgb32(255, 255, 255, 36) : rgb32(255, 255, 255, 170))
                                    : (isDark ? rgb32(58, 58, 62) : rgb32(244, 244, 244));
    const ImU32 otherFill = glass ? (isDark ? rgb32(0, 0, 0, 90) : rgb32(0, 0, 0, 30)) : 0;
    const ImU32 hoverFill = glass ? (isDark ? rgb32(0, 0, 0, 50) : rgb32(0, 0, 0, 14))
                                  : (isDark ? rgb32(46, 46, 50) : rgb32(208, 208, 208));
    // The current tab's glow: a light sheen falling from its top edge, and the accent
    // color along its bottom edge with a faint halo above it.
    const ImU32 glowTop = isDark ? rgb32(255, 255, 255, 46) : rgb32(255, 255, 255, 200);
    const ImU32 glowClear = isDark ? rgb32(255, 255, 255, 0) : rgb32(255, 255, 255, 0);
    const ImU32 topEdge = isDark ? rgb32(255, 255, 255, 80) : rgb32(255, 255, 255, 255);
    const ImU32 accent = accentColor();
    const ImU32 accentHalo = (accent & ~IM_COL32_A_MASK) | (static_cast<ImU32>(isDark ? 70 : 50) << IM_COL32_A_SHIFT);
    const ImU32 accentClear = accent & ~IM_COL32_A_MASK;
    const ImU32 line = glass ? glassStyle().border : colors().toolbarLine;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (!glass) {
        draw->AddRectFilled(origin, ImVec2(origin.x + avail, origin.y + height), rowFill);
    }

    const float minWidth = std::floor(96.0f * u);
    const float width = count > 0 ? std::max(minWidth, std::floor(avail / static_cast<float>(count))) : avail;
    const float closeSize = std::floor(16.0f * u);
    const float closeInset = std::floor(6.0f * u);
    const ImU32 ink = ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32 dimInk = ImGui::GetColorU32(colors().secondary);
    ImRect selectedBar;
    for (int i = 0; i < count; ++i) {
        const float x0 = origin.x + width * static_cast<float>(i);
        // The last tab takes the pixels left over by rounding.
        const float x1 = i + 1 == count && width * static_cast<float>(count) <= avail ? origin.x + avail : x0 + width;
        const bool selected = i == current;
        ImGui::PushID(i);

        // The close button goes first so it takes the click before the tab does.
        const ImVec2 closeMin(x0 + closeInset, std::floor(origin.y + (height - closeSize) * 0.5f));
        const ImVec2 closeMax(closeMin.x + closeSize, closeMin.y + closeSize);
        ImGui::SetCursorScreenPos(ImVec2(x0, origin.y));
        ImGui::SetNextItemAllowOverlap();
        const bool tabPressed = ImGui::InvisibleButton("tab", ImVec2(std::max(1.0f, x1 - x0), height),
                                                       ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
        const bool tabHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenOverlappedByItem);
        const bool tipReady = ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenOverlappedByItem);
        const bool middle = tabPressed && ImGui::IsMouseReleased(ImGuiMouseButton_Middle);
        const bool showClose = selected || tabHovered;
        bool closeHovered = false;
        bool closePressed = false;
        if (showClose) {
            ImGui::SetCursorScreenPos(closeMin);
            closePressed = ImGui::InvisibleButton("close", ImVec2(closeSize, closeSize));
            closeHovered = ImGui::IsItemHovered();
            if (closeHovered && closeTip != nullptr) {
                ImGui::SetTooltip("%s", closeTip);
            }
        }

        if (selected) {
            const float bottom = origin.y + height;
            draw->AddRectFilled(ImVec2(x0, origin.y), ImVec2(x1, bottom), currentFill);
            const float glowDepth = std::floor(height * 0.55f);
            draw->AddRectFilledMultiColor(ImVec2(x0, origin.y), ImVec2(x1, origin.y + glowDepth), glowTop, glowTop, glowClear, glowClear);
            draw->AddLine(ImVec2(x0, origin.y + 0.5f), ImVec2(x1, origin.y + 0.5f), topEdge);
            const float bar = std::max(2.0f, std::floor(2.0f * u));
            const float halo = std::floor(8.0f * u);
            draw->AddRectFilledMultiColor(ImVec2(x0, bottom - bar - halo), ImVec2(x1, bottom - bar), accentClear, accentClear, accentHalo, accentHalo);
            selectedBar = ImRect(ImVec2(x0, bottom - bar), ImVec2(x1, bottom));
        } else if (tabHovered) {
            draw->AddRectFilled(ImVec2(x0, origin.y), ImVec2(x1, origin.y + height), hoverFill);
        } else if (otherFill != 0) {
            draw->AddRectFilled(ImVec2(x0, origin.y), ImVec2(x1, origin.y + height), otherFill);
        }
        // Hairlines between tabs.
        if (i > 0) {
            draw->AddLine(ImVec2(std::floor(x0) + 0.5f, origin.y), ImVec2(std::floor(x0) + 0.5f, origin.y + height), line);
        }

        const ImVec2 closeCenter(std::floor((closeMin.x + closeMax.x) * 0.5f) + 0.5f, std::floor((closeMin.y + closeMax.y) * 0.5f) + 0.5f);
        if (showClose) {
            if (closeHovered) {
                const int shade = isDark ? 255 : 0;
                draw->AddRectFilled(closeMin, closeMax, IM_COL32(shade, shade, shade, ImGui::IsItemActive() ? 50 : 28), std::floor(3.0f * u));
            }
            const float arm = std::floor(3.5f * u);
            const float thickness = std::max(1.0f, std::floor(1.25f * u));
            const ImU32 crossInk = closeHovered ? ink : dimInk;
            draw->AddLine(ImVec2(closeCenter.x - arm, closeCenter.y - arm), ImVec2(closeCenter.x + arm, closeCenter.y + arm), crossInk, thickness);
            draw->AddLine(ImVec2(closeCenter.x - arm, closeCenter.y + arm), ImVec2(closeCenter.x + arm, closeCenter.y - arm), crossInk, thickness);
        }

        // The name, centered in the tab and cut short with an ellipsis when it does not fit.
        const float textLeft = closeMax.x + std::floor(4.0f * u);
        const float textRight = x1 - closeInset - closeSize - std::floor(4.0f * u);
        const char* title = tabs[i].title != nullptr ? tabs[i].title : "";
        const ImVec2 textSize = ImGui::CalcTextSize(title);
        const float textY = std::floor(origin.y + (height - textSize.y) * 0.5f);
        const float room = std::max(0.0f, textRight - textLeft);
        const float textX = textSize.x <= room ? std::floor(textLeft + (room - textSize.x) * 0.5f) : textLeft;
        ImGui::PushStyleColor(ImGuiCol_Text, selected ? ImGui::GetStyleColorVec4(ImGuiCol_Text) : colors().secondary);
        ImGui::RenderTextEllipsis(draw, ImVec2(textX, textY), ImVec2(textRight, origin.y + height), textRight, textRight, title, nullptr, &textSize);
        ImGui::PopStyleColor();
        // Unsaved changes: a dot after the name, like VS Code's tabs.
        if (tabs[i].dirty) {
            const float shown = std::min(textSize.x, room);
            const float radius = std::max(2.5f, std::floor(3.5f * u));
            const ImVec2 dot(std::min(textX + shown + radius + std::floor(5.0f * u), x1 - radius - 2.0f), std::floor(origin.y + height * 0.5f) + 0.5f);
            draw->AddCircleFilled(dot, radius, selected ? ink : dimInk, 12);
        }

        if (tipReady && !closeHovered && tabs[i].tooltip != nullptr && tabs[i].tooltip[0] != '\0') {
            ImGui::SetTooltip("%s", tabs[i].tooltip);
        }
        if (closePressed || middle) {
            event.close = true;
            eventTab = i;
        } else if (tabPressed && !selected) {
            event.select = true;
            eventTab = i;
        }
        ImGui::PopID();
    }
    // A hairline under the whole row, as under the toolbar.
    const float lineY = std::floor(origin.y + height) - 0.5f;
    draw->AddLine(ImVec2(origin.x, lineY), ImVec2(origin.x + avail, lineY), line);
    // The accent bar goes over the hairline so it stays unbroken.
    if (selectedBar.GetWidth() > 0.0f) {
        draw->AddRectFilled(selectedBar.Min, selectedBar.Max, accent);
    }
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(ImVec2(avail, height));
    ImGui::PopID();
    return event;
}

void toolbarBackground() {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    const ImVec2 max(min.x + size.x, min.y + size.y);
    const Colors& c = colors();
    draw->AddRectFilledMultiColor(min, max, c.toolbarTop, c.toolbarTop, c.toolbarBottom, c.toolbarBottom);
    draw->AddLine(ImVec2(min.x, max.y - 1.0f), ImVec2(max.x, max.y - 1.0f), c.toolbarLine);
}

void toolbarSeparator() {
    const float gap = std::floor(7.0f * unit());
    ImGui::SameLine(0.0f, gap);
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float height = toolbarItemHeight();
    const float margin = std::floor(6.0f * unit());
    ImGui::GetWindowDrawList()->AddLine(ImVec2(at.x, at.y + margin), ImVec2(at.x, at.y + height - margin), colors().separatorShade);
    ImGui::GetWindowDrawList()->AddLine(ImVec2(at.x + 1.0f, at.y + margin), ImVec2(at.x + 1.0f, at.y + height - margin), colors().separatorLight);
    ImGui::Dummy(ImVec2(2.0f, height));
    ImGui::SameLine(0.0f, gap);
}

void bottomBarBackground() {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    const ImVec2 max(min.x + size.x, min.y + size.y);
    const Colors& c = colors();
    draw->AddRectFilledMultiColor(min, max, c.bottomTop, c.bottomTop, c.bottomBottom, c.bottomBottom);
    draw->AddLine(min, ImVec2(max.x, min.y), c.bottomLine);
}

void sidebarBackground() {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    const ImVec2 max(min.x + size.x, min.y + size.y);
    draw->AddRectFilled(min, max, colors().sidebar);
    draw->AddLine(ImVec2(max.x - 1.0f, min.y), ImVec2(max.x - 1.0f, max.y), colors().sidebarLine);
}

void sourceListTitle(const char* text) {
    ImGui::PushFont(smallFont());
    ImGui::PushStyleColor(ImGuiCol_Text, colors().sourceTitle);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

bool sourceListHeading(const char* title, const char* reloadId) {
    ImGui::PushFont(smallFont());
    const float line = ImGui::GetTextLineHeight();
    const float scale = ImGui::GetFontSize() / 13.0f;
    // The hit target is a little larger than the title, the size of a small
    // macOS accessory button, while the arrow itself stays inside that square.
    const float size = std::floor(std::max(line + 6.0f * scale, 16.0f * scale));
    const float gap = std::floor(4.0f * scale);
    const float titleWidth = std::max(8.0f, ImGui::GetContentRegionAvail().x - size - gap);
    std::string fitted = title != nullptr ? title : "";
    if (ImGui::CalcTextSize(fitted.c_str()).x > titleWidth) {
        while (fitted.size() > 1 && ImGui::CalcTextSize((fitted + "...").c_str()).x > titleWidth) {
            fitted.pop_back();
            while (!fitted.empty() && (static_cast<unsigned char>(fitted.back()) & 0xC0) == 0x80) {
                fitted.pop_back();
            }
        }
        fitted += "...";
    }

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float textY = std::floor(origin.y + (size - line) * 0.5f);
    ImGui::GetWindowDrawList()->AddText(ImVec2(origin.x, textY), ImGui::GetColorU32(colors().sourceTitle), fitted.c_str());
    ImGui::Dummy(ImVec2(titleWidth, size));
    ImGui::SameLine(0.0f, gap);

    const ImVec2 button = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(reloadId, ImVec2(size, size));
    const bool hot = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (held || hot) {
        const int shade = dark() ? 255 : 0;
        draw->AddRectFilled(
            button,
            ImVec2(button.x + size, button.y + size),
            IM_COL32(shade, shade, shade, held ? 44 : 16),
            std::floor(4.0f * scale));
    }

    const ImU32 ink = hot || held ? ImGui::GetColorU32(ImGuiCol_Text) : ImGui::GetColorU32(colors().sourceTitle);
    const ImVec2 center(button.x + size * 0.5f, button.y + size * 0.5f);
    const float radius = size * 0.28f;
    const float thickness = std::max(1.25f, size * 0.085f);
    // The gap sits just past 12 o'clock and the head ends there, pointing
    // clockwise, which is the arrow.clockwise symbol.
    constexpr float kPi = 3.14159265f;
    const float start = -kPi * 0.5f + 0.70f;
    const float end = start + kPi * 1.75f;
    draw->PathArcTo(center, radius, start, end, 20);
    draw->PathStroke(ink, 0, thickness);

    const float tx = -std::sin(end);
    const float ty = std::cos(end);
    const float ex = center.x + radius * std::cos(end);
    const float ey = center.y + radius * std::sin(end);
    const float head = size * 0.16f;
    const float half = size * 0.11f;
    draw->AddTriangleFilled(
        ImVec2(ex + tx * head, ey + ty * head),
        ImVec2(ex - ty * half, ey + tx * half),
        ImVec2(ex + ty * half, ey - tx * half),
        ink);

    ImGui::PopFont();
    return pressed;
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
    ImGui::PushStyleColor(ImGuiCol_Text, colors().help);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

}  // namespace galaxy::theme
