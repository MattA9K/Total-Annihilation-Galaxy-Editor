#pragma once

// River and river-bridge brushes. A river is one 16 x 16 cell. Cells join their
// neighbours like roads do: the piece on a cell is chosen from which of its four
// sides carry a river. The rules here are plain functions so they can be tested
// without a map or a window.
//
//   River pieces      River-Base, River-Horizontal, River-Vertical, River-R-B ...,
//                     RiverIntersect-3way-..., RiverIntersect-4way
//   River mouths      River-Mouth-T/B/L/R sit on Coast-Water-T/B/L/R. The water is on
//                     the side named, and the river enters from the opposite side.
//   Bridge pieces     RiverB-Horizontal, RiverB-Vertical, RiverB-R-B, ... mirror the
//                     River- names, so a bridge goes where the River- name matches.
//                     RiverB-4way is the bridge for a 4-way crossing. A tileset that
//                     has no such section keeps the crossing without a bridge.
//
// Ridges follow the same rules with their own names:
//   Ridge pieces      Ridge-Base, Ridge-End-X, Ridge-Horizontal, Ridge-Vertical,
//                     Ridge-Corner-R-B ..., Ridge-3Way-..., Ridge-4Way
//   Ridge mouths      Ridge-Mouth-T/B/L/R sit on Cliff-High-T/B/L/R. The high ground is on
//                     the side named, and the ridge runs away from it.
//   Spacer pieces     Ridge-L_R-Spacer and Ridge-T_B-Spacer go on Ridge-Horizontal and
//                     Ridge-Vertical.

#include "format/SctSection.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace galaxy {

inline constexpr int kRiverTop = 1;
inline constexpr int kRiverRight = 2;
inline constexpr int kRiverBottom = 4;
inline constexpr int kRiverLeft = 8;
inline constexpr int kRiverAllSides = 15;

// The only flat bases a river may be painted on. Most lands are Base-85.
// Desert land is Base-80.
inline constexpr std::string_view kRiverLandBase = "Base-85";
inline constexpr std::string_view kRiverDesertLand = "Base-80";

inline int oppositeRiverSide(int side) {
    switch (side) {
    case kRiverTop: return kRiverBottom;
    case kRiverBottom: return kRiverTop;
    case kRiverLeft: return kRiverRight;
    case kRiverRight: return kRiverLeft;
    default: return 0;
    }
}

inline std::string riverLowerText(std::string_view text) {
    std::string lowered(text);
    for (char& character : lowered) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return lowered;
}

inline bool sameRiverKey(std::string_view left, std::string_view right) {
    return riverLowerText(left) == riverLowerText(right);
}

inline bool riverLandLabel(std::string_view label) {
    return sameRiverKey(label, kRiverLandBase) || sameRiverKey(label, kRiverDesertLand);
}

// Drops a trailing "-NN" variation number: "River-Horizontal-02" -> "River-Horizontal".
inline std::string riverKeyOf(std::string_view name) {
    std::size_t end = name.size();
    std::size_t digits = end;
    while (digits > 0 && name[digits - 1] >= '0' && name[digits - 1] <= '9') {
        --digits;
    }
    if (digits < end && digits > 0 && name[digits - 1] == '-') {
        end = digits - 1;
    }
    return std::string(name.substr(0, end));
}

inline int riverSideOfLetter(std::string_view letter) {
    if (letter.size() != 1) {
        return 0;
    }
    switch (std::tolower(static_cast<unsigned char>(letter.front()))) {
    case 't': return kRiverTop;
    case 'r': return kRiverRight;
    case 'b': return kRiverBottom;
    case 'l': return kRiverLeft;
    default: return 0;
    }
}

inline char riverLetterOfSide(int side) {
    switch (side) {
    case kRiverTop: return 'T';
    case kRiverRight: return 'R';
    case kRiverBottom: return 'B';
    case kRiverLeft: return 'L';
    default: return '?';
    }
}

enum class LineFamily { River, Ridge };

