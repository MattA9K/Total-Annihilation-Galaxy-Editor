#include "ui/Theme.hpp"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <vector>

namespace galaxy::theme {
namespace {

constexpr float kPi = 3.14159265f;

// Draws on a 32 x 32 canvas, the freestanding toolbar icon size in the 2014 OS X
// guidelines. Shapes are straight-on with a clear dark outline. Color is kept to
// one or two accents per icon. Every color goes through the style alpha, so a
// disabled toolbar item fades along with its label.
class Canvas {
public:
    Canvas(ImDrawList* list, ImVec2 origin, float scale, bool flip = false)
        : list_(list), origin_(origin), scale_(scale), flip_(flip) {}

    ImVec2 at(float x, float y) const {
        return ImVec2(origin_.x + (flip_ ? 32.0f - x : x) * scale_, origin_.y + y * scale_);
    }

    static ImU32 color(int r, int g, int b, int a = 255) {
        return ImGui::GetColorU32(ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f));
    }

    static ImU32 ink() { return color(66, 70, 78); }

    float width(float w) const { return std::max(1.0f, w * scale_); }

    void line(float x0, float y0, float x1, float y1, ImU32 col, float thickness) const {
        list_->AddLine(at(x0, y0), at(x1, y1), col, width(thickness));
    }

    void rect(float x0, float y0, float x1, float y1, ImU32 fill, ImU32 stroke, float rounding = 2.0f, float thickness = 1.5f) const {
        const ImVec2 a = at(x0, y0);
        const ImVec2 b = at(x1, y1);
        const ImVec2 lo(std::min(a.x, b.x), std::min(a.y, b.y));
        const ImVec2 hi(std::max(a.x, b.x), std::max(a.y, b.y));
        if ((fill >> IM_COL32_A_SHIFT) != 0) {
            list_->AddRectFilled(lo, hi, fill, rounding * scale_);
        }
        if ((stroke >> IM_COL32_A_SHIFT) != 0) {
            list_->AddRect(lo, hi, stroke, rounding * scale_, 0, width(thickness));
        }
    }

    void polygon(std::initializer_list<ImVec2> points, ImU32 fill, ImU32 stroke, float thickness = 1.5f) const {
        std::vector<ImVec2> mapped;
        mapped.reserve(points.size());
        for (const ImVec2& point : points) {
            mapped.push_back(at(point.x, point.y));
        }
        // ImGui fills convex shapes with a fixed winding, so normalize it.
        float area = 0.0f;
        for (std::size_t i = 0; i < mapped.size(); ++i) {
            const ImVec2& p = mapped[i];
            const ImVec2& q = mapped[(i + 1) % mapped.size()];
            area += p.x * q.y - q.x * p.y;
        }
        if (area < 0.0f) {
            std::reverse(mapped.begin(), mapped.end());
        }
        if ((fill >> IM_COL32_A_SHIFT) != 0) {
            list_->AddConvexPolyFilled(mapped.data(), static_cast<int>(mapped.size()), fill);
        }
        if ((stroke >> IM_COL32_A_SHIFT) != 0) {
            list_->AddPolyline(mapped.data(), static_cast<int>(mapped.size()), stroke, ImDrawFlags_Closed, width(thickness));
        }
    }

    void polyline(std::initializer_list<ImVec2> points, ImU32 col, float thickness) const {
        std::vector<ImVec2> mapped;
        mapped.reserve(points.size());
        for (const ImVec2& point : points) {
            mapped.push_back(at(point.x, point.y));
        }
        list_->AddPolyline(mapped.data(), static_cast<int>(mapped.size()), col, ImDrawFlags_None, width(thickness));
    }

    void circle(float cx, float cy, float radius, ImU32 fill, ImU32 stroke, float thickness = 1.5f) const {
        if ((fill >> IM_COL32_A_SHIFT) != 0) {
            list_->AddCircleFilled(at(cx, cy), radius * scale_, fill, 28);
        }
        if ((stroke >> IM_COL32_A_SHIFT) != 0) {
            list_->AddCircle(at(cx, cy), radius * scale_, stroke, 28, width(thickness));
        }
    }

