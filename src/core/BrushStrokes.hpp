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
// CoastalCliff pieces. Base-86 and Base-41 join the same way, with Base-86 on the
// land side of a coast and Base-41 on the lava side. Base-86 and Base-190 join
// with Cliff pieces. Base-85 and Base-255 join with Cliff pieces. Mars uses that
// pair: its cliff flat is height 255. Base-85 and Base-170 join with Cliff pieces.
// Ice uses that pair: water is Base-0, land is Base-85, and the high ground is
// Base-170. Crystal and Acid use the coast alone: water is Base-0 and land is
// Base-85. Lush water is Base-27 and land is Base-77, joined by Coast pieces.
// Lush's Base-183 and Base-255 both join Base-77 with Cliff pieces. Two tops on
// the same land use Cliff-183 and Cliff-255, so each cliff keeps its own
// pictures. Painting either cliff on the ocean keeps Base-77 between the water
// and the cliff.
// Desert water is Base-0 and land is Base-80, joined by Coast pieces.
// Desert's Base-155 joins Base-80 with Cliff pieces, so a click on water keeps
// Base-80 between the water and the cliff. Desert's Base-230 joins Base-155
// with pieces named Cliff-230, so the Base-80 cliff keeps the plain Cliff name.
// Desert's Base-68 joins Base-80 with ShallowCoast pieces. Base-0 and Base-68
// still join with ShallowDeep pieces, the same heights GreenWorld uses. The
// direct Base-0 coast stays, so painting Base-80 on water does not insert a
// Base-68 ring. A cell that holds Base-0, Base-68 and Base-80 uses a CoastDSL
// piece, which is how one Base-80 island can have a deep coast on one side and
// a shallow coast on the other.
// Archipelago joins Base-0 to Base-78 with those same Coast pieces. When Base-85
// is also a paint, water keeps its coast with Base-85 and Base-78 keeps its blend
// with Base-85. Archipelago's Base-68 joins Base-0 with ShallowDeep pieces and
// Base-78 with ShallowCoast pieces. The direct Base-0 coast stays, so painting
// Base-78 on water does not insert a Base-68 ring. A cell that holds Base-0,
// Base-68 and Base-78 uses a CoastDSL piece, which is how one Base-78 island can
// have a deep coast on one side and a shallow coast on the other. Archipelago's
// Base-194 joins Base-78 with Cliff pieces, so a click on water keeps Base-78
// between the water and the ravine. Base-68 is also GreenWorld's shallow water,
// and that shallow coast stays inside one world, so Archipelago's Base-68 does
// not join GreenWorld's Base-85.
// Metal uses the GreenWorld water set: Base-0, Base-68, and Base-85.
// Base-85 and Base-139 join with Cliff pieces. Metal's raised ground is Base-139.
// Base-85 also joins Base-219 with Cliff pieces. When one low ground has two cliff
// tops, Base-219 keeps the plain Cliff-High name and the other top is
// Cliff-<height>-High, so Metal's ramps stay apart from its Base-219 cliffs.
// Metal's Base-255 joins Base-139 only. A brush that already has Base-139 does
// not also join Base-85 to Base-255, which is how Mars reaches its cliff flat.
// Lava
// Base-86 joins either Mars or GreenWorld Base-85 with blend pieces. Archipelago
// Base-78 joins GreenWorld Base-85 the same way, with pieces named for the
// Archipelago side. Archipelago Base-78 also joins Lush Base-77, with pieces
// named Blend-ArchiLush so the grass blend keeps its own files. Archipelago
// Base-78 joins Acid Base-85 with pieces named Blend-ArchiAcid, so that blend
// stays apart from the grass blend. Lush Base-77 joins Lava Base-86 with pieces
// named Blend-Lush, and the mask marks the grass. Ice Base-85 joins Mars Base-85
// with pieces named Blend-IceMars, and the mask marks the ice. Ice Base-85 also
// joins GreenWorld Base-85 with pieces named Blend-IceGreen, and the mask marks
// the ice. Lush Base-77 joins Ice Base-85 with pieces named Blend-LushIce, and
// the mask marks the ice. GreenWorld Base-85 joins Mars Base-85 with pieces named
// Blend-GreenMars, and the mask marks Mars. Crystal Base-85 joins Lava Base-86 with
// pieces named Blend-CrystalLava, and the mask marks Crystal. GreenWorld Base-85 joins
// Metal Base-85 with pieces named Blend-GreenMetal, and the mask marks Metal. Ice Base-85
// joins Lava Base-86 with pieces named Blend-IceLava, and Archipelago Base-78 with pieces named
// Blend-IceArchi; both marks are the ice. GreenWorld Base-85 joins Lush Base-77 with pieces named
// Blend-GreenLush, and the mask marks the GreenWorld grass. Lush Base-77 joins Mars Base-85 with
// pieces named Blend-LushMars, and the mask marks Mars. Archipelago Base-78 joins Crystal Base-85
// with pieces named Blend-ArchiCrystal, and the mask marks the sand. Acid Base-85 joins Crystal
// Base-85 with pieces named Blend-AcidCrystal, and the mask marks Acid. Acid Base-85 also joins
// Desert Base-80 with pieces named Blend-AcidDesert, and the mask marks Acid. Crystal Base-85
// joins Desert Base-80 with pieces named Blend-CrystalDesert, and the mask marks Crystal. Archipelago Base-78
// joins Desert Base-80 with pieces named Blend-ArchiDesert, and the mask marks the sand. GreenWorld Base-85
// joins Desert Base-80 with pieces named Blend-DesertGreen, and the mask marks Desert. Ice Base-85
// joins Desert Base-80 with pieces named Blend-IceDesert, and the mask marks the ice. Ice keeps its own
// coast, so Mars beside the water does not replace that coast. Archipelago's
// own coast from Base-0 stays, so grass beside the water does not replace that
// coast. A cell
// holds one paint or two joined paints. Three paints
// may share a cell when a section draws that layout of corners: Base-0, Base-68
// and Base-85 use a CoastDSL piece, Base-0, Base-68 and Base-78 use one too, and
// Base-219 with Base-85 and either Base-0 or Base-68 uses a CoastalCliff piece.
// GreenWorld, Acid and Lava share a cell when Acid fills one side and GreenWorld
// and Lava split the other. Those pieces are Blend-Tri-Acid. Acid, Crystal and Lava
// share a cell the same way as Crystal and Archipelago, and every name has Acid in it.
// Acid, Desert and Lava share a cell the same way, and every name has Desert in it.
// Lava, Crystal and Desert share a cell the same way, and every name has Crystal and Desert in it.
// Lava, GreenWorld and Desert share a cell the same way, and every name has Grass and Desert in it.
// Lava, Archipelago and Desert share a cell the same way, and every name has Archi and Desert in it.
// Lava, Ice and Desert share a cell the same way, and every name has Ice and Desert in it.
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
    WorldBlend,           // two worlds meeting, such as Base-85 beside Lava Base-86
};

// Which pair of paints a family of pieces joins. Paints are named by height, so
// the rule matches Base-<lowerHeight> with Base-<upperHeight>.
struct BrushTransitionRule {
    int lowerHeight = 0;
    int upperHeight = 0;
    TransitionStyle style = TransitionStyle::None;
};

inline const std::array<BrushTransitionRule, 32>& brushTransitionRules() {
    static const std::array<BrushTransitionRule, 32> rules = {{
        {0, 85, TransitionStyle::Coast},
        {0, 80, TransitionStyle::Coast},
        {27, 77, TransitionStyle::Coast},
        {77, 183, TransitionStyle::Cliff},
        {77, 255, TransitionStyle::Cliff},
        {68, 80, TransitionStyle::ShallowCoast},
        {41, 86, TransitionStyle::Coast},
        {0, 78, TransitionStyle::Coast},
        {0, 68, TransitionStyle::ShallowDeep},
        {68, 85, TransitionStyle::ShallowCoast},
        {68, 78, TransitionStyle::ShallowCoast},
        {78, 85, TransitionStyle::WorldBlend},
        {0, 219, TransitionStyle::CoastalCliffDeep},
        {68, 219, TransitionStyle::CoastalCliffShallow},
        {85, 219, TransitionStyle::Cliff},
        {85, 255, TransitionStyle::Cliff},
        {139, 255, TransitionStyle::Cliff},
        {85, 170, TransitionStyle::Cliff},
        {85, 139, TransitionStyle::Cliff},
        {86, 190, TransitionStyle::Cliff},
        {78, 194, TransitionStyle::Cliff},
        {80, 155, TransitionStyle::Cliff},
        {155, 230, TransitionStyle::Cliff},
        {85, 86, TransitionStyle::WorldBlend},
        {80, 86, TransitionStyle::WorldBlend},
        // The graph applies this only to Acid, Crystal, GreenWorld, or Ice beside Desert.
        {80, 85, TransitionStyle::WorldBlend},
        {78, 86, TransitionStyle::WorldBlend},
        // The graph applies this only to Archipelago beside Desert.
        {78, 80, TransitionStyle::WorldBlend},
        // Same height. The graph applies this to GreenWorld beside Acid, Ice beside Mars, Ice beside GreenWorld, and Acid beside Crystal.
        {85, 85, TransitionStyle::WorldBlend},
        // The graph applies this only to Lush beside Archipelago.
        {77, 78, TransitionStyle::WorldBlend},
        // The graph applies this only to Lush beside Lava.
        {77, 86, TransitionStyle::WorldBlend},
        // The graph applies this only to Lush beside Ice.
        {77, 85, TransitionStyle::WorldBlend},
    }};
    return rules;
}