struct RiverPiece {
    bool valid = false;
    // A bridge over a river, or a spacer in a ridge: a fixed piece that replaces a straight one.
    bool bridge = false;
    bool mouth = false;
    LineFamily family = LineFamily::River;
    // The sides where the water runs on to the next cell.
    int sides = 0;
    // A plain river cell is rebuilt to follow its neighbours. Bridges and mouths keep their shape.
    bool flexible = false;
    std::string key;
};

// Reads "-"-separated side letters, such as "R-B" or "T-L-R".
inline int riverSidesFromLetters(std::string_view text) {
    int sides = 0;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t dash = text.find('-', start);
        const std::string_view part = text.substr(start, dash == std::string_view::npos ? std::string_view::npos : dash - start);
        const int side = riverSideOfLetter(part);
        if (side == 0) {
            return -1;
        }
        sides |= side;
        if (dash == std::string_view::npos) {
            break;
        }
        start = dash + 1;
    }
    return sides;
}

inline int riverSidesFromShape(std::string_view shape) {
    const std::string lowered = riverLowerText(shape);
    if (lowered == "horizontal") {
        return kRiverLeft | kRiverRight;
    }
    if (lowered == "vertical") {
        return kRiverTop | kRiverBottom;
    }
    return riverSidesFromLetters(shape);
}

inline RiverPiece parseRiverPiece(std::string_view name) {
    RiverPiece piece;
    piece.key = riverKeyOf(name);
    const std::string lowered = riverLowerText(piece.key);
    const std::string_view key = piece.key;
    if (lowered.starts_with("riverintersect-")) {
        const std::string rest = lowered.substr(15);
        if (rest == "4way") {
            piece.sides = kRiverAllSides;
        } else if (rest.starts_with("3way-")) {
            const int sides = riverSidesFromLetters(key.substr(20));
            if (sides <= 0) {
                return RiverPiece{};
            }
            piece.sides = sides;
        } else {
            return RiverPiece{};
        }
        piece.valid = true;
        piece.flexible = true;
        return piece;
    }
    if (lowered.starts_with("riverb-")) {
        if (riverLowerText(key.substr(7)) == "4way") {
            piece.valid = true;
            piece.bridge = true;
            piece.sides = kRiverAllSides;
            return piece;
        }
        const int sides = riverSidesFromShape(key.substr(7));
        if (sides <= 0) {
            return RiverPiece{};
        }
        piece.valid = true;
        piece.bridge = true;
        piece.sides = sides;
        return piece;
    }
    if (lowered.starts_with("river-")) {
        const std::string_view rest = key.substr(6);
        const std::string restLower = riverLowerText(rest);
        if (restLower == "base") {
            piece.valid = true;
            piece.flexible = true;
            return piece;
        }
        if (restLower.starts_with("mouth-")) {
            const int water = riverSideOfLetter(rest.substr(6));
            if (water == 0) {
                return RiverPiece{};
            }
            piece.valid = true;
            piece.mouth = true;
            piece.sides = oppositeRiverSide(water);
            return piece;
        }
        const int sides = riverSidesFromShape(rest);
        if (sides <= 0) {
            return RiverPiece{};
        }
        piece.valid = true;
        piece.flexible = true;
        piece.sides = sides;
        return piece;
    }
    return RiverPiece{};
}

