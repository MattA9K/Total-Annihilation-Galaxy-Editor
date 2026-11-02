#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace galaxy {

// The tile brush works on a grid of 16 x 16 tile cells. Every cell corner sits on
// one paint, and a cell picks its picture from its four corners. Two paints may
// only touch through a transition piece, and only some pairs have pieces:
// Base-0 and Base-85 join with Coast pieces, Base-68 and Base-85 with
// ShallowCoast pieces, Base-68 and Base-0 with ShallowDeep pieces, Base-85 and
// Base-219 with Cliff pieces, and Base-219 joins Base-0 and Base-68 with
// CoastalCliff pieces. A cell holds one paint or two joined paints. Three paints
// may share a cell when a section draws that layout of corners: Base-0, Base-68
// and Base-85 use a CoastDSL piece, and Base-219 with Base-85 and either Base-0
// or Base-68 uses a CoastalCliff piece.
inline constexpr int kBrushCellTiles = 16;

// Corner bits of a cell mask.
inline constexpr int kCornerTopLeft = 1;
inline constexpr int kCornerTopRight = 2;
inline constexpr int kCornerBottomLeft = 4;
inline constexpr int kCornerBottomRight = 8;
inline constexpr int kCornerAll = 15;

// A transition cell has corners on two neighboring levels. Its mask marks the
// corners on the upper level.

// The cliff section a mask needs, without its variation number, such as
// "Cliff-High-BR" for a cell whose only upper corner is the bottom right.
// High-X has X as its high ground and Low-X is high everywhere except X. Flat
// cells (0 and 15) and the two diagonal masks have no piece, so those return an
// empty name.
inline std::string_view cliffPieceName(int mask) {
    switch (mask) {
    case kCornerTopLeft: return "Cliff-High-TL";
    case kCornerTopRight: return "Cliff-High-TR";
    case kCornerBottomLeft: return "Cliff-High-BL";
    case kCornerBottomRight: return "Cliff-High-BR";
    case kCornerTopLeft | kCornerTopRight: return "Cliff-High-T";
    case kCornerBottomLeft | kCornerBottomRight: return "Cliff-High-B";
    case kCornerTopLeft | kCornerBottomLeft: return "Cliff-High-L";
    case kCornerTopRight | kCornerBottomRight: return "Cliff-High-R";
    case kCornerAll & ~kCornerTopLeft: return "Cliff-Low-TL";
    case kCornerAll & ~kCornerTopRight: return "Cliff-Low-TR";
    case kCornerAll & ~kCornerBottomLeft: return "Cliff-Low-BL";
    case kCornerAll & ~kCornerBottomRight: return "Cliff-Low-BR";
    default: return {};
    }
}

// The coast section a mask needs, where the mask marks the land corners.
// Coast-Land-X has land only at X. Coast-Water-X has water at X and land at the
// rest, and Coast-Water-T has water along the top, so land along the bottom.
inline std::string_view coastPieceName(int mask) {
    switch (mask) {
    case kCornerTopLeft: return "Coast-Land-TL";
    case kCornerTopRight: return "Coast-Land-TR";
    case kCornerBottomLeft: return "Coast-Land-BL";
    case kCornerBottomRight: return "Coast-Land-BR";
    case kCornerTopLeft | kCornerTopRight: return "Coast-Water-B";
    case kCornerBottomLeft | kCornerBottomRight: return "Coast-Water-T";
    case kCornerTopLeft | kCornerBottomLeft: return "Coast-Water-R";
    case kCornerTopRight | kCornerBottomRight: return "Coast-Water-L";
    case kCornerAll & ~kCornerTopLeft: return "Coast-Water-TL";
    case kCornerAll & ~kCornerTopRight: return "Coast-Water-TR";
    case kCornerAll & ~kCornerBottomLeft: return "Coast-Water-BL";
    case kCornerAll & ~kCornerBottomRight: return "Coast-Water-BR";
    default: return {};
    }
}

