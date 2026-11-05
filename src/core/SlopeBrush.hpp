#pragma once

// Slope brush. A slope is a cliff between Base-85 and Base-219 that a unit can
// walk. Clicking a cliff turns that one cell into the slope with the same high
// ground. Neighbouring slopes are redrawn so a run stays smooth:
//
//   one cell            Single-High-B, Single-High-BL, ...
//   the middle of a run Slope-High-B, and Slope-High-BL where two runs meet
//   where a run meets a cliff, or a low corner meets one cliff
//                       Slope-High-B-TransitionL, Slope-Low-TL-TransitionT, ...
//
// The names and the corner bits are the same ones CliffTiles uses.

#include "core/BrushStrokes.hpp"
#include "core/RiverBrush.hpp"

#include <map>
#include <utility>

namespace galaxy {

struct SlopeShape {
    bool valid = false;
    // True when the cell is already a slope. A cliff is valid and not a slope.
    bool slope = false;
    // High-ground corners, using the same bits as cliffPieceName.
    int mask = 0;
};

inline int slopeCornerCount(int mask) {
    int count = 0;
    for (int bit = 1; bit <= kCornerAll; bit <<= 1) {
        count += (mask & bit) != 0 ? 1 : 0;
    }
    return count;
}

inline bool slopeStraight(int mask) {
    return mask == (kCornerTopLeft | kCornerTopRight) || mask == (kCornerBottomLeft | kCornerBottomRight)
        || mask == (kCornerTopLeft | kCornerBottomLeft) || mask == (kCornerTopRight | kCornerBottomRight);
}

// The sides of a cell where the high ground meets the low ground.
inline int slopeCliffSides(int mask) {
    int sides = 0;
    const auto differ = [&](int left, int right) { return ((mask & left) != 0) != ((mask & right) != 0); };
    if (differ(kCornerTopLeft, kCornerBottomLeft)) {
        sides |= kRiverLeft;
    }
    if (differ(kCornerTopRight, kCornerBottomRight)) {
        sides |= kRiverRight;
    }
    if (differ(kCornerTopLeft, kCornerTopRight)) {
        sides |= kRiverTop;
    }
    if (differ(kCornerBottomLeft, kCornerBottomRight)) {
        sides |= kRiverBottom;
    }
    return sides;
}

inline bool slopeEdgesMatch(int ourMask, int theirMask, int direction) {
    const auto high = [](int mask, int corner) { return (mask & corner) != 0; };
    switch (direction) {
    case kRiverLeft:
        return high(ourMask, kCornerTopLeft) == high(theirMask, kCornerTopRight)
            && high(ourMask, kCornerBottomLeft) == high(theirMask, kCornerBottomRight);
    case kRiverRight:
        return high(ourMask, kCornerTopRight) == high(theirMask, kCornerTopLeft)
            && high(ourMask, kCornerBottomRight) == high(theirMask, kCornerBottomLeft);
    case kRiverTop:
        return high(ourMask, kCornerTopLeft) == high(theirMask, kCornerBottomLeft)
            && high(ourMask, kCornerTopRight) == high(theirMask, kCornerBottomRight);
    case kRiverBottom:
        return high(ourMask, kCornerBottomLeft) == high(theirMask, kCornerTopLeft)
            && high(ourMask, kCornerBottomRight) == high(theirMask, kCornerTopRight);
    default:
        return false;
    }
}

inline void slopeStep(int direction, int& x, int& y) {
    if (direction == kRiverLeft) {
        --x;
    } else if (direction == kRiverRight) {
        ++x;
    } else if (direction == kRiverTop) {
        --y;
    } else if (direction == kRiverBottom) {
        ++y;
    }
}

inline const char* slopeCornerLetters(int bit) {
    switch (bit) {
    case kCornerTopLeft: return "TL";
    case kCornerTopRight: return "TR";
    case kCornerBottomLeft: return "BL";
    case kCornerBottomRight: return "BR";
    default: return nullptr;
    }
}

inline const char* slopeEdgeLetter(int mask) {
    switch (mask) {
    case kCornerTopLeft | kCornerTopRight: return "T";
    case kCornerBottomLeft | kCornerBottomRight: return "B";
    case kCornerTopLeft | kCornerBottomLeft: return "L";
    case kCornerTopRight | kCornerBottomRight: return "R";
    default: return nullptr;
    }
}

// "Cliff-High-BL-02" and "Slope-Low-TL-TransitionT" share one shape.
inline SlopeShape parseSlopeShape(std::string_view label) {
    SlopeShape shape;
    const std::string key = riverLowerText(riverKeyOf(label));
    std::string_view rest;
    bool low = false;
    if (key.starts_with("cliff-high-")) {
        rest = std::string_view(key).substr(11);
    } else if (key.starts_with("cliff-low-")) {
        rest = std::string_view(key).substr(10);
        low = true;
    } else if (key.starts_with("slope-high-")) {
        rest = std::string_view(key).substr(11);
        shape.slope = true;
    } else if (key.starts_with("slope-low-")) {
        rest = std::string_view(key).substr(10);
        shape.slope = true;
        low = true;
    } else if (key.starts_with("single-high-")) {
        rest = std::string_view(key).substr(12);
        shape.slope = true;
    } else {
        return {};
    }
    const std::size_t transition = rest.find("-transition");
    if (transition != std::string_view::npos) {
        rest = rest.substr(0, transition);
    }
    int high = 0;
    if (rest == "tl") {
        high = kCornerTopLeft;
    } else if (rest == "tr") {
        high = kCornerTopRight;
    } else if (rest == "bl") {
        high = kCornerBottomLeft;
    } else if (rest == "br") {
        high = kCornerBottomRight;
    } else if (rest == "t") {
        high = kCornerTopLeft | kCornerTopRight;
    } else if (rest == "b") {
        high = kCornerBottomLeft | kCornerBottomRight;
    } else if (rest == "l") {
        high = kCornerTopLeft | kCornerBottomLeft;
    } else if (rest == "r") {
        high = kCornerTopRight | kCornerBottomRight;
    } else {
        return {};
    }
    if (low) {
        if (slopeCornerCount(high) != 1) {
            return {};
        }
        shape.mask = kCornerAll & ~high;
    } else {
        shape.mask = high;
    }
    shape.valid = slopeCliffSides(shape.mask) != 0;
    return shape;
}

// The slope tile for this high-ground mask. continuing is the kRiver* sides
// whose neighbour is already a slope that should meet this cell smoothly.
inline std::string slopePieceName(int mask, int continuing) {
    const int got = continuing & slopeCliffSides(mask);
    int count = 0;
    int only = 0;
    for (int bit = 1; bit <= kRiverAllSides; bit <<= 1) {
        if ((got & bit) != 0) {
            ++count;
            only = bit;
        }
    }
    if (slopeStraight(mask)) {
        const char* edge = slopeEdgeLetter(mask);
        if (edge == nullptr) {
            return {};
        }
        if (count == 0) {
            return std::string("Single-High-") + edge;
        }
        if (count >= 2) {
            return std::string("Slope-High-") + edge;
        }
        return std::string("Slope-High-") + edge + "-Transition" + riverLetterOfSide(only);
    }
    if (slopeCornerCount(mask) == 1) {
        const char* corner = slopeCornerLetters(mask);
        if (corner == nullptr) {
            return {};
        }
        if (count >= 2) {
            return std::string("Slope-High-") + corner;
        }
        return std::string("Single-High-") + corner;
    }
    if (slopeCornerCount(mask) == 3) {
        const char* corner = slopeCornerLetters(kCornerAll & ~mask);
        if (corner == nullptr) {
            return {};
        }
        if (count == 1) {
            return std::string("Slope-Low-") + corner + "-Transition" + riverLetterOfSide(only);
        }
        return std::string("Slope-Low-") + corner;
    }
    return {};
}

// Converts the cliff under the brush, and redraws the slopes that join it.
// `available` names the slope pieces this tileset actually has. An empty function
// plans as if every piece exists, which is how the GreenWorld slopes behave.
// A tileset that only has the straight singles (Mars) keeps those singles beside
// each other on the 16 x 16 cells, and refuses a cliff corner.
inline RiverPlan planSlope(int x, int y, const RiverLookup& lookup, const std::function<bool(std::string_view)>& available = {}) {
    RiverPlan plan;
    const SlopeShape here = parseSlopeShape(lookup(x, y).label);
    if (!here.valid) {
        const RiverCell cell = lookup(x, y);
        std::string text = "Slopes cannot go on ";
        text += cell.label.empty() ? "this tile" : cell.label;
        text += "\nSlopes only go on cliffs between Base-85 and Base-219";
        plan.reason = std::move(text);
        return plan;
    }

    // No Slope-High or Slope-Low pieces means the run cannot bend or meet in the
    // middle. Neighbouring straight slopes stay singles, one 16 x 16 cell each.
    const bool straightOnly = static_cast<bool>(available) && !available("Slope-High-B") && !available("Slope-Low-TL");
    if (straightOnly && !slopeStraight(here.mask)) {
        const RiverCell cell = lookup(x, y);
        std::string text = "Slopes cannot go on ";
        text += cell.label.empty() ? "this corner" : cell.label;
        text += "\nThese slopes only follow straight cliffs";
        plan.reason = std::move(text);
        return plan;
    }

    std::map<std::pair<int, int>, int> slopes;
    std::vector<std::pair<int, int>> pending;
    const auto add = [&](int cx, int cy, int mask) {
        if (slopes.emplace(std::pair<int, int>{cx, cy}, mask).second) {
            pending.push_back({cx, cy});
        }
    };
    add(x, y, here.mask);
    if (!straightOnly) {
        for (std::size_t index = 0; index < pending.size(); ++index) {
            const int cx = pending[index].first;
            const int cy = pending[index].second;
            const int mask = slopes[{cx, cy}];
            for (int direction = 1; direction <= kRiverAllSides; direction <<= 1) {
                if ((slopeCliffSides(mask) & direction) == 0) {
                    continue;
                }
                int nx = cx;
                int ny = cy;
                slopeStep(direction, nx, ny);
                if (slopes.find({nx, ny}) != slopes.end()) {
                    continue;
                }
                const SlopeShape next = parseSlopeShape(lookup(nx, ny).label);
                // A cliff the brush did not click stays a cliff. Slopes already placed join the run.
                if (!next.valid || !next.slope || !slopeEdgesMatch(mask, next.mask, direction)) {
                    continue;
                }
                add(nx, ny, next.mask);
            }
        }
    }

    const auto inRun = [&](int cx, int cy) { return slopes.find({cx, cy}) != slopes.end(); };
    const auto gentle = [&](int cx, int cy, int mask, int back) {
        if (slopeStraight(mask)) {
            return true;
        }
        int present = 0;
        for (int direction = 1; direction <= kRiverAllSides; direction <<= 1) {
            if ((slopeCliffSides(mask) & direction) == 0) {
                continue;
            }
            int nx = cx;
            int ny = cy;
            slopeStep(direction, nx, ny);
            if (inRun(nx, ny) && slopeEdgesMatch(mask, slopes[{nx, ny}], direction)) {
                present |= direction;
            }
        }
        if (slopeCornerCount(mask) == 1) {
            return present == slopeCliffSides(mask);
        }
        if (present == 0) {
            return true;
        }
        return (present & back) != 0;
    };

    const auto continuing = [&](int cx, int cy, int mask) {
        int sides = 0;
        for (int direction = 1; direction <= kRiverAllSides; direction <<= 1) {
            if ((slopeCliffSides(mask) & direction) == 0) {
                continue;
            }
            int nx = cx;
            int ny = cy;
            slopeStep(direction, nx, ny);
            const auto found = slopes.find({nx, ny});
            if (found == slopes.end() || !slopeEdgesMatch(mask, found->second, direction)) {
                continue;
            }
            if (gentle(nx, ny, found->second, oppositeRiverSide(direction))) {
                sides |= direction;
            }
        }
        return sides;
    };

    const std::pair<int, int> seed{x, y};
    std::vector<std::pair<int, int>> order;
    order.push_back(seed);
    for (const auto& placed : slopes) {
        if (placed.first != seed) {
            order.push_back(placed.first);
        }
    }
    for (const auto& cell : order) {
        const int mask = slopes[cell];
        const std::string wanted = slopePieceName(mask, continuing(cell.first, cell.second, mask));
        if (wanted.empty()) {
            continue;
        }
        const std::string current = riverKeyOf(lookup(cell.first, cell.second).label);
        if (!sameRiverKey(current, wanted)) {
            plan.edits.push_back(RiverEdit{cell.first, cell.second, wanted});
        }
    }
    if (available) {
        for (const RiverEdit& edit : plan.edits) {
            if (available(edit.key)) {
                continue;
            }
            if (edit.x == x && edit.y == y) {
                const RiverCell cell = lookup(x, y);
                plan.edits.clear();
                std::string text = "Slopes cannot go on ";
                text += cell.label.empty() ? "this tile" : cell.label;
                text += "\nThese slopes only follow straight cliffs";
                plan.reason = std::move(text);
                return plan;
            }
        }
        plan.edits.erase(
            std::remove_if(plan.edits.begin(), plan.edits.end(), [&](const RiverEdit& edit) { return !available(edit.key); }),
            plan.edits.end());
    }
    if (plan.edits.empty()) {
        plan.reason = "A slope is already here";
    }
    return plan;
}

}  // namespace galaxy