inline RiverPiece parseRidgePiece(std::string_view name) {
    RiverPiece piece;
    piece.family = LineFamily::Ridge;
    piece.key = riverKeyOf(name);
    const std::string lowered = riverLowerText(piece.key);
    if (!lowered.starts_with("ridge-")) {
        return RiverPiece{};
    }
    const std::string_view rest = std::string_view(piece.key).substr(6);
    const std::string restLower = lowered.substr(6);
    const auto fixedSides = [&](int sides, bool spacer) {
        piece.valid = true;
        piece.bridge = spacer;
        piece.flexible = !spacer;
        piece.sides = sides;
        return piece;
    };
    if (restLower == "base") {
        return fixedSides(0, false);
    }
    if (restLower == "horizontal") {
        return fixedSides(kRiverLeft | kRiverRight, false);
    }
    if (restLower == "vertical") {
        return fixedSides(kRiverTop | kRiverBottom, false);
    }
    if (restLower == "4way") {
        return fixedSides(kRiverAllSides, false);
    }
    if (restLower == "l_r-spacer") {
        return fixedSides(kRiverLeft | kRiverRight, true);
    }
    if (restLower == "t_b-spacer") {
        return fixedSides(kRiverTop | kRiverBottom, true);
    }
    if (restLower.starts_with("end-")) {
        const int side = riverSideOfLetter(rest.substr(4));
        return side == 0 ? RiverPiece{} : fixedSides(side, false);
    }
    if (restLower.starts_with("mouth-")) {
        const int high = riverSideOfLetter(rest.substr(6));
        if (high == 0) {
            return RiverPiece{};
        }
        piece.valid = true;
        piece.mouth = true;
        piece.sides = oppositeRiverSide(high);
        return piece;
    }
    if (restLower.starts_with("corner-") || restLower.starts_with("3way-")) {
        const int sides = riverSidesFromLetters(rest.substr(restLower.starts_with("corner-") ? 7 : 5));
        return sides <= 0 ? RiverPiece{} : fixedSides(sides, false);
    }
    return RiverPiece{};
}

// River or ridge, by name.
inline RiverPiece parseLinePiece(std::string_view name) {
    const RiverPiece river = parseRiverPiece(name);
    return river.valid ? river : parseRidgePiece(name);
}

// The piece a cell needs when the river leaves it through these sides. A cell with
// one side carries on straight, and a cell with none is a small pond.
inline std::string riverKeyForSides(int sides) {
    sides &= kRiverAllSides;
    const int vertical = sides & (kRiverTop | kRiverBottom);
    const int horizontal = sides & (kRiverLeft | kRiverRight);
    switch (sides) {
    case 0: return "River-Base";
    case kRiverAllSides: return "RiverIntersect-4way";
    case kRiverTop | kRiverRight | kRiverBottom: return "RiverIntersect-3way-T-R-B";
    case kRiverTop | kRiverLeft | kRiverBottom: return "RiverIntersect-3way-T-L-B";
    case kRiverTop | kRiverLeft | kRiverRight: return "RiverIntersect-3way-T-L-R";
    case kRiverLeft | kRiverRight | kRiverBottom: return "RiverIntersect-3way-L-R-B";
    default: break;
    }
    if (horizontal == 0) {
        return "River-Vertical";
    }
    if (vertical == 0) {
        return "River-Horizontal";
    }
    // One side from each axis is a bend. The sides read in the name order T, R, B, L.
    if (vertical == kRiverTop && horizontal == kRiverRight) {
        return "River-T-R";
    }
    if (vertical == kRiverTop && horizontal == kRiverLeft) {
        return "River-T-L";
    }
    if (vertical == kRiverBottom && horizontal == kRiverRight) {
        return "River-R-B";
    }
    return "River-L-B";
}

inline std::string riverMouthKey(int waterSide) {
    if (waterSide != kRiverTop && waterSide != kRiverRight && waterSide != kRiverBottom && waterSide != kRiverLeft) {
        return {};
    }
    return std::string("River-Mouth-") + riverLetterOfSide(waterSide);
}

// "River-Horizontal" -> "RiverB-Horizontal". Straights, the four bends, and a 4-way
// crossing have a bridge. Ponds, 3-way junctions and mouths give an empty name.
inline std::string riverBridgeKey(const RiverPiece& piece) {
    if (!piece.valid || piece.bridge || piece.mouth) {
        return {};
    }
    const std::string lowered = riverLowerText(piece.key);
    if (lowered == "riverintersect-4way") {
        return "RiverB-4way";
    }
    if (!lowered.starts_with("river-") || lowered == "river-base") {
        return {};
    }
    return "RiverB-" + piece.key.substr(6);
}