// Name of a corner or an edge for the CoastDSL sections: one corner is TL, TR,
// BL or BR, two corners on one side are T, B, L or R, and two corners on a
// diagonal are written the way the sections are named.
inline std::string_view cornerRegion(int mask) {
    switch (mask) {
    case kCornerTopLeft: return "TL";
    case kCornerTopRight: return "TR";
    case kCornerBottomLeft: return "BL";
    case kCornerBottomRight: return "BR";
    case kCornerTopLeft | kCornerTopRight: return "T";
    case kCornerBottomLeft | kCornerBottomRight: return "B";
    case kCornerTopLeft | kCornerBottomLeft: return "L";
    case kCornerTopRight | kCornerBottomRight: return "R";
    case kCornerTopLeft | kCornerBottomRight: return "TL_and_BR";
    case kCornerTopRight | kCornerBottomLeft: return "BL_and_TR";
    default: return {};
    }
}

inline int cornerCount(int mask) {
    return (mask & 1) + ((mask >> 1) & 1) + ((mask >> 2) & 1) + ((mask >> 3) & 1);
}

// The CoastalCliff section for a cell with Base-219 and either Base-0 or
// Base-68, written the way the sections are named. The masks mark the Base-219
// corners and the Base-85 corners, and the rest are on lowHeight, which is 0 or
// 68. Without Base-85 the layouts are Base-219 on one corner, on everything but
// one corner, or on one side. With Base-85 they are Base-219 on one corner with
// Base-85 on the corner beside it, or Base-219 on one side with Base-85 on one of
// the other two corners. Any other layout returns an empty name.
inline std::string coastalCliffName(int lowHeight, int highMask, int midMask) {
    const int lowMask = kCornerAll & ~(highMask | midMask);
    if ((highMask & midMask) != 0 || highMask == 0 || lowMask == 0 || (lowHeight != 0 && lowHeight != 68)) {
        return {};
    }
    const auto diagonal = [](int mask) {
        return mask == (kCornerTopLeft | kCornerBottomRight) || mask == (kCornerTopRight | kCornerBottomLeft);
    };
    const std::string low = std::to_string(lowHeight);
    const int highs = cornerCount(highMask);
    const int mids = cornerCount(midMask);
    const int lows = cornerCount(lowMask);
    if (mids == 0) {
        if (highs == 1) {
            return "CC-219-" + std::string(cornerRegion(highMask)) + "-" + low + "-ALL";
        }
        if (highs == 3) {
            return "CC-219-ALL-" + low + "-" + std::string(cornerRegion(lowMask));
        }
        if (highs == 2 && !diagonal(highMask)) {
            return "CC-219-" + std::string(cornerRegion(highMask)) + "-" + low + "-" + std::string(cornerRegion(lowMask));
        }
        return {};
    }
    if (highs == 1 && mids == 1 && lows == 2 && !diagonal(lowMask)) {
        return "CC-219-" + std::string(cornerRegion(highMask)) + "-85-" + std::string(cornerRegion(midMask)) + "-" + low + "-" + std::string(cornerRegion(lowMask));
    }
    if (highs == 2 && mids == 1 && lows == 1 && !diagonal(highMask)) {
        return "CC-219-" + std::string(cornerRegion(highMask)) + "-85-" + std::string(cornerRegion(midMask)) + "-" + low + "-" + std::string(cornerRegion(lowMask));
    }
    return {};
}

enum class TransitionStyle {
    None,
    Cliff,
    Coast,
    ShallowCoast,
    ShallowDeep,
    CoastalCliffDeep,     // Base-219 beside Base-0
    CoastalCliffShallow,  // Base-219 beside Base-68
};

// Which pair of paints a family of pieces joins. Paints are named by height, so
// the rule matches Base-<lowerHeight> with Base-<upperHeight>.
struct BrushTransitionRule {
    int lowerHeight = 0;
    int upperHeight = 0;
    TransitionStyle style = TransitionStyle::None;
};

inline const std::array<BrushTransitionRule, 6>& brushTransitionRules() {
    static const std::array<BrushTransitionRule, 6> rules = {{
        {0, 85, TransitionStyle::Coast},
        {0, 68, TransitionStyle::ShallowDeep},
        {68, 85, TransitionStyle::ShallowCoast},
        {0, 219, TransitionStyle::CoastalCliffDeep},
        {68, 219, TransitionStyle::CoastalCliffShallow},
        {85, 219, TransitionStyle::Cliff},
    }};
    return rules;
}