// Coast-Water-T is the usual name. A tileset can use another word for the same
// shape, such as Coast-Lava-T, while Coast-Land stays Coast-Land.
inline std::string coastNamed(std::string_view name, std::string_view liquid) {
    constexpr std::string_view water = "Coast-Water-";
    if (liquid != "Water" && name.size() >= water.size() && name.substr(0, water.size()) == water) {
        return "Coast-" + std::string(liquid) + "-" + std::string(name.substr(water.size()));
    }
    return std::string(name);
}

// "Mars" or "Green". The blend pieces are named for the higher land, so a mask
// with grass on the right is Blend-Green-R.
inline std::string blendWorldName(std::string_view category) {
    std::string lower(category);
    for (char& character : lower) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    if (lower.find("mars") != std::string::npos) {
        return "Mars";
    }
    if (lower.find("green") != std::string::npos) {
        return "Green";
    }
    if (lower.find("lava") != std::string::npos) {
        return "Lava";
    }
    if (lower.find("archipelago") != std::string::npos) {
        return "Archi";
    }
    if (lower.find("desert") != std::string::npos) {
        return "Desert";
    }
    if (lower.find("acid") != std::string::npos) {
        return "Acid";
    }
    if (lower.find("lush") != std::string::npos) {
        return "Lush";
    }
    if (lower.find("ice") != std::string::npos) {
        return "Ice";
    }
    if (lower.find("crystal") != std::string::npos) {
        return "Crystal";
    }
    if (lower.find("metal") != std::string::npos) {
        return "Metal";
    }
    return {};
}

// True when two paints use one height, such as Mars, GreenWorld, and Acid all
// named Base-85. A later paint at another height does not erase an earlier match.
inline bool paintsShareHeight(const std::vector<int>& heights) {
    for (std::size_t i = 0; i < heights.size(); ++i) {
        for (std::size_t j = i + 1; j < heights.size(); ++j) {
            if (heights[i] == heights[j]) {
                return true;
            }
        }
    }
    return false;
}

// The section a mask needs for a family of pieces, without its variation number.
// Empty for flat cells and the two diagonal masks. blendWorld names the higher
// land for a world blend, and an empty name means Mars.
inline std::string transitionPieceName(TransitionStyle style, int mask, std::string_view coastLiquid = "Water", std::string_view blendWorld = "Mars") {
    switch (style) {
    case TransitionStyle::Cliff: return std::string(cliffPieceName(mask));
    case TransitionStyle::Coast: return coastNamed(coastPieceName(mask), coastLiquid);
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
    case TransitionStyle::WorldBlend: {
        // The mask marks the Mars or GreenWorld corners. Blend-Green-R has grass
        // along the right, and Blend-Mars-Low-TL has Mars everywhere except the top left.
        const std::string family = blendWorld.empty() ? std::string("Mars") : std::string(blendWorld);
        const std::string cliff(cliffPieceName(mask));
        if (cliff.rfind("Cliff-High-", 0) == 0) {
            return "Blend-" + family + "-" + cliff.substr(11);
        }
        if (cliff.rfind("Cliff-Low-", 0) == 0) {
            return "Blend-" + family + "-Low-" + cliff.substr(10);
        }
        return {};
    }
    default: return {};
    }
}

// The paints from lowest to highest and which of them are joined by pieces. A
// paint is called a level here, numbered by its place in that order.
class BrushGraph {
public:
    BrushGraph() = default;

    explicit BrushGraph(const std::vector<int>& paintHeights) : BrushGraph(paintHeights, {}) {}