    void ellipse(float cx, float cy, float rx, float ry, float rotation, ImU32 fill, ImU32 stroke, float thickness = 1.2f) const {
        const float rot = flip_ ? -rotation : rotation;
        list_->AddEllipseFilled(at(cx, cy), ImVec2(rx * scale_, ry * scale_), fill, rot, 36);
        if ((stroke >> IM_COL32_A_SHIFT) != 0) {
            list_->AddEllipse(at(cx, cy), ImVec2(rx * scale_, ry * scale_), stroke, rot, 36, width(thickness));
        }
    }

    // Dashed outline of a rectangle, like a selection marquee.
    void dashedRect(float x0, float y0, float x1, float y1, ImU32 col, float thickness) const {
        const float dash = 3.0f;
        const float gap = 2.2f;
        const auto run = [&](float ax, float ay, float bx, float by) {
            const float length = std::hypot(bx - ax, by - ay);
            const float dx = (bx - ax) / length;
            const float dy = (by - ay) / length;
            for (float t = 0.0f; t < length; t += dash + gap) {
                const float end = std::min(t + dash, length);
                line(ax + dx * t, ay + dy * t, ax + dx * end, ay + dy * end, col, thickness);
            }
        };
        run(x0, y0, x1, y0);
        run(x1, y0, x1, y1);
        run(x1, y1, x0, y1);
        run(x0, y1, x0, y0);
    }