// Ridges end instead of carrying on: one side makes Ridge-End-X, and no side a lone Ridge-Base.
inline std::string ridgeKeyForSides(int sides) {
    sides &= kRiverAllSides;
    const int vertical = sides & (kRiverTop | kRiverBottom);
    const int horizontal = sides & (kRiverLeft | kRiverRight);
    switch (sides) {
    case 0: return "Ridge-Base";
    case kRiverTop: return "Ridge-End-T";
    case kRiverRight: return "Ridge-End-R";
    case kRiverBottom: return "Ridge-End-B";
    case kRiverLeft: return "Ridge-End-L";
    case kRiverAllSides: return "Ridge-4Way";
    case kRiverTop | kRiverBottom: return "Ridge-Vertical";
    case kRiverLeft | kRiverRight: return "Ridge-Horizontal";
    case kRiverTop | kRiverRight | kRiverBottom: return "Ridge-3Way-T-R-B";
    case kRiverTop | kRiverLeft | kRiverBottom: return "Ridge-3Way-T-L-B";
    case kRiverTop | kRiverLeft | kRiverRight: return "Ridge-3Way-T-L-R";
    case kRiverLeft | kRiverRight | kRiverBottom: return "Ridge-3Way-L-R-B";
    default: break;
    }
    if (vertical == kRiverTop && horizontal == kRiverRight) {
        return "Ridge-Corner-T-R";
    }
    if (vertical == kRiverTop && horizontal == kRiverLeft) {
        return "Ridge-Corner-T-L";
    }
    if (vertical == kRiverBottom && horizontal == kRiverRight) {
        return "Ridge-Corner-R-B";
    }
    return "Ridge-Corner-L-B";
}

inline std::string ridgeMouthKey(int highSide) {
    if (highSide != kRiverTop && highSide != kRiverRight && highSide != kRiverBottom && highSide != kRiverLeft) {
        return {};
    }
    return std::string("Ridge-Mouth-") + riverLetterOfSide(highSide);
}

// Ridge-Vertical takes Ridge-T_B-Spacer, and Ridge-Horizontal takes Ridge-L_R-Spacer.
inline std::string ridgeSpacerKey(const RiverPiece& piece) {
    if (!piece.valid || piece.family != LineFamily::Ridge || piece.bridge || piece.mouth) {
        return {};
    }
    const std::string lowered = riverLowerText(piece.key);
    if (lowered == "ridge-vertical") {
        return "Ridge-T_B-Spacer";
    }
    if (lowered == "ridge-horizontal") {
        return "Ridge-L_R-Spacer";
    }
    return {};
}

inline std::string lineKeyForSides(LineFamily family, int sides) {
    return family == LineFamily::Ridge ? ridgeKeyForSides(sides) : riverKeyForSides(sides);
}

inline std::string lineMouthKey(LineFamily family, int side) {
    return family == LineFamily::Ridge ? ridgeMouthKey(side) : riverMouthKey(side);
}

inline std::string lineOverlayKey(const RiverPiece& piece) {
    return piece.family == LineFamily::Ridge ? ridgeSpacerKey(piece) : riverBridgeKey(piece);
}

// Cliff-High-T has high ground along the top. Corners and other cliffs give 0.
inline int cliffHighSide(std::string_view name) {
    const std::string key = riverLowerText(riverKeyOf(name));
    constexpr std::string_view prefix = "cliff-high-";
    if (!key.starts_with(prefix)) {
        return 0;
    }
    return riverSideOfLetter(std::string_view(key).substr(prefix.size()));
}

// Coast-Water-T has water along the top. Corner pieces and other coasts give 0.
inline int coastWaterSide(std::string_view name) {
    const std::string key = riverLowerText(riverKeyOf(name));
    constexpr std::string_view prefix = "coast-water-";
    if (!key.starts_with(prefix)) {
        return 0;
    }
    return riverSideOfLetter(std::string_view(key).substr(prefix.size()));
}