    // worlds names each paint's land, such as "Green" or "Mars", so two paints at
    // the same height can still pick different blend pieces.
    BrushGraph(const std::vector<int>& paintHeights, const std::vector<std::string>& worlds)
        : heights_(paintHeights), worlds_(worlds.size() == paintHeights.size() ? worlds : std::vector<std::string>(paintHeights.size())) {
        const int count = static_cast<int>(heights_.size());
        const std::size_t cells = static_cast<std::size_t>(count) * static_cast<std::size_t>(count);
        styles_.assign(cells, TransitionStyle::None);
        distance_.assign(cells, kFar);
        hop_.assign(cells, 0);
        // Mars joins Base-85 straight to Base-255. Metal's Base-255 only has pieces
        // for Base-139, so that direct cliff is left out once Base-139 is present.
        // Archipelago joins Base-0 straight to Base-78. Another world's land,
        // such as Lava beside Mars or Archipelago beside GreenWorld, is reached
        // through Base-85 instead. Base-68 does not remove Archipelago's own
        // coast: shallow water is its own paint. GreenWorld's shallow coast
        // joins Base-68 to Base-85 inside one world, and Archipelago's Base-68
        // stays off GreenWorld's Base-85.
        const bool through139 = std::find(heights_.begin(), heights_.end(), 139) != heights_.end();
        const bool through85 = std::find(heights_.begin(), heights_.end(), 85) != heights_.end();
        for (int a = 0; a < count; ++a) {
            for (int b = 0; b < count; ++b) {
                for (const BrushTransitionRule& rule : brushTransitionRules()) {
                    if (through139 && rule.lowerHeight == 85 && rule.upperHeight == 255) {
                        continue;
                    }
                    if (heights_[static_cast<std::size_t>(a)] == rule.lowerHeight && heights_[static_cast<std::size_t>(b)] == rule.upperHeight) {
                        const bool named = !worlds_[static_cast<std::size_t>(a)].empty() && !worlds_[static_cast<std::size_t>(b)].empty();
                        const bool differentWorld = named && worlds_[static_cast<std::size_t>(a)] != worlds_[static_cast<std::size_t>(b)];
                        const bool foreignLand = through85 && rule.lowerHeight == 0 && rule.upperHeight == 78 && differentWorld;
                        const bool foreignShallow = rule.lowerHeight == 68 && rule.upperHeight == 85 && differentWorld;
                        // Base-85 beside Base-85 is GreenWorld beside Acid, Ice beside Mars, Ice beside GreenWorld,
                        // or GreenWorld beside Mars. Mars still reaches Acid through GreenWorld or Lava.
                        const bool greenAcid = rule.lowerHeight == 85 && rule.upperHeight == 85
                            && ((worlds_[static_cast<std::size_t>(a)] == "Green" && worlds_[static_cast<std::size_t>(b)] == "Acid")
                                || (worlds_[static_cast<std::size_t>(a)] == "Acid" && worlds_[static_cast<std::size_t>(b)] == "Green"));
                        const bool iceMars = rule.lowerHeight == 85 && rule.upperHeight == 85
                            && ((worlds_[static_cast<std::size_t>(a)] == "Ice" && worlds_[static_cast<std::size_t>(b)] == "Mars")
                                || (worlds_[static_cast<std::size_t>(a)] == "Mars" && worlds_[static_cast<std::size_t>(b)] == "Ice"));
                        const bool iceGreen = rule.lowerHeight == 85 && rule.upperHeight == 85
                            && ((worlds_[static_cast<std::size_t>(a)] == "Ice" && worlds_[static_cast<std::size_t>(b)] == "Green")
                                || (worlds_[static_cast<std::size_t>(a)] == "Green" && worlds_[static_cast<std::size_t>(b)] == "Ice"));
                        const bool greenMars = rule.lowerHeight == 85 && rule.upperHeight == 85
                            && ((worlds_[static_cast<std::size_t>(a)] == "Green" && worlds_[static_cast<std::size_t>(b)] == "Mars")
                                || (worlds_[static_cast<std::size_t>(a)] == "Mars" && worlds_[static_cast<std::size_t>(b)] == "Green"));
                        const bool greenMetal = rule.lowerHeight == 85 && rule.upperHeight == 85
                            && ((worlds_[static_cast<std::size_t>(a)] == "Green" && worlds_[static_cast<std::size_t>(b)] == "Metal")
                                || (worlds_[static_cast<std::size_t>(a)] == "Metal" && worlds_[static_cast<std::size_t>(b)] == "Green"));
                        const bool acidCrystal = rule.lowerHeight == 85 && rule.upperHeight == 85
                            && ((worlds_[static_cast<std::size_t>(a)] == "Acid" && worlds_[static_cast<std::size_t>(b)] == "Crystal")
                                || (worlds_[static_cast<std::size_t>(a)] == "Crystal" && worlds_[static_cast<std::size_t>(b)] == "Acid"));
                        const bool acidDesert = rule.lowerHeight == 80 && rule.upperHeight == 85
                            && ((worlds_[static_cast<std::size_t>(a)] == "Acid" && worlds_[static_cast<std::size_t>(b)] == "Desert")
                                || (worlds_[static_cast<std::size_t>(a)] == "Desert" && worlds_[static_cast<std::size_t>(b)] == "Acid"));
                        const bool crystalDesert = rule.lowerHeight == 80 && rule.upperHeight == 85
                            && ((worlds_[static_cast<std::size_t>(a)] == "Crystal" && worlds_[static_cast<std::size_t>(b)] == "Desert")
                                || (worlds_[static_cast<std::size_t>(a)] == "Desert" && worlds_[static_cast<std::size_t>(b)] == "Crystal"));
                        const bool archiDesert = rule.lowerHeight == 78 && rule.upperHeight == 80
                            && ((worlds_[static_cast<std::size_t>(a)] == "Archi" && worlds_[static_cast<std::size_t>(b)] == "Desert")
                                || (worlds_[static_cast<std::size_t>(a)] == "Desert" && worlds_[static_cast<std::size_t>(b)] == "Archi"));
                        const bool greenDesert = rule.lowerHeight == 80 && rule.upperHeight == 85
                            && ((worlds_[static_cast<std::size_t>(a)] == "Green" && worlds_[static_cast<std::size_t>(b)] == "Desert")
                                || (worlds_[static_cast<std::size_t>(a)] == "Desert" && worlds_[static_cast<std::size_t>(b)] == "Green"));
                        const bool iceDesert = rule.lowerHeight == 80 && rule.upperHeight == 85
                            && ((worlds_[static_cast<std::size_t>(a)] == "Ice" && worlds_[static_cast<std::size_t>(b)] == "Desert")
                                || (worlds_[static_cast<std::size_t>(a)] == "Desert" && worlds_[static_cast<std::size_t>(b)] == "Ice"));
                        // A land keeps its own coast. Another land's Base-85 beside the water, such as
                        // GreenWorld beside Mars water or Ice water, goes through that land's own ground.
                        const bool foreignIceCoast = rule.lowerHeight == 0 && rule.upperHeight == 85 && differentWorld;
                        // Desert's own coast from Base-0 stays, so Acid or Crystal beside Desert water
                        // does not take that coast, and Acid or Crystal water does not take Desert's Base-80 coast.
                        const bool foreignDesertCoast = rule.lowerHeight == 0 && rule.upperHeight == 80 && differentWorld;
                        // Cliffs stay inside one land: another land's Base-85 beside Lush Base-255 or
                        // GreenWorld Base-219 goes through that land's own ground.
                        const bool foreignIceCliff = differentWorld && rule.style == TransitionStyle::Cliff;
                        // Base-77 beside Base-78 is the Lush to Archipelago blend only.
                        const bool archiLush = rule.lowerHeight == 77 && rule.upperHeight == 78
                            && ((worlds_[static_cast<std::size_t>(a)] == "Lush" && worlds_[static_cast<std::size_t>(b)] == "Archi")
                                || (worlds_[static_cast<std::size_t>(a)] == "Archi" && worlds_[static_cast<std::size_t>(b)] == "Lush"));
                        // Base-77 beside Base-86 is the Lush to Lava blend only.
                        const bool lushLava = rule.lowerHeight == 77 && rule.upperHeight == 86
                            && ((worlds_[static_cast<std::size_t>(a)] == "Lush" && worlds_[static_cast<std::size_t>(b)] == "Lava")
                                || (worlds_[static_cast<std::size_t>(a)] == "Lava" && worlds_[static_cast<std::size_t>(b)] == "Lush"));
                        // Base-77 beside Base-85 is the Lush to Ice, GreenWorld, or Mars blend.
                        const bool lushIce = rule.lowerHeight == 77 && rule.upperHeight == 85
                            && (((worlds_[static_cast<std::size_t>(a)] == "Lush" && worlds_[static_cast<std::size_t>(b)] == "Ice")
                                || (worlds_[static_cast<std::size_t>(a)] == "Ice" && worlds_[static_cast<std::size_t>(b)] == "Lush"))
                                || ((worlds_[static_cast<std::size_t>(a)] == "Lush" && worlds_[static_cast<std::size_t>(b)] == "Green")
                                || (worlds_[static_cast<std::size_t>(a)] == "Green" && worlds_[static_cast<std::size_t>(b)] == "Lush"))
                                || ((worlds_[static_cast<std::size_t>(a)] == "Lush" && worlds_[static_cast<std::size_t>(b)] == "Mars")
                                || (worlds_[static_cast<std::size_t>(a)] == "Mars" && worlds_[static_cast<std::size_t>(b)] == "Lush")));
                        if (foreignLand || foreignShallow || foreignIceCoast || foreignDesertCoast || foreignIceCliff
                            || (rule.lowerHeight == rule.upperHeight && !greenAcid && !iceMars && !iceGreen && !greenMars && !greenMetal && !acidCrystal)
                            || (rule.lowerHeight == 77 && rule.upperHeight == 78 && !archiLush)
                            || (rule.lowerHeight == 77 && rule.upperHeight == 86 && !lushLava)
                            || (rule.lowerHeight == 77 && rule.upperHeight == 85 && !lushIce)
                            || (rule.lowerHeight == 80 && rule.upperHeight == 85 && !acidDesert && !crystalDesert && !greenDesert && !iceDesert)
                            || (rule.lowerHeight == 78 && rule.upperHeight == 80 && !archiDesert)) {
                            continue;
                        }
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
    std::string_view world(int level) const {
        if (level < 0 || static_cast<std::size_t>(level) >= worlds_.size()) {
            return {};
        }
        return worlds_[static_cast<std::size_t>(level)];
    }
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
    std::vector<std::string> worlds_;
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

// How many other paints join this one with cliff pieces.
inline int cliffPartnerCount(const BrushGraph& graph, int level) {
    int count = 0;
    for (int other = 0; other < graph.size(); ++other) {
        if (other != level && graph.style(level, other) == TransitionStyle::Cliff) {
            ++count;
        }
    }
    return count;
}

// The cliff section for this high-ground mask. A low ground with one cliff top
// uses Cliff-High-B. A low ground with several tops keeps that plain name for
// Base-219 and names the others Cliff-<height>-High-B, so two cliff sets in one
// tileset do not share a file.
inline std::string cliffTransitionName(const BrushGraph& graph, int lowLevel, int highLevel, int highMask) {
    const std::string piece(cliffPieceName(highMask));
    if (piece.empty() || piece.rfind("Cliff-", 0) != 0 || cliffPartnerCount(graph, lowLevel) <= 1 || graph.height(highLevel) == 219) {
        return piece;
    }
    return "Cliff-" + std::to_string(graph.height(highLevel)) + "-" + piece.substr(6);
}

// Ice, Lava, and one more land in one cell: GreenWorld, Archipelago, or Mars. One land fills a
// side and the other two take one corner each across from it, so every edge in the cell is
// straight across or straight down. The piece names the filled land and its side, then where
// one corner land sits. Ice or lava filling a side names the third land, and the third land
// filling a side names the ice:
//  - Blend-Tri-Ice-T-Grass-L: ice along the top, grass bottom left, lava bottom right.
//  - Blend-Tri-Lava-T-Archi-R: lava along the top, sand bottom right, ice bottom left.
//  - Blend-Tri-Mars-T-Ice-L: Mars along the top, ice bottom left, lava bottom right.
// Any other layout of these three lands has no section.
inline std::string triIceName(const BrushGraph& graph, const std::array<int, 4>& corners) {
    int iceLevel = -1;
    int thirdLevel = -1;
    int lavaLevel = -1;
    const char* thirdName = nullptr;
    for (const int level : corners) {
        const std::string_view world = graph.world(level);
        const int height = graph.height(level);
        if (world == "Ice" && height == 85) {
            iceLevel = level;
        } else if (world == "Lava" && height == 86) {
            lavaLevel = level;
        } else {
            const char* name = world == "Green" && height == 85 ? "Grass"
                : world == "Archi" && height == 78 ? "Archi"
                : world == "Mars" && height == 85 ? "Mars" : nullptr;
            if (name == nullptr || (thirdName != nullptr && thirdLevel != level)) {
                return {};
            }
            thirdLevel = level;
            thirdName = name;
        }
    }
    if (iceLevel < 0 || thirdLevel < 0 || lavaLevel < 0) {
        return {};
    }
    const auto maskOf = [&](int level) {
        int mask = 0;
        for (int corner = 0; corner < 4; ++corner) {
            mask |= corners[static_cast<std::size_t>(corner)] == level ? (1 << corner) : 0;
        }
        return mask;
    };
    const auto sideOf = [](int mask) -> const char* {
        if (mask == (kCornerTopLeft | kCornerTopRight)) return "T";
        if (mask == (kCornerBottomLeft | kCornerBottomRight)) return "B";
        if (mask == (kCornerTopLeft | kCornerBottomLeft)) return "L";
        if (mask == (kCornerTopRight | kCornerBottomRight)) return "R";
        return nullptr;
    };
    struct Land { int level; const char* name; };
    const Land lands[3] = {{iceLevel, "Ice"}, {thirdLevel, thirdName}, {lavaLevel, "Lava"}};
    for (const Land& fill : lands) {
        const char* side = sideOf(maskOf(fill.level));
        if (side == nullptr) {
            continue;
        }
        // The other land the name gives: the third land when ice or lava fills a side, otherwise ice.
        const Land& named = fill.level == thirdLevel ? lands[0] : lands[1];
        const int namedMask = maskOf(named.level);
        if (cornerCount(namedMask) != 1) {
            return {};
        }
        const char* where = side[0] == 'T' || side[0] == 'B'
            ? ((namedMask & (kCornerTopRight | kCornerBottomRight)) != 0 ? "R" : "L")
            : ((namedMask & (kCornerBottomLeft | kCornerBottomRight)) != 0 ? "B" : "T");
        return std::string("Blend-Tri-") + fill.name + "-" + side + "-" + named.name + "-" + where;
    }
    return {};
}

// Lava, Lush, and GreenWorld or Mars in one cell, laid out the same way as triIceName. Every
// name has Lush in it.
//  - With GreenWorld: Lava or grass filling a side names the Lush (Blend-Tri-Grass-T-Lush-L has
//    grass along the top, Lush bottom left, lava bottom right), and Lush filling a side names the
//    grass (Blend-Tri-Lush-T-Grass-L).
//  - With Mars: Mars filling a side names the Lush (Blend-Tri-Mars-T-Lush-L), Lush filling a side
//    names the Mars (Blend-Tri-Lush-T-Mars-L), and lava filling a side names the Lush and ends in
//    -Mars (Blend-Tri-Lava-T-Lush-L-Mars), so it does not match the GreenWorld piece.
inline std::string triLushName(const BrushGraph& graph, const std::array<int, 4>& corners) {
    int lushLevel = -1;
    int greenLevel = -1;
    int lavaLevel = -1;
    const char* thirdName = nullptr;
    for (const int level : corners) {
        if (graph.world(level) == "Lush" && graph.height(level) == 77) {
            lushLevel = level;
        } else if (graph.world(level) == "Lava" && graph.height(level) == 86) {
            lavaLevel = level;
        } else if (graph.height(level) == 85 && (graph.world(level) == "Green" || graph.world(level) == "Mars")) {
            const char* name = graph.world(level) == "Green" ? "Grass" : "Mars";
            if (greenLevel >= 0 && greenLevel != level) {
                return {};
            }
            greenLevel = level;
            thirdName = name;
        } else {
            return {};
        }
    }
    if (lushLevel < 0 || greenLevel < 0 || lavaLevel < 0) {
        return {};
    }
    const bool mars = std::string_view(thirdName) == "Mars";
    const auto maskOf = [&](int level) {
        int mask = 0;
        for (int corner = 0; corner < 4; ++corner) {
            mask |= corners[static_cast<std::size_t>(corner)] == level ? (1 << corner) : 0;
        }
        return mask;
    };
    const auto sideOf = [](int mask) -> const char* {
        if (mask == (kCornerTopLeft | kCornerTopRight)) return "T";
        if (mask == (kCornerBottomLeft | kCornerBottomRight)) return "B";
        if (mask == (kCornerTopLeft | kCornerBottomLeft)) return "L";
        if (mask == (kCornerTopRight | kCornerBottomRight)) return "R";
        return nullptr;
    };
    struct Land { int level; const char* name; };
    const Land lands[3] = {{lavaLevel, "Lava"}, {greenLevel, thirdName}, {lushLevel, "Lush"}};
    for (const Land& fill : lands) {
        const char* side = sideOf(maskOf(fill.level));
        if (side == nullptr) {
            continue;
        }
        const Land& named = fill.level == lushLevel ? lands[1] : lands[2];
        const int namedMask = maskOf(named.level);
        if (cornerCount(namedMask) != 1) {
            return {};
        }
        const char* where = side[0] == 'T' || side[0] == 'B'
            ? ((namedMask & (kCornerTopRight | kCornerBottomRight)) != 0 ? "R" : "L")
            : ((namedMask & (kCornerBottomLeft | kCornerBottomRight)) != 0 ? "B" : "T");
        const char* suffix = mars && fill.level == lavaLevel ? "-Mars" : "";
        return std::string("Blend-Tri-") + fill.name + "-" + side + "-" + named.name + "-" + where + suffix;
    }
    return {};
}

// Lava, Crystal and Archipelago in one cell, laid out the same way as triIceName. Lava or
// Archipelago filling a side names the Crystal (Blend-Tri-Lava-T-Crystal-L has lava along the top,
// Crystal bottom left, sand bottom right), and Crystal filling a side names the sand
// (Blend-Tri-Crystal-T-Archi-L). Every name has Crystal in it.
inline std::string triCrystalName(const BrushGraph& graph, const std::array<int, 4>& corners) {
    int crystalLevel = -1;
    int archiLevel = -1;
    int lavaLevel = -1;
    for (const int level : corners) {
        if (graph.world(level) == "Crystal" && graph.height(level) == 85) {
            crystalLevel = level;
        } else if (graph.world(level) == "Archi" && graph.height(level) == 78) {
            archiLevel = level;
        } else if (graph.world(level) == "Lava" && graph.height(level) == 86) {
            lavaLevel = level;
        } else {
            return {};
        }
    }
    if (crystalLevel < 0 || archiLevel < 0 || lavaLevel < 0) {
        return {};
    }
    const auto maskOf = [&](int level) {
        int mask = 0;
        for (int corner = 0; corner < 4; ++corner) {
            mask |= corners[static_cast<std::size_t>(corner)] == level ? (1 << corner) : 0;
        }
        return mask;
    };
    const auto sideOf = [](int mask) -> const char* {
        if (mask == (kCornerTopLeft | kCornerTopRight)) return "T";
        if (mask == (kCornerBottomLeft | kCornerBottomRight)) return "B";
        if (mask == (kCornerTopLeft | kCornerBottomLeft)) return "L";
        if (mask == (kCornerTopRight | kCornerBottomRight)) return "R";
        return nullptr;
    };
    struct Land { int level; const char* name; };
    const Land lands[3] = {{lavaLevel, "Lava"}, {archiLevel, "Archi"}, {crystalLevel, "Crystal"}};
    for (const Land& fill : lands) {
        const char* side = sideOf(maskOf(fill.level));
        if (side == nullptr) {
            continue;
        }
        const Land& named = fill.level == crystalLevel ? lands[1] : lands[2];
        const int namedMask = maskOf(named.level);
        if (cornerCount(namedMask) != 1) {
            return {};
        }
        const char* where = side[0] == 'T' || side[0] == 'B'
            ? ((namedMask & (kCornerTopRight | kCornerBottomRight)) != 0 ? "R" : "L")
            : ((namedMask & (kCornerBottomLeft | kCornerBottomRight)) != 0 ? "B" : "T");
        return std::string("Blend-Tri-") + fill.name + "-" + side + "-" + named.name + "-" + where;
    }
    return {};
}

// Acid, Crystal and Lava in one cell, laid out the same way as triCrystalName. Acid filling a
// side names the Crystal (Blend-Tri-Acid-T-Crystal-L has Acid along the top, Crystal bottom left,
// lava bottom right). Crystal or Lava filling a side names the Acid (Blend-Tri-Crystal-T-Acid-L,
// Blend-Tri-Lava-T-Acid-L). Every name has Acid in it, so none matches the Archipelago Crystal set.
inline std::string triAcidCrystalName(const BrushGraph& graph, const std::array<int, 4>& corners) {
    int acidLevel = -1;
    int crystalLevel = -1;
    int lavaLevel = -1;
    for (const int level : corners) {
        if (graph.world(level) == "Acid" && graph.height(level) == 85) {
            acidLevel = level;
        } else if (graph.world(level) == "Crystal" && graph.height(level) == 85) {
            crystalLevel = level;
        } else if (graph.world(level) == "Lava" && graph.height(level) == 86) {
            lavaLevel = level;
        } else {
            return {};
        }
    }
    if (acidLevel < 0 || crystalLevel < 0 || lavaLevel < 0) {
        return {};
    }
    const auto maskOf = [&](int level) {
        int mask = 0;
        for (int corner = 0; corner < 4; ++corner) {
            mask |= corners[static_cast<std::size_t>(corner)] == level ? (1 << corner) : 0;
        }
        return mask;
    };
    const auto sideOf = [](int mask) -> const char* {
        if (mask == (kCornerTopLeft | kCornerTopRight)) return "T";
        if (mask == (kCornerBottomLeft | kCornerBottomRight)) return "B";
        if (mask == (kCornerTopLeft | kCornerBottomLeft)) return "L";
        if (mask == (kCornerTopRight | kCornerBottomRight)) return "R";
        return nullptr;
    };
    struct Land { int level; const char* name; };
    const Land lands[3] = {{lavaLevel, "Lava"}, {acidLevel, "Acid"}, {crystalLevel, "Crystal"}};
    for (const Land& fill : lands) {
        const char* side = sideOf(maskOf(fill.level));
        if (side == nullptr) {
            continue;
        }
        const Land& named = fill.level == acidLevel ? lands[2] : lands[1];
        const int namedMask = maskOf(named.level);
        if (cornerCount(namedMask) != 1) {
            return {};
        }
        const char* where = side[0] == 'T' || side[0] == 'B'
            ? ((namedMask & (kCornerTopRight | kCornerBottomRight)) != 0 ? "R" : "L")
            : ((namedMask & (kCornerBottomLeft | kCornerBottomRight)) != 0 ? "B" : "T");
        return std::string("Blend-Tri-") + fill.name + "-" + side + "-" + named.name + "-" + where;
    }
    return {};
}

// Acid, Desert and Lava in one cell. Desert filling a side names the Acid
// (Blend-Tri-Desert-T-Acid-L). Acid or Lava filling a side names the Desert
// (Blend-Tri-Acid-T-Desert-L, Blend-Tri-Lava-T-Desert-L). Every name has Desert
// in it, so none matches the GreenWorld Acid set or the Acid Crystal set.
inline std::string triAcidDesertName(const BrushGraph& graph, const std::array<int, 4>& corners) {
    int acidLevel = -1;
    int desertLevel = -1;
    int lavaLevel = -1;
    for (const int level : corners) {
        if (graph.world(level) == "Acid" && graph.height(level) == 85) {
            acidLevel = level;
        } else if (graph.world(level) == "Desert" && graph.height(level) == 80) {
            desertLevel = level;
        } else if (graph.world(level) == "Lava" && graph.height(level) == 86) {
            lavaLevel = level;
        } else {
            return {};
        }
    }
    if (acidLevel < 0 || desertLevel < 0 || lavaLevel < 0) {
        return {};
    }
    const auto maskOf = [&](int level) {
        int mask = 0;
        for (int corner = 0; corner < 4; ++corner) {
            mask |= corners[static_cast<std::size_t>(corner)] == level ? (1 << corner) : 0;
        }
        return mask;
    };
    const auto sideOf = [](int mask) -> const char* {
        if (mask == (kCornerTopLeft | kCornerTopRight)) return "T";
        if (mask == (kCornerBottomLeft | kCornerBottomRight)) return "B";
        if (mask == (kCornerTopLeft | kCornerBottomLeft)) return "L";
        if (mask == (kCornerTopRight | kCornerBottomRight)) return "R";
        return nullptr;
    };
    struct Land { int level; const char* name; };
    const Land lands[3] = {{lavaLevel, "Lava"}, {acidLevel, "Acid"}, {desertLevel, "Desert"}};
    for (const Land& fill : lands) {
        const char* side = sideOf(maskOf(fill.level));
        if (side == nullptr) {
            continue;
        }
        const Land& named = fill.level == desertLevel ? lands[1] : lands[2];
        const int namedMask = maskOf(named.level);
        if (cornerCount(namedMask) != 1) {
            return {};
        }
        const char* where = side[0] == 'T' || side[0] == 'B'
            ? ((namedMask & (kCornerTopRight | kCornerBottomRight)) != 0 ? "R" : "L")
            : ((namedMask & (kCornerBottomLeft | kCornerBottomRight)) != 0 ? "B" : "T");
        return std::string("Blend-Tri-") + fill.name + "-" + side + "-" + named.name + "-" + where;
    }
    return {};
}

// Lava, Crystal and Desert in one cell. Crystal filling a side names Desert
// (Blend-Tri-Crystal-T-Desert-L). Desert filling a side names Crystal
// (Blend-Tri-Desert-T-Crystal-L). Lava filling a side names Crystal and ends in
// -Desert (Blend-Tri-Lava-T-Crystal-L-Desert), so it does not match the
// Archipelago Crystal piece or the Acid Desert piece. Every name has Crystal
// and Desert in it.
inline std::string triCrystalDesertName(const BrushGraph& graph, const std::array<int, 4>& corners) {
    int crystalLevel = -1;
    int desertLevel = -1;
    int lavaLevel = -1;
    for (const int level : corners) {
        if (graph.world(level) == "Crystal" && graph.height(level) == 85) {
            crystalLevel = level;
        } else if (graph.world(level) == "Desert" && graph.height(level) == 80) {
            desertLevel = level;
        } else if (graph.world(level) == "Lava" && graph.height(level) == 86) {
            lavaLevel = level;
        } else {
            return {};
        }
    }
    if (crystalLevel < 0 || desertLevel < 0 || lavaLevel < 0) {
        return {};
    }
    const auto maskOf = [&](int level) {
        int mask = 0;
        for (int corner = 0; corner < 4; ++corner) {
            mask |= corners[static_cast<std::size_t>(corner)] == level ? (1 << corner) : 0;
        }
        return mask;
    };
    const auto sideOf = [](int mask) -> const char* {
        if (mask == (kCornerTopLeft | kCornerTopRight)) return "T";
        if (mask == (kCornerBottomLeft | kCornerBottomRight)) return "B";
        if (mask == (kCornerTopLeft | kCornerBottomLeft)) return "L";
        if (mask == (kCornerTopRight | kCornerBottomRight)) return "R";
        return nullptr;
    };
    struct Land { int level; const char* name; };
    const Land lands[3] = {{lavaLevel, "Lava"}, {crystalLevel, "Crystal"}, {desertLevel, "Desert"}};
    for (const Land& fill : lands) {
        const char* side = sideOf(maskOf(fill.level));
        if (side == nullptr) {
            continue;
        }
        const Land& named = fill.level == crystalLevel ? lands[2] : lands[1];
        const int namedMask = maskOf(named.level);
        if (cornerCount(namedMask) != 1) {
            return {};
        }
        const char* where = side[0] == 'T' || side[0] == 'B'
            ? ((namedMask & (kCornerTopRight | kCornerBottomRight)) != 0 ? "R" : "L")
            : ((namedMask & (kCornerBottomLeft | kCornerBottomRight)) != 0 ? "B" : "T");
        const char* suffix = fill.level == lavaLevel ? "-Desert" : "";
        return std::string("Blend-Tri-") + fill.name + "-" + side + "-" + named.name + "-" + where + suffix;
    }
    return {};
}

// Lava, GreenWorld and Desert in one cell. Grass filling a side names Desert
// (Blend-Tri-Grass-T-Desert-L). Desert filling a side names Grass
// (Blend-Tri-Desert-T-Grass-L). Lava filling a side names Grass and ends in
// -Desert (Blend-Tri-Lava-T-Grass-L-Desert), so it does not match the Ice Grass
// piece or the Acid Desert piece. Every name has Grass and Desert in it.
inline std::string triGreenDesertName(const BrushGraph& graph, const std::array<int, 4>& corners) {
    int greenLevel = -1;
    int desertLevel = -1;
    int lavaLevel = -1;
    for (const int level : corners) {
        if (graph.world(level) == "Green" && graph.height(level) == 85) {
            greenLevel = level;
        } else if (graph.world(level) == "Desert" && graph.height(level) == 80) {
            desertLevel = level;
        } else if (graph.world(level) == "Lava" && graph.height(level) == 86) {
            lavaLevel = level;
        } else {
            return {};
        }
    }
    if (greenLevel < 0 || desertLevel < 0 || lavaLevel < 0) {
        return {};
    }
    const auto maskOf = [&](int level) {
        int mask = 0;
        for (int corner = 0; corner < 4; ++corner) {
            mask |= corners[static_cast<std::size_t>(corner)] == level ? (1 << corner) : 0;
        }
        return mask;
    };
    const auto sideOf = [](int mask) -> const char* {
        if (mask == (kCornerTopLeft | kCornerTopRight)) return "T";
        if (mask == (kCornerBottomLeft | kCornerBottomRight)) return "B";
        if (mask == (kCornerTopLeft | kCornerBottomLeft)) return "L";
        if (mask == (kCornerTopRight | kCornerBottomRight)) return "R";
        return nullptr;
    };
    struct Land { int level; const char* name; };
    const Land lands[3] = {{lavaLevel, "Lava"}, {greenLevel, "Grass"}, {desertLevel, "Desert"}};
    for (const Land& fill : lands) {
        const char* side = sideOf(maskOf(fill.level));
        if (side == nullptr) {
            continue;
        }
        const Land& named = fill.level == greenLevel ? lands[2] : lands[1];
        const int namedMask = maskOf(named.level);
        if (cornerCount(namedMask) != 1) {
            return {};
        }
        const char* where = side[0] == 'T' || side[0] == 'B'
            ? ((namedMask & (kCornerTopRight | kCornerBottomRight)) != 0 ? "R" : "L")
            : ((namedMask & (kCornerBottomLeft | kCornerBottomRight)) != 0 ? "B" : "T");
        const char* suffix = fill.level == lavaLevel ? "-Desert" : "";
        return std::string("Blend-Tri-") + fill.name + "-" + side + "-" + named.name + "-" + where + suffix;
    }
    return {};
}

// Lava, Archipelago and Desert in one cell. Archipelago filling a side names Desert
// (Blend-Tri-Archi-T-Desert-L). Desert filling a side names Archipelago
// (Blend-Tri-Desert-T-Archi-L). Lava filling a side names Archipelago and ends in
// -Desert (Blend-Tri-Lava-T-Archi-L-Desert), so it does not match the Ice Archipelago
// piece or the Acid Desert piece. Every name has Archi and Desert in it.
inline std::string triArchiDesertName(const BrushGraph& graph, const std::array<int, 4>& corners) {
    int archiLevel = -1;
    int desertLevel = -1;
    int lavaLevel = -1;
    for (const int level : corners) {
        if (graph.world(level) == "Archi" && graph.height(level) == 78) {
            archiLevel = level;
        } else if (graph.world(level) == "Desert" && graph.height(level) == 80) {
            desertLevel = level;
        } else if (graph.world(level) == "Lava" && graph.height(level) == 86) {
            lavaLevel = level;
        } else {
            return {};
        }
    }
    if (archiLevel < 0 || desertLevel < 0 || lavaLevel < 0) {
        return {};
    }
    const auto maskOf = [&](int level) {
        int mask = 0;
        for (int corner = 0; corner < 4; ++corner) {
            mask |= corners[static_cast<std::size_t>(corner)] == level ? (1 << corner) : 0;
        }
        return mask;
    };
    const auto sideOf = [](int mask) -> const char* {
        if (mask == (kCornerTopLeft | kCornerTopRight)) return "T";
        if (mask == (kCornerBottomLeft | kCornerBottomRight)) return "B";
        if (mask == (kCornerTopLeft | kCornerBottomLeft)) return "L";
        if (mask == (kCornerTopRight | kCornerBottomRight)) return "R";
        return nullptr;
    };
    struct Land { int level; const char* name; };
    const Land lands[3] = {{lavaLevel, "Lava"}, {archiLevel, "Archi"}, {desertLevel, "Desert"}};
    for (const Land& fill : lands) {
        const char* side = sideOf(maskOf(fill.level));
        if (side == nullptr) {
            continue;
        }
        const Land& named = fill.level == archiLevel ? lands[2] : lands[1];
        const int namedMask = maskOf(named.level);
        if (cornerCount(namedMask) != 1) {
            return {};
        }
        const char* where = side[0] == 'T' || side[0] == 'B'
            ? ((namedMask & (kCornerTopRight | kCornerBottomRight)) != 0 ? "R" : "L")
            : ((namedMask & (kCornerBottomLeft | kCornerBottomRight)) != 0 ? "B" : "T");
        const char* suffix = fill.level == lavaLevel ? "-Desert" : "";
        return std::string("Blend-Tri-") + fill.name + "-" + side + "-" + named.name + "-" + where + suffix;
    }
    return {};
}

// Lava, Ice and Desert in one cell. Ice filling a side names Desert
// (Blend-Tri-Ice-T-Desert-L). Desert filling a side names Ice
// (Blend-Tri-Desert-T-Ice-L). Lava filling a side names Ice and ends in
// -Desert (Blend-Tri-Lava-T-Ice-L-Desert), so it does not match the Ice Grass
// piece or the Acid Desert piece. Every name has Ice and Desert in it.
inline std::string triIceDesertName(const BrushGraph& graph, const std::array<int, 4>& corners) {
    int iceLevel = -1;
    int desertLevel = -1;
    int lavaLevel = -1;
    for (const int level : corners) {
        if (graph.world(level) == "Ice" && graph.height(level) == 85) {
            iceLevel = level;
        } else if (graph.world(level) == "Desert" && graph.height(level) == 80) {
            desertLevel = level;
        } else if (graph.world(level) == "Lava" && graph.height(level) == 86) {
            lavaLevel = level;
        } else {
            return {};
        }
    }
    if (iceLevel < 0 || desertLevel < 0 || lavaLevel < 0) {
        return {};
    }
    const auto maskOf = [&](int level) {
        int mask = 0;
        for (int corner = 0; corner < 4; ++corner) {
            mask |= corners[static_cast<std::size_t>(corner)] == level ? (1 << corner) : 0;
        }
        return mask;
    };
    const auto sideOf = [](int mask) -> const char* {
        if (mask == (kCornerTopLeft | kCornerTopRight)) return "T";
        if (mask == (kCornerBottomLeft | kCornerBottomRight)) return "B";
        if (mask == (kCornerTopLeft | kCornerBottomLeft)) return "L";
        if (mask == (kCornerTopRight | kCornerBottomRight)) return "R";
        return nullptr;
    };
    struct Land { int level; const char* name; };
    const Land lands[3] = {{lavaLevel, "Lava"}, {iceLevel, "Ice"}, {desertLevel, "Desert"}};
    for (const Land& fill : lands) {
        const char* side = sideOf(maskOf(fill.level));
        if (side == nullptr) {
            continue;
        }
        const Land& named = fill.level == iceLevel ? lands[2] : lands[1];
        const int namedMask = maskOf(named.level);
        if (cornerCount(namedMask) != 1) {
            return {};
        }
        const char* where = side[0] == 'T' || side[0] == 'B'
            ? ((namedMask & (kCornerTopRight | kCornerBottomRight)) != 0 ? "R" : "L")
            : ((namedMask & (kCornerBottomLeft | kCornerBottomRight)) != 0 ? "B" : "T");
        const char* suffix = fill.level == lavaLevel ? "-Desert" : "";
        return std::string("Blend-Tri-") + fill.name + "-" + side + "-" + named.name + "-" + where + suffix;
    }
    return {};
}

// GreenWorld, Acid and Lava in one cell. Acid fills one side. Grass and lava
// split the opposite side, so the piece is Blend-Tri-Acid-R-Grass-B when acid
// is on the right and grass is the bottom of the left side. Any other layout
// of these three lands has no section. Ice and Lava with a third land use triIceName.
inline std::string triBlendName(const BrushGraph& graph, const std::array<int, 4>& corners) {
    if (std::string ice = triIceName(graph, corners); !ice.empty()) {
        return ice;
    }
    if (std::string lush = triLushName(graph, corners); !lush.empty()) {
        return lush;
    }
    if (std::string crystal = triCrystalName(graph, corners); !crystal.empty()) {
        return crystal;
    }
    if (std::string acidCrystal = triAcidCrystalName(graph, corners); !acidCrystal.empty()) {
        return acidCrystal;
    }
    if (std::string acidDesert = triAcidDesertName(graph, corners); !acidDesert.empty()) {
        return acidDesert;
    }
    if (std::string crystalDesert = triCrystalDesertName(graph, corners); !crystalDesert.empty()) {
        return crystalDesert;
    }
    if (std::string greenDesert = triGreenDesertName(graph, corners); !greenDesert.empty()) {
        return greenDesert;
    }
    if (std::string archiDesert = triArchiDesertName(graph, corners); !archiDesert.empty()) {
        return archiDesert;
    }
    if (std::string iceDesert = triIceDesertName(graph, corners); !iceDesert.empty()) {
        return iceDesert;
    }
    int greenLevel = -1;
    int acidLevel = -1;
    int lavaLevel = -1;
    for (const int level : corners) {
        if (graph.world(level) == "Green" && graph.height(level) == 85) {
            greenLevel = level;
        } else if (graph.world(level) == "Acid" && graph.height(level) == 85) {
            acidLevel = level;
        } else if (graph.world(level) == "Lava" && graph.height(level) == 86) {
            lavaLevel = level;
        } else {
            return {};
        }
    }
    if (greenLevel < 0 || acidLevel < 0 || lavaLevel < 0 || greenLevel == acidLevel || greenLevel == lavaLevel || acidLevel == lavaLevel) {
        return {};
    }
    const auto maskOf = [&](int level) {
        int mask = 0;
        for (int corner = 0; corner < 4; ++corner) {
            mask |= corners[static_cast<std::size_t>(corner)] == level ? (1 << corner) : 0;
        }
        return mask;
    };
    const int acidMask = maskOf(acidLevel);
    const int grassMask = maskOf(greenLevel);
    const char* acidSide = nullptr;
    if (acidMask == (kCornerTopLeft | kCornerTopRight)) {
        acidSide = "T";
    } else if (acidMask == (kCornerBottomLeft | kCornerBottomRight)) {
        acidSide = "B";
    } else if (acidMask == (kCornerTopLeft | kCornerBottomLeft)) {
        acidSide = "L";
    } else if (acidMask == (kCornerTopRight | kCornerBottomRight)) {
        acidSide = "R";
    }
    if (acidSide == nullptr || cornerCount(grassMask) != 1) {
        return {};
    }
    const char* grassSide = nullptr;
    if (acidSide[0] == 'T' || acidSide[0] == 'B') {
        grassSide = (grassMask & (kCornerTopRight | kCornerBottomRight)) != 0 ? "R" : "L";
    } else {
        grassSide = (grassMask & (kCornerBottomLeft | kCornerBottomRight)) != 0 ? "B" : "T";
    }
    return std::string("Blend-Tri-Acid-") + acidSide + "-Grass-" + grassSide;
}

// The section a cell needs, from the level at each of its corners, without a
// variation number. Empty for a flat cell and for a layout that has no section.
// Two levels use the family that joins them. Base-0, Base-68 and Base-85
// together use a CoastDSL section, Base-0, Base-68 and Base-80 use one too,
// Base-0, Base-68 and Base-78 use one too, and Base-219, Base-85 and either
// Base-0 or Base-68 use a CoastalCliff section. GreenWorld, Acid and Lava use
// a Blend-Tri-Acid section when acid fills one side.
inline std::string brushPieceName(const BrushGraph& graph, const std::array<int, 4>& corners, std::string_view coastLiquid = "Water") {
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
        const TransitionStyle style = graph.style(sorted[0], sorted[1]);
        if (style == TransitionStyle::Cliff) {
            return cliffTransitionName(graph, sorted[0], sorted[1], maskOf(sorted[1]));
        }
        // Levels are stored from lowest height to highest. A world blend marks the
        // Mars, GreenWorld, Acid, Desert, or Lush corners when the other land is Lava.
        // Archipelago beside GreenWorld is Blend-Archi. Archipelago beside Lava is
        // Blend-ArchiLava, and Archipelago beside Acid is Blend-ArchiAcid, so those
        // blends do not share a file.
        int marked = sorted[1];
        std::string family;
        if (style == TransitionStyle::WorldBlend && graph.height(sorted[0]) == 78 && graph.height(sorted[1]) == 86) {
            marked = sorted[0];
            family = "ArchiLava";
        } else if (style == TransitionStyle::WorldBlend && graph.height(sorted[0]) == 78 && graph.height(sorted[1]) == 85
            && graph.world(sorted[0]) == "Archi" && graph.world(sorted[1]) == "Crystal") {
            // The mask marks the sand. Blend-ArchiCrystal-T has Archipelago along the top.
            marked = sorted[0];
            family = "ArchiCrystal";
        } else if (style == TransitionStyle::WorldBlend && graph.height(sorted[0]) == 78 && graph.height(sorted[1]) == 85
            && graph.world(sorted[0]) == "Archi" && graph.world(sorted[1]) == "Ice") {
            // The mask marks the ice. Blend-IceArchi-T has snow along the top.
            marked = sorted[1];
            family = "IceArchi";
        } else if (style == TransitionStyle::WorldBlend && graph.height(sorted[0]) == 77 && graph.height(sorted[1]) == 78
            && ((graph.world(sorted[0]) == "Lush" && graph.world(sorted[1]) == "Archi")
                || (graph.world(sorted[0]) == "Archi" && graph.world(sorted[1]) == "Lush"))) {
            // The mask marks the sand. Blend-ArchiLush-T has Archipelago along the top.
            marked = graph.world(sorted[0]) == "Archi" ? sorted[0] : sorted[1];
            family = "ArchiLush";
        } else if (style == TransitionStyle::WorldBlend && graph.height(sorted[0]) == 78 && graph.height(sorted[1]) == 85
            && ((graph.world(sorted[0]) == "Archi" && graph.world(sorted[1]) == "Acid")
                || (graph.world(sorted[0]) == "Acid" && graph.world(sorted[1]) == "Archi"))) {
            // The mask marks the sand. Blend-ArchiAcid-T has Archipelago along the top.
            marked = graph.world(sorted[0]) == "Archi" ? sorted[0] : sorted[1];
            family = "ArchiAcid";
        } else if (style == TransitionStyle::WorldBlend
            && ((graph.height(sorted[0]) == 78 && graph.height(sorted[1]) == 80)
                || (graph.height(sorted[0]) == 80 && graph.height(sorted[1]) == 78))
            && ((graph.world(sorted[0]) == "Archi" && graph.world(sorted[1]) == "Desert")
                || (graph.world(sorted[0]) == "Desert" && graph.world(sorted[1]) == "Archi"))) {
            // The mask marks the sand. Blend-ArchiDesert-T has Archipelago along the top.
            marked = graph.world(sorted[0]) == "Archi" ? sorted[0] : sorted[1];
            family = "ArchiDesert";
        } else if (style == TransitionStyle::WorldBlend && (graph.world(sorted[0]) == "Archi" || graph.world(sorted[1]) == "Archi")) {
            marked = graph.world(sorted[0]) == "Archi" ? sorted[0] : sorted[1];
        } else if (style == TransitionStyle::WorldBlend && graph.world(sorted[0]) == "Ice" && graph.world(sorted[1]) == "Lava") {
            // The mask marks the ice. Blend-IceLava-T has snow along the top.
            marked = sorted[0];
            family = "IceLava";
        } else if (style == TransitionStyle::WorldBlend && graph.world(sorted[0]) == "Crystal" && graph.world(sorted[1]) == "Lava") {
            // The mask marks Crystal. Blend-CrystalLava-T has Crystal along the top.
            marked = sorted[0];
            family = "CrystalLava";
        } else if (style == TransitionStyle::WorldBlend && graph.height(sorted[0]) == 85 && graph.height(sorted[1]) == 85
            && ((graph.world(sorted[0]) == "Acid" && graph.world(sorted[1]) == "Crystal")
                || (graph.world(sorted[0]) == "Crystal" && graph.world(sorted[1]) == "Acid"))) {
            // The mask marks Acid. Blend-AcidCrystal-T has Acid along the top.
            marked = graph.world(sorted[0]) == "Acid" ? sorted[0] : sorted[1];
            family = "AcidCrystal";
        } else if (style == TransitionStyle::WorldBlend
            && ((graph.height(sorted[0]) == 80 && graph.height(sorted[1]) == 85)
                || (graph.height(sorted[0]) == 85 && graph.height(sorted[1]) == 80))
            && ((graph.world(sorted[0]) == "Acid" && graph.world(sorted[1]) == "Desert")
                || (graph.world(sorted[0]) == "Desert" && graph.world(sorted[1]) == "Acid"))) {
            // The mask marks Acid. Blend-AcidDesert-T has Acid along the top.
            marked = graph.world(sorted[0]) == "Acid" ? sorted[0] : sorted[1];
            family = "AcidDesert";
        } else if (style == TransitionStyle::WorldBlend
            && ((graph.height(sorted[0]) == 80 && graph.height(sorted[1]) == 85)
                || (graph.height(sorted[0]) == 85 && graph.height(sorted[1]) == 80))
            && ((graph.world(sorted[0]) == "Crystal" && graph.world(sorted[1]) == "Desert")
                || (graph.world(sorted[0]) == "Desert" && graph.world(sorted[1]) == "Crystal"))) {
            // The mask marks Crystal. Blend-CrystalDesert-T has Crystal along the top.
            marked = graph.world(sorted[0]) == "Crystal" ? sorted[0] : sorted[1];
            family = "CrystalDesert";
        } else if (style == TransitionStyle::WorldBlend
            && ((graph.height(sorted[0]) == 80 && graph.height(sorted[1]) == 85)
                || (graph.height(sorted[0]) == 85 && graph.height(sorted[1]) == 80))
            && ((graph.world(sorted[0]) == "Green" && graph.world(sorted[1]) == "Desert")
                || (graph.world(sorted[0]) == "Desert" && graph.world(sorted[1]) == "Green"))) {
            // The mask marks Desert. Blend-DesertGreen-T has Desert along the top, as Blend-Archi-T has sand.
            marked = graph.world(sorted[0]) == "Desert" ? sorted[0] : sorted[1];
            family = "DesertGreen";
        } else if (style == TransitionStyle::WorldBlend
            && ((graph.height(sorted[0]) == 80 && graph.height(sorted[1]) == 85)
                || (graph.height(sorted[0]) == 85 && graph.height(sorted[1]) == 80))
            && ((graph.world(sorted[0]) == "Ice" && graph.world(sorted[1]) == "Desert")
                || (graph.world(sorted[0]) == "Desert" && graph.world(sorted[1]) == "Ice"))) {
            // The mask marks the ice. Blend-IceDesert-T has ice along the top.
            marked = graph.world(sorted[0]) == "Ice" ? sorted[0] : sorted[1];
            family = "IceDesert";
        } else if (style == TransitionStyle::WorldBlend && graph.world(sorted[1]) == "Lava" && graph.world(sorted[0]) != "Lava") {
            marked = sorted[0];
        } else if (style == TransitionStyle::WorldBlend && graph.height(sorted[0]) == 85 && graph.height(sorted[1]) == 85
            && ((graph.world(sorted[0]) == "Ice" && graph.world(sorted[1]) == "Mars")
                || (graph.world(sorted[0]) == "Mars" && graph.world(sorted[1]) == "Ice"))) {
            // The mask marks the ice. Blend-IceMars-T has ice along the top.
            marked = graph.world(sorted[0]) == "Ice" ? sorted[0] : sorted[1];
            family = "IceMars";
        } else if (style == TransitionStyle::WorldBlend && graph.height(sorted[0]) == 85 && graph.height(sorted[1]) == 85
            && ((graph.world(sorted[0]) == "Ice" && graph.world(sorted[1]) == "Green")
                || (graph.world(sorted[0]) == "Green" && graph.world(sorted[1]) == "Ice"))) {
            // The mask marks the ice. Blend-IceGreen-T has ice along the top.
            marked = graph.world(sorted[0]) == "Ice" ? sorted[0] : sorted[1];
            family = "IceGreen";
        } else if (style == TransitionStyle::WorldBlend && graph.height(sorted[0]) == 77 && graph.height(sorted[1]) == 85
            && graph.world(sorted[0]) == "Lush" && graph.world(sorted[1]) == "Green") {
            // The mask marks the GreenWorld grass. Blend-GreenLush-T has GreenWorld along the top.
            marked = sorted[1];
            family = "GreenLush";
        } else if (style == TransitionStyle::WorldBlend && graph.height(sorted[0]) == 77 && graph.height(sorted[1]) == 85
            && graph.world(sorted[0]) == "Lush" && graph.world(sorted[1]) == "Mars") {
            // The mask marks Mars. Blend-LushMars-B has Mars along the bottom.
            marked = sorted[1];
            family = "LushMars";
        } else if (style == TransitionStyle::WorldBlend
            && ((graph.height(sorted[0]) == 77 && graph.height(sorted[1]) == 85)
                || (graph.height(sorted[0]) == 85 && graph.height(sorted[1]) == 77))
            && ((graph.world(sorted[0]) == "Lush" && graph.world(sorted[1]) == "Ice")
                || (graph.world(sorted[0]) == "Ice" && graph.world(sorted[1]) == "Lush"))) {
            // The mask marks the ice. Blend-LushIce-T has ice along the top.
            marked = graph.world(sorted[0]) == "Ice" ? sorted[0] : sorted[1];
            family = "LushIce";
        } else if (style == TransitionStyle::WorldBlend && graph.height(sorted[0]) == 85 && graph.height(sorted[1]) == 85
            && ((graph.world(sorted[0]) == "Green" && graph.world(sorted[1]) == "Metal")
                || (graph.world(sorted[0]) == "Metal" && graph.world(sorted[1]) == "Green"))) {
            // The mask marks Metal. Blend-GreenMetal-T has Metal along the top.
            marked = graph.world(sorted[0]) == "Metal" ? sorted[0] : sorted[1];
            family = "GreenMetal";
        } else if (style == TransitionStyle::WorldBlend && graph.height(sorted[0]) == 85 && graph.height(sorted[1]) == 85
            && ((graph.world(sorted[0]) == "Green" && graph.world(sorted[1]) == "Mars")
                || (graph.world(sorted[0]) == "Mars" && graph.world(sorted[1]) == "Green"))) {
            // The mask marks Mars. Blend-GreenMars-T has Mars along the top.
            marked = graph.world(sorted[0]) == "Mars" ? sorted[0] : sorted[1];
            family = "GreenMars";
        } else if (style == TransitionStyle::WorldBlend
            && ((graph.world(sorted[0]) == "Green" && graph.world(sorted[1]) == "Acid")
                || (graph.world(sorted[0]) == "Acid" && graph.world(sorted[1]) == "Green"))) {
            // The mask marks the grass. Blend-GreenAcid-T has grass along the top.
            marked = graph.world(sorted[0]) == "Green" ? sorted[0] : sorted[1];
            family = "GreenAcid";
        }
        if (family.empty()) {
            family = std::string(graph.world(marked));
        }
        if (family.empty()) {
            family = "Mars";
        }
        return transitionPieceName(style, maskOf(marked), coastLiquid, family);
    }
    if (distinct == 3) {
        const std::string tri = triBlendName(graph, corners);
        if (!tri.empty()) {
            return tri;
        }
        const int low = graph.height(sorted[0]);
        const int middle = graph.height(sorted[1]);
        const int high = graph.height(sorted[2]);
        if (low == 0 && middle == 68 && (high == 85 || high == 80 || high == 78)) {
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

// The 8 x 8 brush paints the point where four 16 x 16 cells meet. The cursor
// snaps to the nearest such point, and a map large enough to have one keeps the
// snap off the outer edge so four cells always share it. nodeX and nodeY are
// that point, in the same coordinates planBrush uses.
inline bool brushCornerNode(int tileX, int tileY, int cellsX, int cellsY, int& nodeX, int& nodeY) {
    if (tileX < 0 || tileY < 0 || cellsX < 0 || cellsY < 0) {
        return false;
    }
    const int half = kBrushCellTiles / 2;
    nodeX = (tileX + half) / kBrushCellTiles;
    nodeY = (tileY + half) / kBrushCellTiles;
    if (cellsX > 1) {
        nodeX = std::clamp(nodeX, 1, cellsX - 1);
    }
    if (cellsY > 1) {
        nodeY = std::clamp(nodeY, 1, cellsY - 1);
    }
    return nodeX >= 0 && nodeY >= 0 && nodeX <= cellsX && nodeY <= cellsY;
}

// Top-left cell of a square brush centered on the pointer. 16 x 16 is the cell
// under the pointer. 32, 64, 128, 256 and 512 cover an even number of cells, so
// the center is the nearest point where four cells meet and the block reaches
// the same distance on every side of that point.
inline void brushBlockOrigin(int tileX, int tileY, int brushTiles, int cellsX, int cellsY, int& originX, int& originY) {
    const int blockCells = std::max(1, brushTiles / kBrushCellTiles);
    if (blockCells <= 1) {
        originX = tileX / kBrushCellTiles;
        originY = tileY / kBrushCellTiles;
        return;
    }
    const int half = kBrushCellTiles / 2;
    int nodeX = (tileX + half) / kBrushCellTiles;
    int nodeY = (tileY + half) / kBrushCellTiles;
    if (cellsX >= 0) {
        nodeX = std::clamp(nodeX, 0, cellsX);
    }
    if (cellsY >= 0) {
        nodeY = std::clamp(nodeY, 0, cellsY);
    }
    originX = nodeX - blockCells / 2;
    originY = nodeY - blockCells / 2;
}

// "Green47" is 47. Names that do not start with Green and a number are ignored,
// so NewHill_85_01 is not a Hill-85 file.
inline bool greenHillNumber(std::string_view name, int& number) {
    if (name.size() < 6) {
        return false;
    }
    constexpr std::string_view prefix = "green";
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        char character = name[i];
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
        if (character != prefix[i]) {
            return false;
        }
    }
    int value = 0;
    for (std::size_t i = prefix.size(); i < name.size(); ++i) {
        if (name[i] < '0' || name[i] > '9') {
            return false;
        }
        value = std::min(1000000, value * 10 + (name[i] - '0'));
    }
    number = value;
    return true;
}

// Which Hill-85 button a GreenWorld hill number belongs to, from the file name.
// 8, 16, or 0 when that number is not used. Green47 is listed with the 8 x 8
// names; the section is 16 x 16, and the brush follows the section.
inline int listedGreenHillSpan(int number) {
    if ((number >= 1 && number <= 20) || (number >= 23 && number <= 28) || number == 30
        || (number >= 35 && number <= 44) || (number >= 46 && number <= 55)) {
        return 8;
    }
    if (number >= 56 && number <= 72) {
        return 16;
    }
    return 0;
}

// Green21, Green22, Green29, Green31 through Green34, and Green45 are the Rough-85 set.
inline bool listedGreenRough(int number) {
    return number == 21 || number == 22 || number == 29 || (number >= 31 && number <= 34) || number == 45;
}

// "GreenIsland07" is 7. GreenIsland01 through GreenIsland10 are the Island button.
inline bool greenIslandNumber(std::string_view name, int& number) {
    constexpr std::string_view prefix = "greenisland";
    if (name.size() <= prefix.size()) {
        return false;
    }
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        char character = name[i];
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
        if (character != prefix[i]) {
            return false;
        }
    }
    int value = 0;
    for (std::size_t i = prefix.size(); i < name.size(); ++i) {
        if (name[i] < '0' || name[i] > '9') {
            return false;
        }
        value = std::min(1000000, value * 10 + (name[i] - '0'));
    }
    number = value;
    return number >= 1 && number <= 10;
}

// "Hill_21" is 21. Hill_01 through Hill_37 are the Lava Hill_ files.
inline bool lavaHillNumber(std::string_view name, int& number) {
    constexpr std::string_view prefix = "hill_";
    if (name.size() <= prefix.size()) {
        return false;
    }
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        char character = name[i];
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
        if (character != prefix[i]) {
            return false;
        }
    }
    int value = 0;
    for (std::size_t i = prefix.size(); i < name.size(); ++i) {
        if (name[i] < '0' || name[i] > '9') {
            return false;
        }
        value = std::min(1000000, value * 10 + (name[i] - '0'));
    }
    number = value;
    return number >= 1 && number <= 37;
}

// Which Hill-86 button a Lava hill belongs to. 8, 16, or 0 when the section is
// another size. Hill_21 through Hill_29 are 16 x 16. Hill_18, Hill_30, Hill_31,
// Hill_33, Hill_34, and Hill_35 are not 8 x 8 or 16 x 16.
inline int listedLavaHillSpan(int number) {
    if (number >= 21 && number <= 29) {
        return 16;
    }
    if ((number >= 1 && number <= 17) || number == 19 || number == 20 || number == 32 || number == 36 || number == 37) {
        return 8;
    }
    return 0;
}

// "LDt07_65" is 7. LDt01_65 through LDt08_65 and LDt41_65 through LDt44_65 are the Lush pool button.
inline bool lushPoolNumber(std::string_view name, int& number) {
    constexpr std::string_view prefix = "ldt";
    constexpr std::string_view suffix = "_65";
    if (name.size() <= prefix.size() + suffix.size()) {
        return false;
    }
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        char character = name[i];
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
        if (character != prefix[i]) {
            return false;
        }
    }
    if (name.substr(name.size() - suffix.size()) != suffix) {
        return false;
    }
    int value = 0;
    for (std::size_t i = prefix.size(); i < name.size() - suffix.size(); ++i) {
        if (name[i] < '0' || name[i] > '9') {
            return false;
        }
        value = std::min(1000000, value * 10 + (name[i] - '0'));
    }
    number = value;
    return (number >= 1 && number <= 8) || (number >= 41 && number <= 44);
}

// Top-left tile of a hill. 16 x 16 uses the brush cell under the pointer.
// 8 x 8 uses the quadrant of that cell under the pointer.
inline void hillStampOrigin(int tileX, int tileY, int span, int& originX, int& originY) {
    const int cell = kBrushCellTiles;
    originX = tileX >= 0 ? tileX / cell * cell : 0;
    originY = tileY >= 0 ? tileY / cell * cell : 0;
    if (span > 0 && span < cell && tileX >= 0 && tileY >= 0) {
        if (tileX % cell >= span) {
            originX += span;
        }
        if (tileY % cell >= span) {
            originY += span;
        }
    }
}

// Plans a stroke over a square block of cells. The painted cells become flat on
// `level`. Corners that would end up in a cell with a paint they are not joined
// to move one step along the chain of joined paints toward the painted one,
// which makes the bands of transition cells around the block. readNode gives a
// corner's level before the stroke. Corners run from 0 to cellsX across and 0 to
// cellsY down, and the cell at (x, y) owns corners (x, y), (x + 1, y), (x, y + 1)
// and (x + 1, y + 1).
//
// blockCells of 0 paints one corner instead of a block. blockX and blockY are
// that corner. The cells that share it become corner pieces, and no cell is
// filled flat by the stroke itself.
using BrushNodeReader = std::function<int(int nodeX, int nodeY)>;

inline BrushPlan planBrush(int cellsX, int cellsY, int blockX, int blockY, int blockCells, int level, const BrushGraph& graph, const BrushNodeReader& readNode) {
    BrushPlan plan;
    const bool corner = blockCells == 0;
    if (cellsX <= 0 || cellsY <= 0 || blockCells < 0 || !readNode || level < 0 || level >= graph.size()) {
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
    // Corners of the painted block keep the painted level. A corner stroke locks its one corner.
    const auto isLocked = [&](int x, int y) {
        if (corner) {
            return x == blockX && y == blockY;
        }
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
    if (corner) {
        setLevel(blockX, blockY, level);
        if (inLattice(blockX, blockY)) {
            work.emplace_back(blockX, blockY);
        }
    } else {
        for (int cy = blockY; cy < blockY + blockCells; ++cy) {
            for (int cx = blockX; cx < blockX + blockCells; ++cx) {
                if (!isCell(cx, cy)) {
                    continue;
                }
                for (int cornerIndex = 0; cornerIndex < 4; ++cornerIndex) {
                    const int x = cx + (cornerIndex & 1);
                    const int y = cy + (cornerIndex >> 1);
                    if (slots.find(keyOf(x, y)) == slots.end()) {
                        setLevel(x, y, level);
                        work.emplace_back(x, y);
                    }
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
                const bool inBlock = !corner && cx >= blockX && cx < blockX + blockCells && cy >= blockY && cy < blockY + blockCells;
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
