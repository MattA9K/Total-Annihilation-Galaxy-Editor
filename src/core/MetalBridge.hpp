#pragma once

// Metal's Bridge 139 brush. A bridge deck stands at Base-139 over Metal2's dark Base-0 water, one 16 x 16 cell
// at a time, and runs only in straight lines: it never turns a corner, but it can split into a 3-way or 4-way
// junction. Each piece is named by the sides its deck leaves through, in the order T, R, B, L:
//
//   Bridge139-Mouth-B    a straight high coast (Cliff-Ramp-High-B, land along the bottom) with a bridge leaving
//                        through the top. Mouths start a bridge, and only join through that one side.
//   Bridge139-T          a deck that ends here, coming from the top
//   Bridge139-T-B        a straight deck, and Bridge139-R-L
//   Bridge139-T-R-L      a 3-way junction, and the other three
//   Bridge139-T-R-B-L    a 4-way crossing
//   Bridge139-Lone       a lone piece on its own in the water
//
// Clicking a straight high coast starts a bridge there and lays its first deck in the water beyond. Clicking the
// dark water lays a deck that joins every bridge beside it, or a lone piece when none is. A neighbour that would
// have to turn a corner to join is left alone.
//
// Metal's walls follow the same rules with their own prefixes, and may turn corners:
//   Wall85-...           on Base-85, from Metal_v14 Walls_86. Wall85-Mouth-B starts a wall on a straight Base-219
//                        cliff (Cliff-High-B), and Wall85-Mouth-BR two walls on a single high corner (Cliff-High-BR).
//   Wall139-...          on Base-139, from Metal_v14 Walls_139. It has no cliff starts yet.

#include "core/RiverBrush.hpp"

#include <map>
#include <vector>
#include <string>
#include <utility>