// What a 16 x 16 cell on the map holds, as far as rivers care.
struct RiverCell {
    // Coast and Cliff are the straight edges where a river or a ridge can start.
    enum class Kind { Other, Land, Coast, Cliff, River, Ridge };
    Kind kind = Kind::Other;
    // A readable name for tooltips, such as "Base-68". Empty when the cell is not recognised.
    std::string label;
    // The side with water on a Coast cell, or high ground on a Cliff cell.
    int waterSide = 0;
    RiverPiece piece;
};

inline RiverCell classifyRiverLabel(std::string_view label) {
    RiverCell cell;
    cell.label = std::string(label);
    if (riverLandLabel(label)) {
        cell.kind = RiverCell::Kind::Land;
        return cell;
    }
    const int water = coastWaterSide(label);
    if (water != 0) {
        cell.kind = RiverCell::Kind::Coast;
        cell.waterSide = water;
        cell.label = riverKeyOf(label);
        return cell;
    }
    const int high = cliffHighSide(label);
    if (high != 0) {
        cell.kind = RiverCell::Kind::Cliff;
        cell.waterSide = high;
        cell.label = riverKeyOf(label);
        return cell;
    }
    const RiverPiece piece = parseLinePiece(label);
    if (piece.valid) {
        cell.kind = piece.family == LineFamily::Ridge ? RiverCell::Kind::Ridge : RiverCell::Kind::River;
        cell.piece = piece;
        cell.label = piece.key;
        return cell;
    }
    // Base names end in their height, which is not a variation number.
    cell.label = riverLowerText(label).starts_with("base") ? std::string(label) : riverKeyOf(label);
    return cell;
}

struct RiverEdit {
    int x = 0;
    int y = 0;
    std::string key;
};

// Which cells change, or why nothing does. An empty reason means the brush works here.
struct RiverPlan {
    std::vector<RiverEdit> edits;
    std::string reason;

    bool allowed() const { return reason.empty() && !edits.empty(); }
};

using RiverLookup = std::function<RiverCell(int, int)>;

namespace river_detail {

inline std::string cannotPlace(const char* what, const RiverCell& cell, const char* where) {
    std::string text = std::string(what) + " cannot go on ";
    text += cell.label.empty() ? "this tile" : cell.label;
    text += std::string("\n") + where;
    return text;
}

inline bool permits(const RiverPiece& piece, int side) {
    return piece.flexible || (piece.sides & side) != 0;
}

}  // namespace river_detail

inline constexpr std::array<std::pair<int, std::pair<int, int>>, 4> kRiverNeighbours = {{
    {kRiverTop, {0, -1}},
    {kRiverRight, {1, 0}},
    {kRiverBottom, {0, 1}},
    {kRiverLeft, {-1, 0}},
}};

namespace river_detail {

inline RiverCell::Kind lineKind(LineFamily family) {
    return family == LineFamily::Ridge ? RiverCell::Kind::Ridge : RiverCell::Kind::River;
}

// Where a mouth can start: a coast for rivers, a cliff for ridges.
inline RiverCell::Kind mouthKind(LineFamily family) {
    return family == LineFamily::Ridge ? RiverCell::Kind::Cliff : RiverCell::Kind::Coast;
}

inline std::string noPlace(LineFamily family, const RiverCell& here) {
    return family == LineFamily::Ridge
        ? cannotPlace("Ridges", here, "Ridges only go on Base-85 and cliff edges")
        : cannotPlace("Rivers", here, "Rivers only go on Base-85, Base-80 and coast edges");
}

}  // namespace river_detail