// The section a mask needs for a family of pieces, without its variation number.
// Empty for flat cells and the two diagonal masks.
inline std::string transitionPieceName(TransitionStyle style, int mask) {
    switch (style) {
    case TransitionStyle::Cliff: return std::string(cliffPieceName(mask));
    case TransitionStyle::Coast: return std::string(coastPieceName(mask));
    case TransitionStyle::ShallowCoast: {
        const std::string_view coast = coastPieceName(mask);
        return coast.empty() ? std::string() : "Shallow" + std::string(coast);
    }
    case TransitionStyle::ShallowDeep: {
        // Named like the cliffs: Shallow-X has shallow water only at X, Shallow-T has
        // it along the top, and Deep-X has deep water only at X. The mask marks the
        // shallow corners.
        const std::string cliff(cliffPieceName(mask));
        if (cliff.rfind("Cliff-High-", 0) == 0) {
            return "ShallowDeep-Shallow-" + cliff.substr(11);
        }
        if (cliff.rfind("Cliff-Low-", 0) == 0) {
            return "ShallowDeep-Deep-" + cliff.substr(10);
        }
        return {};
    }
    case TransitionStyle::CoastalCliffDeep: return coastalCliffName(0, mask, 0);
    case TransitionStyle::CoastalCliffShallow: return coastalCliffName(68, mask, 0);
    default: return {};
    }
}

// The paints from lowest to highest and which of them are joined by pieces. A
// paint is called a level here, numbered by its place in that order.
class BrushGraph {
public:
    BrushGraph() = default;

    explicit BrushGraph(const std::vector<int>& paintHeights) : heights_(paintHeights) {
        const int count = static_cast<int>(heights_.size());
        const std::size_t cells = static_cast<std::size_t>(count) * static_cast<std::size_t>(count);
        styles_.assign(cells, TransitionStyle::None);
        distance_.assign(cells, kFar);
        hop_.assign(cells, 0);
        for (int a = 0; a < count; ++a) {
            for (int b = 0; b < count; ++b) {
                for (const BrushTransitionRule& rule : brushTransitionRules()) {
                    if (heights_[static_cast<std::size_t>(a)] == rule.lowerHeight && heights_[static_cast<std::size_t>(b)] == rule.upperHeight) {
                        styles_[index(a, b)] = rule.style;
                        styles_[index(b, a)] = rule.style;
                    }
                }
            }
        }
        for (int a = 0; a < count; ++a) {
            for (int b = 0; b < count; ++b) {
                hop_[index(a, b)] = b;
                distance_[index(a, b)] = a == b ? 0 : (styles_[index(a, b)] != TransitionStyle::None ? 1 : kFar);
            }
        }
        for (int via = 0; via < count; ++via) {
            for (int a = 0; a < count; ++a) {
                for (int b = 0; b < count; ++b) {
                    if (distance_[index(a, via)] + distance_[index(via, b)] < distance_[index(a, b)]) {
                        distance_[index(a, b)] = distance_[index(a, via)] + distance_[index(via, b)];
                        hop_[index(a, b)] = hop_[index(a, via)];
                    }
                }
            }
        }
    }

    int size() const { return static_cast<int>(heights_.size()); }
    int height(int level) const { return heights_[static_cast<std::size_t>(level)]; }
    TransitionStyle style(int a, int b) const { return styles_[index(a, b)]; }
    // Steps along the shortest chain of joined paints, so 0 to 219 passes through 85.
    int distance(int a, int b) const { return distance_[index(a, b)]; }
    int stepToward(int from, int target) const { return hop_[index(from, target)]; }
    // False when two paints are different and share no pieces, so they must not share a cell.
    // Paints with no chain between them cannot be fixed, so those count as fine.
    bool compatible(int a, int b) const { return a == b || style(a, b) != TransitionStyle::None || distance(a, b) >= kFar; }
    // True when the paint is joined to another one.
    bool connected(int level) const {
        for (int other = 0; other < size(); ++other) {
            if (other != level && style(level, other) != TransitionStyle::None) {
                return true;
            }
        }
        return false;
    }

    static constexpr int kFar = 1000000;

private:
    std::size_t index(int a, int b) const { return static_cast<std::size_t>(a) * heights_.size() + static_cast<std::size_t>(b); }

    std::vector<int> heights_;
    std::vector<TransitionStyle> styles_;
    std::vector<int> distance_;
    std::vector<int> hop_;
};

// Two corners on opposite diagonals have no picture, so a stroke grows or
// erases a corner until no cell is left like that.
inline bool cliffDiagonal(int mask) {
    return mask == (kCornerTopRight | kCornerBottomLeft) || mask == (kCornerTopLeft | kCornerBottomRight);
}

