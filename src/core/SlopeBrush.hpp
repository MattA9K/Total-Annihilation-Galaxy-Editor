#pragma once

// Slope brush. A slope is a cliff between Base-85 and Base-219 that a unit can
// walk. Clicking a cliff turns that one cell into the slope with the same high
// ground. A family number, such as 255, is the same idea for another cliff:
// Metal's Base-139 to Base-255 slopes are Single-255-High-B and the other three
// straight edges. Family 202 is Archipelago's Base-78 to Base-194 slopes. Those
// cliffs keep the plain Cliff-High names. Family 155 is Desert's Base-80 to
// Base-155 slopes, and those cliffs keep the plain Cliff-High names too. Family
// 230 is Desert's Base-155 to Base-230 slopes, and those read the Cliff-230
// cliffs. Both Desert sets have the Mars set: four straight singles and four high-corner singles, with no
// run pieces, so a neighbour stays a single and a corner cliff is refused. A
// corner alone is Single-202-High-TL or Single-202-Low-BR. One neighbouring slope uses Slope-202-High-TL-TransitionT,
// and both arms use Slope-202-High-TL. Neighbouring slopes are redrawn so a run stays smooth:
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

// "Single-High-B" stays plain. Family 255 names the same shape "Single-255-High-B".
inline std::string slopeQualify(std::string name, int family) {
    if (family <= 0 || name.empty()) {
        return name;
    }
    const std::size_t dash = name.find('-');
    if (dash == std::string::npos) {
        return name;
    }
    name.insert(dash + 1, std::to_string(family) + "-");
    return name;
}

// "Cliff-High-BL-02" and "Slope-Low-TL-TransitionT" share one shape. Family 0 is
// that plain name. Family 255 reads Cliff-255-High-B and Single-255-High-B.
inline SlopeShape parseSlopeShape(std::string_view label, int family = 0) {
    SlopeShape shape;
    const std::string key = riverLowerText(riverKeyOf(label));
    struct Taken {
        bool matched = false;
        std::string_view rest;
    };
    const auto take = [&](std::string_view prefix, bool qualify) {
        Taken taken;
        const std::string qualified = qualify ? slopeQualify(std::string(prefix), family) : std::string(prefix);
        if (!key.starts_with(qualified)) {
            return taken;
        }
        taken.matched = true;
        taken.rest = std::string_view(key).substr(qualified.size());
        return taken;
    };
// Family 255 cliffs are Cliff-255-High. Family 230 cliffs are Cliff-230-High.
// Archipelago's family 202 and Desert's family 155 read the plain Cliff-High
// cliffs and keep the family number in the slope names only.
const bool qualifiedCliffs = family == 255 || family == 230;
const Taken cliffHigh = take("cliff-high-", qualifiedCliffs);
const Taken cliffLow = take("cliff-low-", qualifiedCliffs);
const Taken slopeHigh = take("slope-high-", true);
const Taken slopeLow = take("slope-low-", true);
const Taken singleHigh = take("single-high-", true);
const Taken singleLow = take("single-low-", true);
std::string_view rest;
bool low = false;
if (cliffHigh.matched) {
    rest = cliffHigh.rest;
} else if (cliffLow.matched) {
    rest = cliffLow.rest;
    low = true;
} else if (slopeHigh.matched) {
    rest = slopeHigh.rest;
    shape.slope = true;
} else if (slopeLow.matched) {
    rest = slopeLow.rest;
    shape.slope = true;
    low = true;
} else if (singleHigh.matched) {
    rest = singleHigh.rest;
    shape.slope = true;
} else if (singleLow.matched) {
    rest = singleLow.rest;
    shape.slope = true;
    low = true;
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
inline std::string slopePieceName(int mask, int continuing, int family = 0) {
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
            return slopeQualify(std::string("Single-High-") + edge, family);
        }
        if (count >= 2) {
            return slopeQualify(std::string("Slope-High-") + edge, family);
        }
        return slopeQualify(std::string("Slope-High-") + edge + "-Transition" + riverLetterOfSide(only), family);
    }
    if (slopeCornerCount(mask) == 1) {
        const char* corner = slopeCornerLetters(mask);
        if (corner == nullptr) {
            return {};
        }
        if (count >= 2) {
            return slopeQualify(std::string("Slope-High-") + corner, family);
        }
        // Archipelago has a one-sided corner for each arm. GreenWorld keeps the
        // single until both arms are slopes.
        if (count == 1 && family == 202) {
            return slopeQualify(std::string("Slope-High-") + corner + "-Transition" + riverLetterOfSide(only), family);
        }
        return slopeQualify(std::string("Single-High-") + corner, family);
    }
    if (slopeCornerCount(mask) == 3) {
        const char* corner = slopeCornerLetters(kCornerAll & ~mask);
        if (corner == nullptr) {
            return {};
        }
        if (count == 1) {
            return slopeQualify(std::string("Slope-Low-") + corner + "-Transition" + riverLetterOfSide(only), family);
        }
        // An Archipelago corner that meets no other slope is a Single. The
        // Slope-Low picture is the one used once both arms are slopes.
        if (count == 0 && family == 202) {
            return slopeQualify(std::string("Single-Low-") + corner, family);
        }
        return slopeQualify(std::string("Slope-Low-") + corner, family);
    }
    return {};
}