// Rivers go on Base-85 or Base-80 and on the straight coasts. Ridges go on Base-85 and on the
// straight cliffs. A cell that already holds one is rebuilt, and so are the plain
// cells of the same kind that now join it.
inline RiverPlan planLine(LineFamily family, int x, int y, const RiverLookup& lookup, const std::function<bool(std::string_view)>& available = {}) {
    const bool ridge = family == LineFamily::Ridge;
    const RiverCell::Kind lineKind = river_detail::lineKind(family);
    RiverPlan plan;
    const RiverCell here = lookup(x, y);
    RiverPiece centre;
    if (here.kind == RiverCell::Kind::Land) {
        centre = parseLinePiece(lineKeyForSides(family, 0));
    } else if (here.kind == river_detail::mouthKind(family)) {
        centre = parseLinePiece(lineMouthKey(family, here.waterSide));
    } else if (here.kind == lineKind) {
        if (here.piece.bridge) {
            plan.reason = ridge ? "A ridge spacer is already here" : "A river bridge is already here";
            return plan;
        }
        centre = here.piece;
    }
    if (!centre.valid) {
        plan.reason = river_detail::noPlace(family, here);
        return plan;
    }

    std::map<std::pair<int, int>, RiverPiece> placed;
    const auto cellAt = [&](int cx, int cy) {
        const auto found = placed.find({cx, cy});
        if (found != placed.end()) {
            RiverCell cell;
            cell.kind = lineKind;
            cell.piece = found->second;
            cell.label = found->second.key;
            return cell;
        }
        return lookup(cx, cy);
    };
    // The sides of a cell where a neighbouring line meets it.
    const auto joinedSides = [&](int cx, int cy, const RiverPiece& self) {
        int sides = 0;
        for (const auto& [side, step] : kRiverNeighbours) {
            if (!river_detail::permits(self, side)) {
                continue;
            }
            const RiverCell next = cellAt(cx + step.first, cy + step.second);
            if (next.kind == lineKind && river_detail::permits(next.piece, oppositeRiverSide(side))) {
                sides |= side;
            }
        }
        return sides;
    };

    RiverPiece wanted = centre;
    if (centre.flexible) {
        const int sides = joinedSides(x, y, centre);
        // A lone cell that is already drawn keeps its shape.
        wanted = (sides == 0 && here.kind == lineKind) ? centre : parseLinePiece(lineKeyForSides(family, sides));
    }
    // A tileset without a pond starts a lone river as a straight run.
    if (available && !available(wanted.key) && sameRiverKey(wanted.key, "River-Base") && available("River-Horizontal")) {
        wanted = parseLinePiece("River-Horizontal");
    }
    if (here.kind != lineKind || !sameRiverKey(here.piece.key, wanted.key)) {
        placed[{x, y}] = wanted;
        plan.edits.push_back(RiverEdit{x, y, wanted.key});
    }

    // Plain neighbours that now meet this cell take the shape that matches.
    for (const auto& [side, step] : kRiverNeighbours) {
        if (!river_detail::permits(wanted, side)) {
            continue;
        }
        const int nx = x + step.first;
        const int ny = y + step.second;
        const RiverCell next = cellAt(nx, ny);
        if (next.kind != lineKind || !next.piece.flexible || !river_detail::permits(next.piece, oppositeRiverSide(side))) {
            continue;
        }
        const int sides = joinedSides(nx, ny, next.piece);
        if (sides == 0) {
            continue;
        }
        const RiverPiece rebuilt = parseLinePiece(lineKeyForSides(family, sides));
        if (!sameRiverKey(next.piece.key, rebuilt.key)) {
            placed[{nx, ny}] = rebuilt;
            plan.edits.push_back(RiverEdit{nx, ny, rebuilt.key});
        }
    }

    if (plan.edits.empty()) {
        plan.reason = ridge ? "A ridge is already here" : "A river is already here";
    }
    return plan;
}

// A bridge or a spacer only goes on a plain cell of its own kind. A bridge takes the
// same shape (River-Horizontal becomes RiverB-Horizontal), and a spacer fills the
// straight piece it sits on.
inline RiverPlan planLineOverlay(LineFamily family, int x, int y, const RiverLookup& lookup, const std::function<bool(std::string_view)>& available = {}) {
    const bool ridge = family == LineFamily::Ridge;
    RiverPlan plan;
    const RiverCell here = lookup(x, y);
    if (here.kind != river_detail::lineKind(family)) {
        plan.reason = ridge ? river_detail::cannotPlace("Ridge spacers", here, "Spacers only go on ridge tiles")
                            : river_detail::cannotPlace("River bridges", here, "Bridges only go on river tiles");
        return plan;
    }
    if (here.piece.bridge) {
        plan.reason = ridge ? "A spacer is already here" : "A bridge is already here";
        return plan;
    }
    const std::string key = lineOverlayKey(here.piece);
    if (key.empty() || (available && !available(key))) {
        plan.reason = ridge ? river_detail::cannotPlace("Ridge spacers", here, "Spacers go on straight ridges")
                            : river_detail::cannotPlace("River bridges", here, "Bridges go on straight rivers and bends");
        return plan;
    }
    plan.edits.push_back(RiverEdit{x, y, key});
    return plan;
}