// The CoastDSL section for a cell with land, shallow water and deep water. The
// masks mark the land corners and the shallow corners, and the rest is deep
// water. Only some layouts have a section: land on one side with a shallow and a
// deep corner, one land corner with a shallow corner beside it and deep water
// along the next side, and one land corner opposite deep water with shallow
// water on the other diagonal. Any other layout returns an empty name.
inline std::string dslPieceName(int landMask, int shallowMask) {
    const int deepMask = kCornerAll & ~(landMask | shallowMask);
    if ((landMask & shallowMask) != 0 || landMask == 0 || shallowMask == 0 || deepMask == 0) {
        return {};
    }
    const int lands = cornerCount(landMask);
    const int shallows = cornerCount(shallowMask);
    const int deeps = cornerCount(deepMask);
    const auto diagonal = [](int mask) {
        return mask == (kCornerTopLeft | kCornerBottomRight) || mask == (kCornerTopRight | kCornerBottomLeft);
    };
    bool fits = false;
    if (lands == 2 && shallows == 1 && deeps == 1) {
        fits = !diagonal(landMask);
    } else if (lands == 1 && shallows == 1 && deeps == 2) {
        fits = !diagonal(deepMask);
    } else if (lands == 1 && shallows == 2 && deeps == 1) {
        fits = diagonal(landMask | deepMask);
    }
    if (!fits) {
        return {};
    }
    return "DSL-Land-" + std::string(cornerRegion(landMask)) + "-Shallow-" + std::string(cornerRegion(shallowMask)) + "-Water-" + std::string(cornerRegion(deepMask));
}

// The section a cell needs, from the level at each of its corners, without a
// variation number. Empty for a flat cell and for a layout that has no section.
// Two levels use the family that joins them. Base-0, Base-68 and Base-85
// together use a CoastDSL section, and Base-219, Base-85 and Base-0 or Base-68
// use a CoastalCliff section.
inline std::string brushPieceName(const BrushGraph& graph, const std::array<int, 4>& corners) {
    std::array<int, 4> sorted = corners;
    std::sort(sorted.begin(), sorted.end());
    const int distinct = static_cast<int>(std::unique(sorted.begin(), sorted.end()) - sorted.begin());
    const auto maskOf = [&](int level) {
        int mask = 0;
        for (int corner = 0; corner < 4; ++corner) {
            mask |= corners[static_cast<std::size_t>(corner)] == level ? (1 << corner) : 0;
        }
        return mask;
    };
    if (distinct == 2) {
        return transitionPieceName(graph.style(sorted[0], sorted[1]), maskOf(sorted[1]));
    }
    if (distinct == 3) {
        const int low = graph.height(sorted[0]);
        const int middle = graph.height(sorted[1]);
        const int high = graph.height(sorted[2]);
        if (low == 0 && middle == 68 && high == 85) {
            return dslPieceName(maskOf(sorted[2]), maskOf(sorted[1]));
        }
        if (middle == 85 && high == 219) {
            return coastalCliffName(low, maskOf(sorted[2]), maskOf(sorted[1]));
        }
    }
    return {};
}

// True when a cell with these corners has a picture: one level, or a layout that
// brushPieceName finds a section for.
inline bool brushCellValid(const BrushGraph& graph, const std::array<int, 4>& corners) {
    if (corners[0] == corners[1] && corners[0] == corners[2] && corners[0] == corners[3]) {
        return true;
    }
    return !brushPieceName(graph, corners).empty();
}

// The number after "<prefix>" in a section name, where prefix ends with a dash.
// Returns -1 when the name does not start with the prefix (ignoring case) or the
// ending is not all digits. "01" is 1, and any number of digits is accepted so
// later variations such as -05 match without a code change.
inline int numberedVariation(std::string_view name, std::string_view prefix) {
    if (prefix.empty() || name.size() <= prefix.size()) {
        return -1;
    }
    const auto fold = [](char character) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    };
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        if (fold(name[i]) != fold(prefix[i])) {
            return -1;
        }
    }
    int value = 0;
    for (std::size_t i = prefix.size(); i < name.size(); ++i) {
        if (name[i] < '0' || name[i] > '9') {
            return -1;
        }
        value = std::min(1000000, value * 10 + (name[i] - '0'));
    }
    return value;
}