// Converts the cliff under the brush, and redraws the slopes that join it.
// `available` names the slope pieces this tileset actually has. An empty function
// plans as if every piece exists, which is how the GreenWorld slopes behave.
// A tileset that only has the straight singles (Mars) keeps those singles beside
// each other on the 16 x 16 cells, and refuses a cliff corner.
inline RiverPlan planSlope(int x, int y, const RiverLookup& lookup, const std::function<bool(std::string_view)>& available = {}, int family = 0) {
    RiverPlan plan;
    const SlopeShape here = parseSlopeShape(lookup(x, y).label, family);
    if (!here.valid) {
        const RiverCell cell = lookup(x, y);
        std::string text = "Slopes cannot go on ";
        text += cell.label.empty() ? "this tile" : cell.label;
        const char* range = "\nSlopes only go on cliffs between Base-85 and Base-219";
        if (family == 255) {
            range = "\nSlopes only go on cliffs between Base-139 and Base-255";
        } else if (family == 202) {
            range = "\nSlopes only go on cliffs between Base-78 and Base-194";
        } else if (family == 155) {
            range = "\nSlopes only go on cliffs between Base-80 and Base-155";
        } else if (family == 230) {
            range = "\nSlopes only go on cliffs between Base-155 and Base-230";
        }
        text += range;
        plan.reason = std::move(text);
        return plan;
    }

    // No Slope-High or Slope-Low pieces means the run cannot bend or meet in the
    // middle. Neighbouring straight slopes stay singles, one 16 x 16 cell each.
    const bool straightOnly = static_cast<bool>(available) && !available(slopeQualify("Slope-High-B", family)) && !available(slopeQualify("Slope-Low-TL", family));
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
                const SlopeShape next = parseSlopeShape(lookup(nx, ny).label, family);
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
            // Family 202's one-sided corner is itself a joining slope, so the
            // arm beside it becomes a transition too.
            if (family == 202 && (present & back) != 0) {
                return true;
            }
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
        std::string wanted = slopePieceName(mask, continuing(cell.first, cell.second, mask), family);
        if (wanted.empty()) {
            continue;
        }
        // A tileset that has no picture for one join uses the slope that meets
        // on both sides, then the single. The click still places a real slope.
        if (available && !available(wanted)) {
            const std::string marker = "-Transition";
            const auto cut = wanted.rfind(marker);
            if (cut != std::string::npos) {
                const std::string plain = wanted.substr(0, cut);
                if (available(plain)) {
                    wanted = plain;
                } else if (plain.starts_with("Slope-")) {
                    const std::string single = "Single-" + plain.substr(6);
                    if (available(single)) {
                        wanted = single;
                    }
                }
            }
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