namespace galaxy {

// The label the Tile Brush reads for Metal2's dark water.
inline constexpr std::string_view kMetalBridgeWater = "Base-0-2";

// One of Metal's line brushes: its piece prefix, the ground it is laid on, whether it may turn a corner, and the
// cliffs it starts from.
struct MetalLineRules {
    std::string prefix;          // "Bridge139", "Wall85" or "Wall139"
    std::string ground;          // the label of the cell it is laid on
    bool bends = false;
    std::string cliffPrefix;     // "cliff-ramp-high-" or "cliff-high-", lowered; empty for none
    bool cornerMouths = false;   // a single high corner takes a mouth with two lines leaving it
    const char* what = "Bridges";
    const char* where = "";
};

inline MetalLineRules metalBridgeRules() {
    return MetalLineRules{"Bridge139", std::string(kMetalBridgeWater), false, "cliff-ramp-high-", false, "Bridges",
        "Bridges start on a straight Base-139 coast, then go on the dark water beside them\nA lone bridge goes on the dark water"};
}
inline MetalLineRules metalWall85Rules() {
    return MetalLineRules{"Wall85", "Base-85", true, "cliff-high-", true, "Walls",
        "Walls go on Metal's Base-85 and start from its Base-219 cliffs"};
}
inline MetalLineRules metalWall139Rules() {
    return MetalLineRules{"Wall139", "Base-139", true, "", false, "Walls", "Walls go on Metal's Base-139"};
}

struct MetalBridgePiece {
    bool valid = false;
    bool mouth = false;
    // The sides the line leaves through. A mouth leaves through the sides away from its high ground.
    int sides = 0;
};

inline std::string metalBridgeLetters(int sides) {
    std::string text;
    for (const int side : {kRiverTop, kRiverRight, kRiverBottom, kRiverLeft}) {
        if ((sides & side) != 0) {
            text += text.empty() ? "" : "-";
            text += riverLetterOfSide(side);
        }
    }
    return text;
}

// A line that would turn a corner: exactly two sides that are not opposite each other.
inline bool metalBridgeBends(int sides) {
    return sides == (kRiverTop | kRiverRight) || sides == (kRiverRight | kRiverBottom) || sides == (kRiverBottom | kRiverLeft)
        || sides == (kRiverLeft | kRiverTop);
}

inline std::string metalLineKey(const MetalLineRules& rules, int sides) {
    if (sides == 0) {
        return rules.prefix + "-Lone";
    }
    if (!rules.bends && metalBridgeBends(sides)) {
        return {};
    }
    return rules.prefix + "-" + metalBridgeLetters(sides);
}
inline std::string metalBridgeKey(int sides) { return metalLineKey(metalBridgeRules(), sides); }

// The mouth on a cliff whose high ground is on these sides: Bridge139-Mouth-B, Wall85-Mouth-BR.
inline std::string metalLineMouthKey(const MetalLineRules& rules, int highSides) {
    std::string letters;
    for (const int side : {kRiverTop, kRiverBottom, kRiverLeft, kRiverRight}) {
        if ((highSides & side) != 0) {
            letters += riverLetterOfSide(side);
        }
    }
    return rules.prefix + "-Mouth-" + letters;
}
inline std::string metalBridgeMouthKey(int landSide) { return metalLineMouthKey(metalBridgeRules(), landSide); }

// The high sides of "TR", "B" and the rest: T and B first, then L and R.
inline int metalHighSides(std::string_view letters) {
    if (letters.size() == 1) {
        return riverSideOfLetter(letters);
    }
    if (letters.size() == 2) {
        const int first = riverSideOfLetter(letters.substr(0, 1));
        const int second = riverSideOfLetter(letters.substr(1, 1));
        return (first == kRiverTop || first == kRiverBottom) && (second == kRiverLeft || second == kRiverRight) ? first | second : 0;
    }
    return 0;
}

inline int metalAwayFrom(int highSides) {
    int sides = 0;
    for (const int side : {kRiverTop, kRiverRight, kRiverBottom, kRiverLeft}) {
        if ((highSides & side) != 0) {
            sides |= oppositeRiverSide(side);
        }
    }
    return sides;
}

inline MetalBridgePiece parseMetalLinePiece(const MetalLineRules& rules, std::string_view label) {
    MetalBridgePiece piece;
    const std::string key = riverLowerText(riverKeyOf(label));
    const std::string prefix = riverLowerText(rules.prefix) + "-";
    if (!key.starts_with(prefix)) {
        return piece;
    }
    const std::string_view rest = std::string_view(key).substr(prefix.size());
    if (rest == "lone") {
        piece.valid = true;
        return piece;
    }
    if (rest.starts_with("mouth-")) {
        const int high = metalHighSides(rest.substr(6));
        if (high == 0) {
            return piece;
        }
        piece.valid = true;
        piece.mouth = true;
        piece.sides = metalAwayFrom(high);
        return piece;
    }
    int sides = 0;
    std::size_t start = 0;
    while (start <= rest.size()) {
        const std::size_t end = std::min(rest.find('-', start), rest.size());
        const int side = riverSideOfLetter(rest.substr(start, end - start));
        if (side == 0 || (sides & side) != 0) {
            return piece;
        }
        sides |= side;
        start = end + 1;
    }
    if (!rules.bends && metalBridgeBends(sides)) {
        return piece;
    }
    piece.valid = true;
    piece.sides = sides;
    return piece;
}
inline MetalBridgePiece parseMetalBridgePiece(std::string_view label) { return parseMetalLinePiece(metalBridgeRules(), label); }

// The high sides of a cliff this line can start from: one side for a straight cliff, two for a single high corner
// when the rules allow corner mouths. Anything else gives 0.
inline int metalLineCliffHigh(const MetalLineRules& rules, std::string_view label) {
    if (rules.cliffPrefix.empty()) {
        return 0;
    }
    const std::string key = riverLowerText(riverKeyOf(label));
    if (!key.starts_with(rules.cliffPrefix)) {
        return 0;
    }
    const std::string_view letters = std::string_view(key).substr(rules.cliffPrefix.size());
    const int high = metalHighSides(letters);
    return letters.size() == 2 && !rules.cornerMouths ? 0 : high;
}

inline bool metalBridgeWater(std::string_view label) {
    return sameRiverKey(label, kMetalBridgeWater);
}

namespace metal_line_detail {
inline std::pair<int, int> stepOf(int side) {
    return side == kRiverTop ? std::pair<int, int>{0, -1} : side == kRiverRight ? std::pair<int, int>{1, 0}
        : side == kRiverBottom ? std::pair<int, int>{0, 1} : std::pair<int, int>{-1, 0};
}
}  // namespace metal_line_detail

// Clicking a cliff the line starts from lays its mouth and the first piece in each cell beyond. Clicking the ground
// lays a piece that joins every line beside it, or a lone piece when none is. A neighbour that cannot join without
// turning a corner the rules forbid is left alone.
inline RiverPlan planMetalLine(const MetalLineRules& rules, int x, int y, const RiverLookup& lookup) {
    using metal_line_detail::stepOf;
    RiverPlan plan;
    std::map<std::pair<int, int>, MetalBridgePiece> placed;
    const auto pieceAt = [&](int cx, int cy) {
        const auto found = placed.find({cx, cy});
        return found != placed.end() ? found->second : parseMetalLinePiece(rules, lookup(cx, cy).label);
    };
    const auto place = [&](int cx, int cy, const std::string& key) {
        MetalBridgePiece piece = parseMetalLinePiece(rules, key);
        placed[{cx, cy}] = piece;
        plan.edits.push_back(RiverEdit{cx, cy, key});
    };
    const std::string lowerWhat = riverLowerText(rules.what);
    const std::string one = lowerWhat.substr(0, lowerWhat.size() - 1);

    // Lays a piece at (cx, cy) leaving through `must` and joining every line beside it that can meet it, and grows
    // those neighbours to meet it. False when the piece would have to turn a corner the rules forbid.
    const auto joinAt = [&](int cx, int cy, int must) {
        int sides = must;
        std::vector<std::pair<int, std::string>> grownBy;
        for (const int side : {kRiverTop, kRiverRight, kRiverBottom, kRiverLeft}) {
            const auto step = stepOf(side);
            const MetalBridgePiece next = pieceAt(cx + step.first, cy + step.second);
            if (!next.valid) {
                continue;
            }
            const int back = oppositeRiverSide(side);
            if ((next.sides & back) != 0) {
                sides |= side;
                continue;
            }
            if (next.mouth) {
                continue;
            }
            const std::string grown = metalLineKey(rules, next.sides | back);
            if (!grown.empty() && !metalLineKey(rules, sides | side).empty()) {
                sides |= side;
                grownBy.push_back({side, grown});
            }
        }
        const std::string key = metalLineKey(rules, sides);
        if (key.empty()) {
            return false;
        }
        if (!sameRiverKey(lookup(cx, cy).label, key)) {
            place(cx, cy, key);
        }
        for (const auto& [side, grown] : grownBy) {
            const auto step = stepOf(side);
            place(cx + step.first, cy + step.second, grown);
        }
        return true;
    };
    const auto turnRefusal = [&]() {
        return std::string(rules.what) + " only run in straight lines\nThe " + lowerWhat + " here would meet at a corner";
    };

    const RiverCell here = lookup(x, y);
    const MetalBridgePiece existing = parseMetalLinePiece(rules, here.label);
    if (existing.valid) {
        // Clicking a line that is already here joins it to the lines beside it.
        if (!existing.mouth && !joinAt(x, y, existing.sides)) {
            plan.reason = turnRefusal();
        }
        if (plan.edits.empty() && plan.reason.empty()) {
            plan.reason = "A " + one + " is already here";
        }
        return plan;
    }

    const int high = metalLineCliffHigh(rules, here.label);
    if (high != 0) {
        // The line leaves the cliff through every side away from its high ground, into the ground or a line there.
        const int out = metalAwayFrom(high);
        for (const int side : {kRiverTop, kRiverRight, kRiverBottom, kRiverLeft}) {
            if ((out & side) == 0) {
                continue;
            }
            const auto step = stepOf(side);
            const RiverCell cell = lookup(x + step.first, y + step.second);
            const MetalBridgePiece next = parseMetalLinePiece(rules, cell.label);
            if (next.valid && next.mouth) {
                plan.reason = "This " + one + " would run straight into another cliff's " + one;
                return plan;
            }
            if (!next.valid && !sameRiverKey(cell.label, rules.ground)) {
                plan.reason = "A " + one + " from here needs " + rules.ground + " right beyond the cliff";
                return plan;
            }
        }
        MetalBridgePiece mouth;
        mouth.valid = true;
        mouth.mouth = true;
        mouth.sides = out;
        placed[{x, y}] = mouth;
        plan.edits.push_back(RiverEdit{x, y, metalLineMouthKey(rules, high)});
        for (const int side : {kRiverTop, kRiverRight, kRiverBottom, kRiverLeft}) {
            if ((out & side) == 0) {
                continue;
            }
            const auto step = stepOf(side);
            const MetalBridgePiece next = pieceAt(x + step.first, y + step.second);
            if (!joinAt(x + step.first, y + step.second, oppositeRiverSide(side) | (next.valid ? next.sides : 0))) {
                plan.edits.clear();
                plan.reason = std::string(rules.what) + " only run in straight lines\nThe " + one + " beyond this cliff would have to turn a corner";
                return plan;
            }
        }
        return plan;
    }

    if (!sameRiverKey(here.label, rules.ground)) {
        plan.reason = river_detail::cannotPlace(rules.what, here, rules.where);
        return plan;
    }
    if (!joinAt(x, y, 0)) {
        plan.reason = turnRefusal();
    }
    return plan;
}

inline RiverPlan planMetalBridge(int x, int y, const RiverLookup& lookup) { return planMetalLine(metalBridgeRules(), x, y, lookup); }

}  // namespace galaxy