// A paint whose strokes carry another name in the tileset. The Base-68 strokes
// are named Base-69, so a Base-68 brush uses them.
inline std::string_view brushStrokeAlias(std::string_view paint) {
    constexpr std::string_view base68 = "Base-68";
    if (paint.size() != base68.size()) {
        return {};
    }
    for (std::size_t i = 0; i < paint.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(paint[i])) != std::tolower(static_cast<unsigned char>(base68[i]))) {
            return {};
        }
    }
    return "Base-69";
}

// Variation number for a stroke named "<paint>-<size>x<size>-NN", such as
// Base-85-16x16-01. Returns -1 when the name is not a stroke of that paint and
// size. The paint name must match in full, so Base-85 never matches Base-8.
inline int brushStrokeVariation(std::string_view strokeName, std::string_view paint, int size) {
    if (paint.empty() || size <= 0) {
        return -1;
    }
    const std::string suffix = '-' + std::to_string(size) + 'x' + std::to_string(size) + '-';
    const int found = numberedVariation(strokeName, std::string(paint) + suffix);
    if (found >= 0) {
        return found;
    }
    const std::string_view alias = brushStrokeAlias(paint);
    return alias.empty() ? -1 : numberedVariation(strokeName, std::string(alias) + suffix);
}

// Variation number for a transition section such as Cliff-High-BR-02 or
// Coast-Water-B-01 when the piece is "Cliff-High-BR" or "Coast-Water-B".
// Cliff-High-T never matches Cliff-High-TL-01.
inline int transitionVariation(std::string_view sectionName, std::string_view piece) {
    if (piece.empty()) {
        return -1;
    }
    if (sectionName.size() == piece.size()) {
        // A section named exactly like the piece, such as DSL-Land-L-Shallow-BR-Water-TR,
        // is its only variation.
        bool same = true;
        for (std::size_t i = 0; i < piece.size() && same; ++i) {
            same = std::tolower(static_cast<unsigned char>(sectionName[i])) == std::tolower(static_cast<unsigned char>(piece[i]));
        }
        if (same) {
            return 0;
        }
    }
    std::string prefix(piece);
    prefix += '-';
    return numberedVariation(sectionName, prefix);
}

struct BrushNode {
    int x = 0;
    int y = 0;
    int level = 0;
};

// A cell that needs a new picture. Flat cells have mask 0 and sit on `level`.
// Transition cells have corners on `level` and `upper`, and the mask marks the
// corners on `upper`.
struct BrushCell {
    int x = 0;
    int y = 0;
    int level = 0;
    int upper = 0;
    int mask = 0;
    // The level at each corner: top left, top right, bottom left, bottom right.
    std::array<int, 4> corners = {0, 0, 0, 0};
};

struct BrushPlan {
    // Every corner the stroke sets, including corners moved to keep levels a step apart.
    std::vector<BrushNode> nodes;
    // Cells around the painted block that need a new picture. Cells whose
    // corners did not change are left alone.
    std::vector<BrushCell> cells;
};

// Plans a stroke over a square block of cells. The painted cells become flat on
// `level`. Corners that would end up in a cell with a paint they are not joined
// to move one step along the chain of joined paints toward the painted one,
// which makes the bands of transition cells around the block. readNode gives a
// corner's level before the stroke. Corners run from 0 to cellsX across and 0 to
// cellsY down, and the cell at (x, y) owns corners (x, y), (x + 1, y), (x, y + 1)
// and (x + 1, y + 1).
using BrushNodeReader = std::function<int(int nodeX, int nodeY)>;