    // A thick arc that ends in an arrowhead. Angles are in radians and the arc runs
    // from `from` to `to` in the direction of travel.
    void arcArrow(float cx, float cy, float radius, float from, float to, ImU32 col, float thickness) const {
        std::vector<ImVec2> points;
        constexpr int steps = 18;
        for (int i = 0; i <= steps; ++i) {
            const float a = from + (to - from) * static_cast<float>(i) / static_cast<float>(steps);
            points.push_back(at(cx + radius * std::cos(a), cy + radius * std::sin(a)));
        }
        list_->AddPolyline(points.data(), static_cast<int>(points.size()), col, ImDrawFlags_None, width(thickness));
        const float sign = to < from ? -1.0f : 1.0f;
        const float tipAngle = to;
        const float tx = -std::sin(tipAngle) * sign;
        const float ty = std::cos(tipAngle) * sign;
        const float ex = cx + radius * std::cos(tipAngle);
        const float ey = cy + radius * std::sin(tipAngle);
        const float head = 5.2f;
        const float half = 4.6f;
        polygon(
            {ImVec2(ex + tx * head, ey + ty * head),
             ImVec2(ex - ty * half, ey + tx * half),
             ImVec2(ex + ty * half, ey - tx * half)},
            col,
            0);
    }

private:
    ImDrawList* list_;
    ImVec2 origin_;
    float scale_;
    bool flip_;
};

struct Palette {
    ImU32 ink;
    ImU32 paper;
    ImU32 paperEdge;
    ImU32 blue;
    ImU32 blueSoft;
    ImU32 green;
    ImU32 greenDeep;
    ImU32 sand;
    ImU32 orange;
    ImU32 red;
    ImU32 steel;
    ImU32 halo;       // outline around a shape that sits on top of others
    ImU32 inkFaint;
    ImU32 mapPanel;   // backdrop of the height map picture
    ImU32 discSoft;   // pale half of the opacity disc
};

Palette makePalette() {
    if (dark()) {
        // Lighter outlines on darker paper, so the icons keep their shape on a dark toolbar.
        const ImU32 ink = Canvas::color(226, 229, 236);
        return Palette{
            ink,
            Canvas::color(62, 65, 72),
            Canvas::color(150, 156, 166),
            Canvas::color(66, 140, 244),
            Canvas::color(52, 74, 110),
            Canvas::color(96, 170, 88),
            Canvas::color(70, 130, 74),
            Canvas::color(206, 180, 124),
            Canvas::color(240, 146, 60),
            Canvas::color(232, 84, 74),
            Canvas::color(118, 126, 140),
            Canvas::color(24, 26, 30),
            Canvas::color(226, 229, 236, 70),
            Canvas::color(40, 52, 68),
            Canvas::color(58, 78, 112),
        };
    }
    return Palette{
        Canvas::ink(),
        Canvas::color(252, 252, 252),
        Canvas::color(120, 126, 136),
        Canvas::color(46, 120, 232),
        Canvas::color(176, 208, 248),
        Canvas::color(108, 176, 96),
        Canvas::color(70, 130, 74),
        Canvas::color(222, 196, 140),
        Canvas::color(238, 138, 48),
        Canvas::color(218, 70, 60),
        Canvas::color(150, 158, 170),
        Canvas::color(255, 255, 255),
        Canvas::color(66, 70, 78, 70),
        Canvas::color(214, 232, 246),
        Canvas::color(196, 218, 248),
    };
}

void sheet(const Canvas& c, const Palette& p, float x0, float y0, float x1, float y1) {
    c.rect(x0, y0, x1, y1, p.paper, p.ink, 2.5f, 1.5f);
}

void drawSelect(const Canvas& c, const Palette& p) {
    c.rect(4.0f, 5.0f, 25.0f, 24.0f, Canvas::color(46, 120, 232, 36), 0, 0.0f);
    c.dashedRect(4.0f, 5.0f, 25.0f, 24.0f, p.blue, 1.8f);
    c.line(20.0f, 21.5f, 26.0f, 29.5f, p.halo, 5.2f);
    c.polygon({ImVec2(13.0f, 12.0f), ImVec2(13.0f, 30.0f), ImVec2(27.0f, 22.5f)}, p.halo, p.halo, 3.0f);
    c.line(20.0f, 22.5f, 25.5f, 29.0f, p.ink, 3.2f);
    c.polygon({ImVec2(14.2f, 14.6f), ImVec2(14.2f, 27.4f), ImVec2(24.4f, 21.8f)}, p.ink, p.ink, 1.0f);
}

void drawCut(const Canvas& c, const Palette& p) {
    c.line(10.5f, 3.0f, 21.5f, 20.5f, p.steel, 3.2f);
    c.line(21.5f, 3.0f, 10.5f, 20.5f, p.steel, 3.2f);
    c.line(10.5f, 3.0f, 21.5f, 20.5f, p.ink, 1.1f);
    c.line(21.5f, 3.0f, 10.5f, 20.5f, p.ink, 1.1f);
    c.circle(10.0f, 25.0f, 4.4f, 0, p.red, 2.6f);
    c.circle(22.0f, 25.0f, 4.4f, 0, p.red, 2.6f);
    c.circle(16.0f, 11.8f, 1.6f, p.paper, p.ink, 1.3f);
}

void drawCopy(const Canvas& c, const Palette& p) {
    sheet(c, p, 4.5f, 3.5f, 19.5f, 22.5f);
    sheet(c, p, 12.0f, 9.0f, 27.5f, 29.0f);
    c.rect(14.5f, 23.0f, 25.0f, 26.5f, p.green, 0, 1.0f);
    c.polygon({ImVec2(14.5f, 23.0f), ImVec2(19.7f, 14.2f), ImVec2(25.0f, 23.0f)}, p.sand, p.ink, 1.3f);
}

void drawCopyHeights(const Canvas& c, const Palette& p) {
    sheet(c, p, 4.5f, 3.5f, 19.5f, 22.5f);
    sheet(c, p, 12.0f, 9.0f, 27.5f, 29.0f);
    c.polyline({ImVec2(14.2f, 25.5f), ImVec2(17.0f, 19.6f), ImVec2(19.6f, 23.0f), ImVec2(22.2f, 14.6f), ImVec2(25.4f, 19.0f)}, p.orange, 2.2f);
    c.line(14.2f, 27.0f, 25.4f, 27.0f, p.paperEdge, 1.2f);
}

void drawPaste(const Canvas& c, const Palette& p) {
    c.rect(5.0f, 6.0f, 27.0f, 30.0f, Canvas::color(220, 172, 100), p.ink, 3.0f, 1.5f);
    c.rect(9.0f, 11.0f, 23.0f, 27.0f, p.paper, p.paperEdge, 1.5f, 1.2f);
    c.line(11.5f, 16.0f, 20.5f, 16.0f, p.paperEdge, 1.3f);
    c.line(11.5f, 19.5f, 20.5f, 19.5f, p.paperEdge, 1.3f);
    c.line(11.5f, 23.0f, 17.5f, 23.0f, p.paperEdge, 1.3f);
    c.rect(11.0f, 3.0f, 21.0f, 9.5f, p.steel, p.ink, 2.0f, 1.5f);
    c.circle(16.0f, 5.2f, 1.1f, p.paper, 0, 1.0f);
}

void drawModifyHeight(const Canvas& c, const Palette& p) {
    c.line(8.5f, 8.0f, 8.5f, 24.0f, p.blue, 3.0f);
    c.polygon({ImVec2(8.5f, 3.0f), ImVec2(3.6f, 9.6f), ImVec2(13.4f, 9.6f)}, p.blue, 0, 0.0f);
    c.polygon({ImVec2(8.5f, 29.0f), ImVec2(3.6f, 22.4f), ImVec2(13.4f, 22.4f)}, p.blue, 0, 0.0f);
    c.polygon({ImVec2(14.5f, 28.0f), ImVec2(21.5f, 10.0f), ImVec2(29.0f, 28.0f)}, p.green, p.ink, 1.5f);
    c.polygon({ImVec2(21.5f, 10.0f), ImVec2(18.2f, 18.4f), ImVec2(24.9f, 18.4f)}, p.paper, p.ink, 1.1f);
    c.line(13.5f, 28.0f, 30.0f, 28.0f, p.ink, 1.5f);
}

void drawBlend(const Canvas& c, const Palette& p) {
    c.rect(3.5f, 8.0f, 14.5f, 25.0f, p.green, 0, 2.5f);
    c.rect(17.5f, 8.0f, 28.5f, 25.0f, p.sand, 0, 2.5f);
    // Mix the middle strip left to right.
    const ImVec2 left = c.at(14.0f, 8.0f);
    const ImVec2 right = c.at(18.0f, 25.0f);
    ImGui::GetWindowDrawList()->AddRectFilledMultiColor(
        ImVec2(std::min(left.x, right.x), left.y), ImVec2(std::max(left.x, right.x), right.y), p.green, p.sand, p.sand, p.green);
    c.rect(3.5f, 8.0f, 28.5f, 25.0f, 0, p.ink, 2.5f, 1.5f);
    c.line(14.0f, 8.0f, 14.0f, 25.0f, p.inkFaint, 1.0f);
    c.line(18.0f, 8.0f, 18.0f, 25.0f, p.inkFaint, 1.0f);
}

void drawHeightMap(const Canvas& c, const Palette& p) {
    c.rect(3.5f, 4.5f, 28.5f, 27.5f, p.mapPanel, p.ink, 3.0f, 1.5f);
    c.ellipse(16.0f, 16.4f, 11.2f, 8.6f, -0.35f, p.green, p.ink, 1.2f);
    c.ellipse(16.4f, 16.2f, 7.6f, 5.6f, -0.35f, Canvas::color(228, 208, 96), p.ink, 1.2f);
    c.ellipse(16.8f, 16.0f, 3.9f, 2.7f, -0.35f, p.orange, p.ink, 1.2f);
}

void drawView3D(const Canvas& c, const Palette& p) {
    // A tilted ground plane with a hill on it, and a see-through sea in front.
    c.polygon({ImVec2(2.5f, 19.0f), ImVec2(16.0f, 9.5f), ImVec2(29.5f, 19.0f), ImVec2(16.0f, 28.5f)}, p.green, p.ink, 1.5f);
    c.polygon({ImVec2(9.0f, 20.0f), ImVec2(16.0f, 5.0f), ImVec2(23.0f, 20.0f), ImVec2(16.0f, 24.5f)}, p.sand, p.ink, 1.5f);
    c.polygon({ImVec2(16.0f, 5.0f), ImVec2(13.4f, 11.8f), ImVec2(18.6f, 11.8f)}, p.paper, 0, 0.0f);
    c.polygon({ImVec2(2.5f, 19.0f), ImVec2(16.0f, 28.5f), ImVec2(29.5f, 19.0f), ImVec2(29.5f, 23.0f), ImVec2(16.0f, 31.5f), ImVec2(2.5f, 23.0f)}, p.blueSoft, p.ink, 1.3f);
}

void drawUndo(const Canvas& c, const Palette& p) {
    c.arcArrow(16.5f, 17.5f, 8.8f, 0.30f * kPi, -0.92f * kPi, p.blue, 3.4f);
}

void drawOpacity(const Canvas& c, const Palette& p) {
    c.circle(16.0f, 16.0f, 11.5f, p.discSoft, 0);
    ImDrawList* list = ImGui::GetWindowDrawList();
    list->PathClear();
    const ImVec2 center = c.at(16.0f, 16.0f);
    const float radius = 11.5f * (c.at(1.0f, 0.0f).x - c.at(0.0f, 0.0f).x);
    list->PathArcTo(center, std::fabs(radius), kPi * 0.5f, kPi * 1.5f, 24);
    list->PathFillConvex(p.blue);
    c.circle(16.0f, 16.0f, 11.5f, 0, p.ink, 1.6f);
    c.line(16.0f, 4.5f, 16.0f, 27.5f, p.ink, 1.2f);
}

void drawDelete(const Canvas& c, const Palette& p) {
    c.rect(12.5f, 3.6f, 19.5f, 8.0f, 0, p.ink, 1.5f, 1.6f);
    c.polygon({ImVec2(7.8f, 11.0f), ImVec2(24.2f, 11.0f), ImVec2(22.0f, 29.0f), ImVec2(10.0f, 29.0f)}, p.red, p.ink, 1.5f);
    c.rect(5.5f, 7.0f, 26.5f, 11.2f, p.steel, p.ink, 2.0f, 1.5f);
    for (const float x : {12.6f, 16.0f, 19.4f}) {
        c.line(x, 14.6f, x, 25.8f, Canvas::color(255, 255, 255, 215), 1.7f);
    }
}

void drawIcon(ToolIcon icon, const Canvas& canvas, const Canvas& flipped) {
    const Palette p = makePalette();
    switch (icon) {
    case ToolIcon::Select: drawSelect(canvas, p); break;
    case ToolIcon::Cut: drawCut(canvas, p); break;
    case ToolIcon::Copy: drawCopy(canvas, p); break;
    case ToolIcon::CopyHeights: drawCopyHeights(canvas, p); break;
    case ToolIcon::Paste: drawPaste(canvas, p); break;
    case ToolIcon::ModifyHeight: drawModifyHeight(canvas, p); break;
    case ToolIcon::Blend: drawBlend(canvas, p); break;
    case ToolIcon::HeightMap: drawHeightMap(canvas, p); break;
    case ToolIcon::View3D: drawView3D(canvas, p); break;
    case ToolIcon::Undo: drawUndo(canvas, p); break;
    case ToolIcon::Redo: drawUndo(flipped, p); break;
    case ToolIcon::Opacity: drawOpacity(canvas, p); break;
    case ToolIcon::Delete: drawDelete(canvas, p); break;
    }
}

}  // namespace