inline RiverPlan planRiver(int x, int y, const RiverLookup& lookup, const std::function<bool(std::string_view)>& available = {}) {
    return planLine(LineFamily::River, x, y, lookup, available);
}
inline RiverPlan planRiverBridge(int x, int y, const RiverLookup& lookup, const std::function<bool(std::string_view)>& available = {}) {
    return planLineOverlay(LineFamily::River, x, y, lookup, available);
}
inline RiverPlan planRidge(int x, int y, const RiverLookup& lookup) { return planLine(LineFamily::Ridge, x, y, lookup); }
inline RiverPlan planRidgeSpacer(int x, int y, const RiverLookup& lookup) { return planLineOverlay(LineFamily::Ridge, x, y, lookup); }

// ---------------------------------------------------------------------------
// Reading the map. A cell is recognised by its pictures: the same 16 x 16 tiles
// that a section holds produce the same signature, wherever the section was stamped.

inline std::uint64_t riverTileHash(const std::uint8_t* pixels) {
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    for (int offset = 0; offset < 1024; offset += 8) {
        std::uint64_t word = 0;
        std::memcpy(&word, pixels + offset, sizeof(word));
        hash = (hash ^ word) * 0x100000001b3ULL;
        hash ^= hash >> 29;
    }
    return hash;
}

inline constexpr int kRiverCellTiles = 16;

// Signature of the 16 x 16 tiles whose top-left tile is (tileX, tileY) in the stamp.
// Zero means the cell is not entirely inside the stamp.
inline std::uint64_t riverStampSignature(const SectionStamp& stamp, int tileX, int tileY) {
    if (stamp.heightsOnly || stamp.tileImages.empty() || tileX < 0 || tileY < 0
        || tileX + kRiverCellTiles > stamp.tileWidth || tileY + kRiverCellTiles > stamp.tileHeight) {
        return 0;
    }
    std::uint64_t signature = 0x9e3779b97f4a7c15ULL;
    for (int y = 0; y < kRiverCellTiles; ++y) {
        for (int x = 0; x < kRiverCellTiles; ++x) {
            const std::size_t at = static_cast<std::size_t>(tileY + y) * static_cast<std::size_t>(stamp.tileWidth) + static_cast<std::size_t>(tileX + x);
            if (at >= stamp.indices.size()) {
                return 0;
            }
            const std::size_t picture = stamp.indices[at];
            if ((picture + 1) * 1024 > stamp.tileImages.size()) {
                return 0;
            }
            signature = (signature ^ riverTileHash(stamp.tileImages.data() + picture * 1024)) * 0x100000001b3ULL;
            signature ^= signature >> 31;
        }
    }
    return signature == 0 ? 1 : signature;
}

inline std::uint64_t riverMapSignature(const LoadedMap& map, int cellX, int cellY) {
    const int tileX = cellX * kRiverCellTiles;
    const int tileY = cellY * kRiverCellTiles;
    if (cellX < 0 || cellY < 0 || tileX + kRiverCellTiles > map.tileWidth || tileY + kRiverCellTiles > map.tileHeight) {
        return 0;
    }
    std::uint64_t signature = 0x9e3779b97f4a7c15ULL;
    for (int y = 0; y < kRiverCellTiles; ++y) {
        for (int x = 0; x < kRiverCellTiles; ++x) {
            const std::size_t at = static_cast<std::size_t>(tileY + y) * static_cast<std::size_t>(map.tileWidth) + static_cast<std::size_t>(tileX + x);
            if (at >= map.indices.size()) {
                return 0;
            }
            const std::size_t picture = map.indices[at];
            if ((picture + 1) * 1024 > map.tileImages.size()) {
                return 0;
            }
            signature = (signature ^ riverTileHash(map.tileImages.data() + picture * 1024)) * 0x100000001b3ULL;
            signature ^= signature >> 31;
        }
    }
    return signature == 0 ? 1 : signature;
}