inline BrushPlan planBrush(int cellsX, int cellsY, int blockX, int blockY, int blockCells, int level, const BrushGraph& graph, const BrushNodeReader& readNode) {
    BrushPlan plan;
    if (cellsX <= 0 || cellsY <= 0 || blockCells <= 0 || !readNode || level < 0 || level >= graph.size()) {
        return plan;
    }
    const auto inLattice = [&](int x, int y) {
        return x >= 0 && y >= 0 && x <= cellsX && y <= cellsY;
    };
    const auto isCell = [&](int cx, int cy) {
        return cx >= 0 && cy >= 0 && cx < cellsX && cy < cellsY;
    };
    const auto keyOf = [&](int x, int y) {
        return static_cast<long long>(y) * (cellsX + 1) + x;
    };
    std::unordered_map<long long, std::size_t> slots;

    const auto levelBefore = [&](int x, int y) {
        return inLattice(x, y) ? std::clamp(readNode(x, y), 0, graph.size() - 1) : 0;
    };
    // Corners of the painted block keep the painted level.
    const auto isLocked = [&](int x, int y) {
        return x >= blockX && x <= blockX + blockCells && y >= blockY && y <= blockY + blockCells;
    };
    const auto levelAfter = [&](int x, int y) {
        if (!inLattice(x, y)) {
            return 0;
        }
        const auto found = slots.find(keyOf(x, y));
        return found != slots.end() ? plan.nodes[found->second].level : levelBefore(x, y);
    };
    const auto setLevel = [&](int x, int y, int value) {
        if (!inLattice(x, y)) {
            return;
        }
        const long long key = keyOf(x, y);
        const auto found = slots.find(key);
        if (found != slots.end()) {
            plan.nodes[found->second].level = value;
            return;
        }
        slots.emplace(key, plan.nodes.size());
        plan.nodes.push_back(BrushNode{x, y, value});
    };

    std::vector<std::pair<int, int>> work;
    for (int cy = blockY; cy < blockY + blockCells; ++cy) {
        for (int cx = blockX; cx < blockX + blockCells; ++cx) {
            if (!isCell(cx, cy)) {
                continue;
            }
            for (int corner = 0; corner < 4; ++corner) {
                const int x = cx + (corner & 1);
                const int y = cy + (corner >> 1);
                if (slots.find(keyOf(x, y)) == slots.end()) {
                    setLevel(x, y, level);
                    work.emplace_back(x, y);
                }
            }
        }
    }

    // Settle the corners. A changed corner pulls any corner sharing a cell with
    // it onto a joined paint, and a cell left with its upper corners on a
    // diagonal moves toward the painted level. Each move is one step along a
    // chain of paints toward the painted one. The step limit only guards against
    // a tileset whose paints cannot all be joined.
    int steps = 0;
    const int stepLimit = 4000000;
    while (!work.empty() && steps++ < stepLimit) {
        const std::pair<int, int> node = work.back();
        work.pop_back();
        const int here = levelAfter(node.first, node.second);
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if ((dx == 0 && dy == 0) || !inLattice(node.first + dx, node.second + dy)) {
                    continue;
                }
                const int other = levelAfter(node.first + dx, node.second + dy);
                if (!graph.compatible(here, other)) {
                    setLevel(node.first + dx, node.second + dy, graph.stepToward(other, here));
                    work.emplace_back(node.first + dx, node.second + dy);
                }
            }
        }
        for (int cy = node.second - 1; cy <= node.second; ++cy) {
            for (int cx = node.first - 1; cx <= node.first; ++cx) {
                if (!isCell(cx, cy)) {
                    continue;
                }
                const std::array<int, 4> corners = {levelAfter(cx, cy), levelAfter(cx + 1, cy), levelAfter(cx, cy + 1), levelAfter(cx + 1, cy + 1)};
                const int lowest = *std::min_element(corners.begin(), corners.end());
                const int highest = *std::max_element(corners.begin(), corners.end());
                if (lowest == highest) {
                    continue;
                }
                bool onlyTwo = true;
                int mask = 0;
                for (int corner = 0; corner < 4; ++corner) {
                    const int value = corners[static_cast<std::size_t>(corner)];
                    onlyTwo = onlyTwo && (value == lowest || value == highest);
                    mask |= value == highest ? (1 << corner) : 0;
                }
                if (!onlyTwo) {
                    // Pairs that are not joined are settled by the corner rule above, so this
                    // only handles cells whose paints all join each other, such as 0, 68 and
                    // 85. Those are fine when a section draws their layout. Otherwise the
                    // fewest corners change, each moving toward the painted paint.
                    bool allJoined = true;
                    for (const int first : corners) {
                        for (const int second : corners) {
                            allJoined = allJoined && graph.compatible(first, second);
                        }
                    }
                    if (!allJoined || brushCellValid(graph, corners)) {
                        continue;
                    }
                    std::array<std::vector<int>, 4> options;
                    for (int corner = 0; corner < 4; ++corner) {
                        const int current = corners[static_cast<std::size_t>(corner)];
                        options[static_cast<std::size_t>(corner)].push_back(current);
                        if (!isLocked(cx + (corner & 1), cy + (corner >> 1))) {
                            for (int candidate = 0; candidate < graph.size(); ++candidate) {
                                if (candidate != current && graph.distance(level, candidate) < graph.distance(level, current)) {
                                    options[static_cast<std::size_t>(corner)].push_back(candidate);
                                }
                            }
                        }
                    }
                    bool found = false;
                    int fewest = 5;
                    int smallestMove = graph.kFar * 8;
                    std::array<int, 4> best = corners;
                    for (const int a : options[0]) {
                        for (const int b : options[1]) {
                            for (const int c : options[2]) {
                                for (const int d : options[3]) {
                                    const std::array<int, 4> trial = {a, b, c, d};
                                    if (!brushCellValid(graph, trial)) {
                                        continue;
                                    }
                                    int changes = 0;
                                    int move = 0;
                                    for (std::size_t i = 0; i < 4; ++i) {
                                        if (trial[i] != corners[i]) {
                                            ++changes;
                                            move += graph.distance(level, corners[i]) - graph.distance(level, trial[i]);
                                        }
                                    }
                                    if (changes < fewest || (changes == fewest && move < smallestMove)) {
                                        found = true;
                                        fewest = changes;
                                        smallestMove = move;
                                        best = trial;
                                    }
                                }
                            }
                        }
                    }
                    for (int corner = 0; corner < 4; ++corner) {
                        const std::size_t slot = static_cast<std::size_t>(corner);
                        const int x = cx + (corner & 1);
                        const int y = cy + (corner >> 1);
                        const int target = found ? best[slot] : (isLocked(x, y) ? corners[slot] : level);
                        if (target != corners[slot]) {
                            setLevel(x, y, target);
                            work.emplace_back(x, y);
                        }
                    }
                    continue;
                }
                if (!cliffDiagonal(mask)) {
                    continue;
                }
                const bool raise = graph.distance(level, highest) <= graph.distance(level, lowest);
                for (int corner = 0; corner < 4; ++corner) {
                    const int x = cx + (corner & 1);
                    const int y = cy + (corner >> 1);
                    const int current = corners[static_cast<std::size_t>(corner)];
                    if (raise && current == lowest) {
                        setLevel(x, y, highest);
                        work.emplace_back(x, y);
                    } else if (!raise && current == highest) {
                        setLevel(x, y, lowest);
                        work.emplace_back(x, y);
                    }
                }
            }
        }
    }

    std::vector<BrushCell> candidates;
    for (const BrushNode& node : plan.nodes) {
        for (int dy = -1; dy <= 0; ++dy) {
            for (int dx = -1; dx <= 0; ++dx) {
                const int cx = node.x + dx;
                const int cy = node.y + dy;
                const bool inBlock = cx >= blockX && cx < blockX + blockCells && cy >= blockY && cy < blockY + blockCells;
                if (isCell(cx, cy) && !inBlock) {
                    candidates.push_back(BrushCell{cx, cy, 0, 0, 0, {0, 0, 0, 0}});
                }
            }
        }
    }
    std::sort(candidates.begin(), candidates.end(), [](const BrushCell& left, const BrushCell& right) {
        return left.y != right.y ? left.y < right.y : left.x < right.x;
    });
    candidates.erase(std::unique(candidates.begin(), candidates.end(), [](const BrushCell& left, const BrushCell& right) {
        return left.x == right.x && left.y == right.y;
    }), candidates.end());
    for (BrushCell& cell : candidates) {
        const std::array<int, 4> corners = {levelAfter(cell.x, cell.y), levelAfter(cell.x + 1, cell.y), levelAfter(cell.x, cell.y + 1), levelAfter(cell.x + 1, cell.y + 1)};
        const std::array<int, 4> before = {levelBefore(cell.x, cell.y), levelBefore(cell.x + 1, cell.y), levelBefore(cell.x, cell.y + 1), levelBefore(cell.x + 1, cell.y + 1)};
        if (corners == before) {
            continue;
        }
        cell.corners = corners;
        cell.level = *std::min_element(corners.begin(), corners.end());
        cell.upper = *std::max_element(corners.begin(), corners.end());
        cell.mask = 0;
        if (cell.upper > cell.level) {
            for (int corner = 0; corner < 4; ++corner) {
                cell.mask |= corners[static_cast<std::size_t>(corner)] == cell.upper ? (1 << corner) : 0;
            }
        }
        plan.cells.push_back(cell);
    }
    return plan;
}

}  // namespace galaxy