bool toolbarItem(const char* id, ToolIcon icon, const char* label, bool on, const char* widthLabel) {
    const float u = unit();
    const bool disabled = (ImGui::GetCurrentContext()->CurrentItemFlags & ImGuiItemFlags_Disabled) != 0;
    ImFont* labelFont = toolbarLabelFont();

    ImGui::PushFont(labelFont);
    const float labelWidth = ImGui::CalcTextSize(label).x;
    const float reserved = widthLabel != nullptr ? std::max(labelWidth, ImGui::CalcTextSize(widthLabel).x) : labelWidth;
    ImGui::PopFont();

    const float width = std::floor(std::max(50.0f * u, reserved + 16.0f * u));
    const float height = toolbarItemHeight();
    const ImVec2 origin = ImGui::GetCursorScreenPos();

    const bool pressed = ImGui::InvisibleButton(id, ImVec2(width, height));
    const bool hovered = ImGui::IsItemHovered() && !disabled;
    const bool held = ImGui::IsItemActive() && !disabled;

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 corner(origin.x + width, origin.y + height);
    const float rounding = std::floor(5.0f * u);
    // Pressed and hovered states darken a light toolbar and lighten a dark one.
    const int shade = dark() ? 255 : 0;
    if (on) {
        draw->AddRectFilled(origin, corner, Canvas::color(shade, shade, shade, held ? 58 : 44), rounding);
        draw->AddRect(origin, corner, Canvas::color(shade, shade, shade, 70), rounding, 0, 1.0f);
    } else if (held) {
        draw->AddRectFilled(origin, corner, Canvas::color(shade, shade, shade, 44), rounding);
    } else if (hovered) {
        draw->AddRectFilled(origin, corner, Canvas::color(shade, shade, shade, 16), rounding);
    }

    const float iconSize = 32.0f * u;
    const ImVec2 iconOrigin(std::floor(origin.x + (width - iconSize) * 0.5f), std::floor(origin.y + 5.0f * u));
    const Canvas canvas(draw, iconOrigin, u);
    const Canvas flipped(draw, iconOrigin, u, true);
    drawIcon(icon, canvas, flipped);

    ImGui::PushFont(labelFont);
    const ImVec2 textPos(std::floor(origin.x + (width - labelWidth) * 0.5f), std::floor(origin.y + 5.0f * u + iconSize + 3.0f * u));
    const ImVec4 pressedText = dark() ? ImVec4(0.98f, 0.98f, 0.98f, 1.0f) : ImVec4(0.08f, 0.08f, 0.08f, 1.0f);
    draw->AddText(textPos, ImGui::GetColorU32(on ? pressedText : ImGui::GetStyleColorVec4(ImGuiCol_Text)), label);
    ImGui::PopFont();
    return pressed;
}