// The average height of the 32 x 32 height samples that sit under one cell. Base-85
// and Base-219 share their pictures, so the height is what tells them apart.
inline int riverStampHeight(const SectionStamp& stamp, int tileX, int tileY) {
    const std::size_t stride = static_cast<std::size_t>(stamp.tileWidth) * 2;
    long total = 0;
    long count = 0;
    for (int y = 0; y < kRiverCellTiles * 2; ++y) {
        for (int x = 0; x < kRiverCellTiles * 2; ++x) {
            const std::size_t at = static_cast<std::size_t>(tileY * 2 + y) * stride + static_cast<std::size_t>(tileX * 2 + x);
            if (at < stamp.heights.size()) {
                total += stamp.heights[at];
                ++count;
            }
        }
    }
    return count == 0 ? 0 : static_cast<int>(total / count);
}

inline int riverMapHeight(const LoadedMap& map, int cellX, int cellY) {
    const std::size_t stride = static_cast<std::size_t>(map.heightWidth());
    long total = 0;
    long count = 0;
    for (int y = 0; y < kRiverCellTiles * 2; ++y) {
        for (int x = 0; x < kRiverCellTiles * 2; ++x) {
            const std::size_t at = static_cast<std::size_t>(cellY * kRiverCellTiles * 2 + y) * stride + static_cast<std::size_t>(cellX * kRiverCellTiles * 2 + x);
            if (at < map.heights.size()) {
                total += map.heights[at];
                ++count;
            }
        }
    }
    return count == 0 ? 0 : static_cast<int>(total / count);
}

// Every 16 x 16 cell of the sections that matter to rivers and ridges, by signature.
// Different sections can share a picture, such as Base-85 and Base-219. The height of
// the cell then picks the closest one.
class RiverCatalog {
public:
    void clear() { entries_.clear(); }
    bool empty() const { return entries_.empty(); }

    // A section larger than one cell, such as a 64 x 64 brush stroke, adds every cell inside it.
    void add(const SectionStamp& stamp, const std::string& label) {
        for (int y = 0; y + kRiverCellTiles <= stamp.tileHeight; y += kRiverCellTiles) {
            for (int x = 0; x + kRiverCellTiles <= stamp.tileWidth; x += kRiverCellTiles) {
                const std::uint64_t signature = riverStampSignature(stamp, x, y);
                if (signature == 0) {
                    continue;
                }
                std::vector<Entry>& candidates = entries_[signature];
                const bool known = std::any_of(candidates.begin(), candidates.end(), [&](const Entry& entry) { return entry.label == label; });
                if (!known) {
                    candidates.push_back(Entry{label, riverStampHeight(stamp, x, y)});
                }
            }
        }
    }

    RiverCell classify(const LoadedMap& map, int cellX, int cellY) const {
        const std::uint64_t signature = riverMapSignature(map, cellX, cellY);
        if (signature == 0) {
            return RiverCell{};
        }
        const auto found = entries_.find(signature);
        if (found == entries_.end() || found->second.empty()) {
            return RiverCell{};
        }
        const std::vector<Entry>& candidates = found->second;
        const Entry* best = &candidates.front();
        if (candidates.size() > 1) {
            const int height = riverMapHeight(map, cellX, cellY);
            for (const Entry& entry : candidates) {
                if (std::abs(entry.height - height) < std::abs(best->height - height)) {
                    best = &entry;
                }
            }
        }
        return classifyRiverLabel(best->label);
    }

private:
    struct Entry {
        std::string label;
        int height = 0;
    };
    std::unordered_map<std::uint64_t, std::vector<Entry>> entries_;
};

}  // namespace galaxy