BadgeRect badgeRect(ImVec2 itemMin, ImVec2 itemMax) {
    const float scale = ImGui::GetFontSize() / 13.0f;
    const float size = std::floor(24.0f * scale);
    const float inset = std::floor(4.0f * scale);
    const ImVec2 min(itemMax.x - size - inset, itemMin.y + inset);
    return BadgeRect{min, ImVec2(min.x + size, min.y + size)};
}

void drawBadge(const BadgeRect& rect, BadgeIcon icon, bool hot) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float size = rect.max.x - rect.min.x;
    const ImVec2 center((rect.min.x + rect.max.x) * 0.5f, (rect.min.y + rect.max.y) * 0.5f);
    // Dark translucent disc with a thin light rim reads on any tile picture.
    const bool trash = icon == BadgeIcon::Trash;
    ImU32 fill = Canvas::color(28, 30, 34, hot ? 235 : 180);
    if (hot && trash) {
        fill = Canvas::color(196, 48, 43, 240);
    } else if (hot) {
        fill = Canvas::color(54, 128, 236, 240);
    }
    draw->AddCircleFilled(center, size * 0.5f, fill, 32);
    draw->AddCircle(center, size * 0.5f, Canvas::color(255, 255, 255, hot ? 200 : 120), 32, 1.0f);

    const Canvas canvas(draw, rect.min, size / 32.0f);
    const ImU32 glyph = Canvas::color(255, 255, 255, 245);
    if (trash) {
        canvas.line(8.5f, 10.5f, 23.5f, 10.5f, glyph, 2.0f);
        canvas.polyline({ImVec2(13.0f, 10.5f), ImVec2(13.0f, 7.5f), ImVec2(19.0f, 7.5f), ImVec2(19.0f, 10.5f)}, glyph, 1.8f);
        canvas.polyline({ImVec2(10.5f, 13.5f), ImVec2(11.6f, 25.0f), ImVec2(20.4f, 25.0f), ImVec2(21.5f, 13.5f)}, glyph, 1.8f);
        for (const float x : {14.2f, 16.0f, 17.8f}) {
            canvas.line(x, 16.0f, x, 22.0f, glyph, 1.4f);
        }
        return;
    }
    // A five-point star, filled as a core plus five tips so every piece is convex.
    ImVec2 outer[5];
    ImVec2 inner[5];
    for (int i = 0; i < 5; ++i) {
        const float tip = -kPi * 0.5f + static_cast<float>(i) * 2.0f * kPi / 5.0f;
        const float notch = tip + kPi / 5.0f;
        outer[i] = ImVec2(16.0f + 11.5f * std::cos(tip), 17.0f + 11.5f * std::sin(tip));
        inner[i] = ImVec2(16.0f + 4.8f * std::cos(notch), 17.0f + 4.8f * std::sin(notch));
    }
    canvas.polygon({inner[0], inner[1], inner[2], inner[3], inner[4]}, glyph, 0, 0.0f);
    for (int i = 0; i < 5; ++i) {
        canvas.polygon({inner[(i + 4) % 5], outer[i], inner[i]}, glyph, 0, 0.0f);
    }
}

}  // namespace galaxy::theme
