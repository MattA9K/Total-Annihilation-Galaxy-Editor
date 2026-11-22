#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <functional>
#include <memory>
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
// Lava, GreenWorld and Mars share a cell the same way, and every name has Grass and Mars in it.
// Lava, Ice and Lush share a cell the same way, and every name has Ice and Lush in it.
// Lava, Desert and Lush share a cell the same way, and every name has Desert and Lush in it.
// Lava, GreenWorld and Crystal share a cell the same way, and every name has Grass and Crystal in it.
// Lava, Archipelago and GreenWorld share a cell the same way, and every name has Grass and Archi in it.
// Lava, Acid and Archipelago share a cell the same way, and every name has Acid and Archi in it.
// Lava, Ice and Crystal share a cell the same way, and every name has Ice and Crystal in it.
// Lava, Ice and Acid share a cell the same way, and every name has Ice and Acid in it.
// Lava, Archipelago and Lush share a cell the same way, and every name has Archi and Lush in it.
// Lava, Crystal and Lush share a cell the same way, and every name has Crystal and Lush in it.
// Lava, Desert and Mars share a cell the same way, and every name has Desert and Mars in it.
// Lava, Acid and Lush share a cell the same way, and every name has Acid and Lush in it.
// Lava, Acid and Mars share a cell the same way, and every name has Acid and Mars in it.
// Lava, Archipelago and Mars share a cell the same way, and every name has Archi and Mars in it.
// Lava, Crystal and Mars share a cell the same way, and every name has Crystal and Mars in it.
// Lava, GreenWorld and Metal share a cell the same way, and every name has Grass and Metal in it.
// Lava, Desert and Metal share a cell the same way, and every name has Desert and Metal in it.
// Lava, Archipelago and Metal share a cell the same way, and every name has Archi and Metal in it.
// Lava, Lush and Metal share a cell the same way, and every name has Lush and Metal in it.
// Lava, Ice and Metal share a cell the same way, and every name has Ice and Metal in it.
// Lava, Acid and Metal share a cell the same way, and every name has Acid and Metal in it.
// Lava, Crystal and Metal share a cell the same way, and every name has Crystal and Metal in it.
// Mars Base-0 joins GreenWorld Base-0 with pieces named Blend-WaterGreenMars, and the mask marks
// the Mars water, and Crystal Base-0 joins it with Blend-WaterGreenCrystal, marking the Crystal
// water. Acid Base-0 joins it with Blend-WaterGreenAcid, Archipelago and Desert Base-0 with Blend-WaterGreenDesert, and Lush Base-27 with
// Blend-WaterGreenLush, whose heights rise from 0 to 27. Archipelago and Desert water joins Acid,
// Lush, Crystal and Mars water with Blend-WaterDesertAcid, -Lush, -Crystal and -Mars, marking the
// other water. Mars water joins Lush, Crystal and Acid water with Blend-WaterMarsLush, -Crystal and
// -Acid, marking the other water. Lush water joins Crystal and Acid water with Blend-WaterLushCrystal
// and -Acid, marking the lower water. Crystal water joins Acid water with Blend-WaterCrystalAcid,
// marking the Acid water. Ice and Metal Base-0 are the same blue
// water and use the same pieces, so an island of one land sits in its own water fading into the other land's sea.
// The two coasts never meet: brushPlanMovesForeignCoast refuses a stroke that would.
inline constexpr int kBrushCellTiles = 16;
// The 4 x 4 and 8 x 8 brushes lay their corners every 8 tiles and use 8 x 8 tile pieces, so a
// coast or blend can turn twice as often. Those pieces are halved copies of the 16 x 16 ones,
// filed in folders named like theirs with -8x8 on the end, such as CoastTiles-8x8. Cliffs, coastal
// cliffs, slopes, ridges and rivers have no 8 x 8 pieces, so those strokes stay 16 x 16.
inline constexpr int kFineCellTiles = 8;

// The tile size of the pieces in a section folder, and the folder's name without the -8x8
// ending: CoastTiles-8x8 holds 8 x 8 pieces of the CoastTiles kind, and CoastTiles holds 16 x 16.
inline int brushPieceFolderTiles(std::string_view folder, std::string& kind) {
    constexpr std::string_view ending = "-8x8";
    if (folder.size() > ending.size()) {
        const std::string_view tail = folder.substr(folder.size() - ending.size());
        bool same = true;
        for (std::size_t i = 0; i < ending.size() && same; ++i) {
            same = std::tolower(static_cast<unsigned char>(tail[i])) == ending[i];
        }
        if (same) {
            kind = std::string(folder.substr(0, folder.size() - ending.size()));
            return kFineCellTiles;
        }
    }
    kind = std::string(folder);
    return kBrushCellTiles;
}

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

inline const std::array<BrushTransitionRule, 35>& brushTransitionRules() {
    static const std::array<BrushTransitionRule, 35> rules = {{
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
        // The graph applies this only to named Desert pairs at Base-80 and Base-85.
        {80, 85, TransitionStyle::WorldBlend},
        {78, 86, TransitionStyle::WorldBlend},
        // The graph applies this only to Archipelago beside Desert.
        {78, 80, TransitionStyle::WorldBlend},
        // Same height. The graph applies this to named Base-85 world pairs.
        {85, 85, TransitionStyle::WorldBlend},
        // The graph applies this only to Lush beside Archipelago.
        {77, 78, TransitionStyle::WorldBlend},
        // The graph applies this only to Lush beside Lava.
        {77, 86, TransitionStyle::WorldBlend},
        // The graph applies this only to Lush beside named Base-85 lands.
        {77, 85, TransitionStyle::WorldBlend},
        // The graph applies this only to Lush beside Desert.
        {77, 80, TransitionStyle::WorldBlend},
        // Water beside water. The graph applies this only to Mars, Crystal, Acid, Archipelago or Desert
        // Base-0 beside the blue water of GreenWorld, Ice or Metal Base-0, to Mars, Crystal or Acid
        // Base-0 beside the Archipelago and Desert water, to Crystal or Acid Base-0 beside Mars Base-0,
        // and to Crystal Base-0 beside Acid Base-0.
        {0, 0, TransitionStyle::WorldBlend},
        // The graph applies this only to the blue water, the Archipelago and Desert water, Mars, Crystal
        // or Acid water beside Lush Base-27, whose water stands higher.
        {0, 27, TransitionStyle::WorldBlend},
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

// GreenWorld, Ice and Metal draw the same blue water, and Archipelago and Desert share one water
// too. Two lands of one water family read and blend as one water.
inline bool blueWaterWorld(std::string_view world) {
    return world == "Green" || world == "Ice" || world == "Metal";
}
// Archipelago and Desert water, which the two lands share.
inline bool sandWaterWorld(std::string_view world) {
    return world == "Archi" || world == "Desert";
}
inline bool sameWaterFamily(std::string_view a, std::string_view b) {
    return a == b || (blueWaterWorld(a) && blueWaterWorld(b)) || (sandWaterWorld(a) && sandWaterWorld(b));
}
// The waters that fade into the blue water with a Blend-WaterGreen piece. Lush's Base-27 water
// does too, through its own rule, since it stands higher.
inline bool waterBlendsWithBlue(std::string_view world) {
    return world == "Mars" || world == "Crystal" || world == "Archi" || world == "Desert" || world == "Acid";
}
// The waters that fade into Mars water with a Blend-WaterMars piece. Lush's Base-27 water does
// too, through its own rule.
inline bool waterBlendsWithMars(std::string_view world) {
    return world == "Crystal" || world == "Acid";
}
// The waters that fade into the Archipelago and Desert water with a Blend-WaterDesert piece. Lush's
// Base-27 water does too, through its own rule.
inline bool waterBlendsWithSand(std::string_view world) {
    return world == "Mars" || world == "Crystal" || world == "Acid";
}

// The paints from lowest to highest and which of them are joined by pieces. A
// paint is called a level here, numbered by its place in that order.
class BrushGraph {
public:
    BrushGraph() = default;

    explicit BrushGraph(const std::vector<int>& paintHeights) : BrushGraph(paintHeights, {}) {}

    // worlds names each paint's land, such as "Green" or "Mars", so two paints at
    // the same height can still pick different blend pieces. blendsOnlyAcrossWorlds
    // keeps two different lands from joining through anything but a blend. The Tile
    // Brush leaves it off: it only ever holds one land's bases and the other lands'
    // ground. The random map generator holds every base of several lands, where
    // GreenWorld's shallows would otherwise meet Desert's sand through Desert's own
    // shallow coast, a pair no tileset draws.
    //
    // brushWorld names the Tile Brush's own land. Two paints of another land only join through
    // that land's own pieces, which the brush does not hold, so they are left unjoined: the Mars
    // brush has GreenWorld Base-0 and Base-85 but no GreenWorld coast. Empty leaves every pair in.
    BrushGraph(const std::vector<int>& paintHeights, const std::vector<std::string>& worlds, bool blendsOnlyAcrossWorlds = false,
        std::string_view brushWorld = {})
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
                        const std::string& wa = worlds_[static_cast<std::size_t>(a)];
                        const std::string& wb = worlds_[static_cast<std::size_t>(b)];
                        const auto namedPair = [&](const char* x, const char* y) {
                            return (wa == x && wb == y) || (wa == y && wb == x);
                        };
                        const bool named = !wa.empty() && !wb.empty();
                        const bool differentWorld = named && wa != wb;
                        if (blendsOnlyAcrossWorlds && differentWorld && rule.style != TransitionStyle::WorldBlend) {
                            continue;
                        }
                        if (!brushWorld.empty() && named && wa == wb && wa != brushWorld && rule.style != TransitionStyle::WorldBlend) {
                            continue;
                        }
                        // Water only meets another land's water through a water blend.
                        const bool foreignWater = rule.lowerHeight == 0 && differentWorld && rule.style != TransitionStyle::WorldBlend;
                        const bool same0 = rule.lowerHeight == 0 && rule.upperHeight == 0 && named
                            && (((blueWaterWorld(wa) != blueWaterWorld(wb)) && (blueWaterWorld(wa) ? waterBlendsWithBlue(wb) : waterBlendsWithBlue(wa)))
                                || ((sandWaterWorld(wa) != sandWaterWorld(wb)) && (sandWaterWorld(wa) ? waterBlendsWithSand(wb) : waterBlendsWithSand(wa)))
                                || ((wa == "Mars") != (wb == "Mars") && (wa == "Mars" ? waterBlendsWithMars(wb) : waterBlendsWithMars(wa)))
                                || namedPair("Crystal", "Acid"));
                        // Ordered by height: the blue, sand or Mars water is the Base-0 side and Lush the Base-27 side.
                        const bool lushWater = rule.lowerHeight == 0 && rule.upperHeight == 27
                            && (blueWaterWorld(wa) || sandWaterWorld(wa) || wa == "Mars" || wa == "Crystal" || wa == "Acid") && wb == "Lush";
                        const bool foreignLand = through85 && rule.lowerHeight == 0 && rule.upperHeight == 78 && differentWorld;
                        const bool foreignShallow = rule.lowerHeight == 68 && rule.upperHeight == 85 && differentWorld;
                        const bool same85 = rule.lowerHeight == 85 && rule.upperHeight == 85
                            && (namedPair("Green", "Acid") || namedPair("Ice", "Mars") || namedPair("Ice", "Green")
                                || namedPair("Green", "Mars") || namedPair("Green", "Metal") || namedPair("Acid", "Crystal")
                                || namedPair("Green", "Crystal") || namedPair("Acid", "Mars") || namedPair("Ice", "Acid")
                                || namedPair("Acid", "Metal") || namedPair("Mars", "Crystal") || namedPair("Mars", "Metal")
                                || namedPair("Ice", "Crystal") || namedPair("Ice", "Metal") || namedPair("Crystal", "Metal"));
                        const bool desert85 = rule.lowerHeight == 80 && rule.upperHeight == 85
                            && (namedPair("Acid", "Desert") || namedPair("Crystal", "Desert") || namedPair("Green", "Desert")
                                || namedPair("Ice", "Desert") || namedPair("Mars", "Desert") || namedPair("Metal", "Desert"));
                        const bool archiDesert = rule.lowerHeight == 78 && rule.upperHeight == 80 && namedPair("Archi", "Desert");
                        // A land keeps its own coast. Another land's Base-85 beside the water, such as
                        // GreenWorld beside Mars water or Ice water, goes through that land's own ground.
                        const bool foreignIceCoast = rule.lowerHeight == 0 && rule.upperHeight == 85 && differentWorld;
                        // Desert's own coast from Base-0 stays, so Acid or Crystal beside Desert water
                        // does not take that coast, and Acid or Crystal water does not take Desert's Base-80 coast.
                        const bool foreignDesertCoast = rule.lowerHeight == 0 && rule.upperHeight == 80 && differentWorld;
                        // Cliffs stay inside one land: another land's Base-85 beside Lush Base-255 or
                        // GreenWorld Base-219 goes through that land's own ground.
                        const bool foreignIceCliff = differentWorld && rule.style == TransitionStyle::Cliff;
                        const bool archiLush = rule.lowerHeight == 77 && rule.upperHeight == 78 && namedPair("Lush", "Archi");
                        const bool lushLava = rule.lowerHeight == 77 && rule.upperHeight == 86 && namedPair("Lush", "Lava");
                        const bool lush85 = rule.lowerHeight == 77 && rule.upperHeight == 85
                            && (namedPair("Lush", "Ice") || namedPair("Lush", "Green") || namedPair("Lush", "Mars")
                                || namedPair("Lush", "Acid") || namedPair("Lush", "Crystal") || namedPair("Lush", "Metal"));
                        const bool lushDesert = rule.lowerHeight == 77 && rule.upperHeight == 80 && namedPair("Lush", "Desert");
                        if (foreignLand || foreignShallow || foreignIceCoast || foreignDesertCoast || foreignIceCliff || foreignWater
                            || (rule.lowerHeight == rule.upperHeight && !same85 && !same0)
                            || (rule.lowerHeight == 0 && rule.upperHeight == 0 && !same0)
                            || (rule.lowerHeight == 0 && rule.upperHeight == 27 && !lushWater)
                            || (rule.lowerHeight == 77 && rule.upperHeight == 78 && !archiLush)
                            || (rule.lowerHeight == 77 && rule.upperHeight == 86 && !lushLava)
                            || (rule.lowerHeight == 77 && rule.upperHeight == 85 && !lush85)
                            || (rule.lowerHeight == 80 && rule.upperHeight == 85 && !desert85)
                            || (rule.lowerHeight == 78 && rule.upperHeight == 80 && !archiDesert)
                            || (rule.lowerHeight == 77 && rule.upperHeight == 80 && !lushDesert)) {
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

// Lava, GreenWorld and Mars in one cell. Grass filling a side names Mars
// (Blend-Tri-Grass-T-Mars-L). Mars filling a side names Grass
// (Blend-Tri-Mars-T-Grass-L). Lava filling a side names Grass and ends in
// -Mars (Blend-Tri-Lava-T-Grass-L-Mars), so it does not match the Ice Grass
// piece, the Ice Mars piece, or the Green Desert piece. Every name has Grass
// and Mars in it.
inline std::string triGreenMarsName(const BrushGraph& graph, const std::array<int, 4>& corners) {
    int greenLevel = -1;
    int marsLevel = -1;
    int lavaLevel = -1;
    for (const int level : corners) {
        if (graph.world(level) == "Green" && graph.height(level) == 85) {
            greenLevel = level;
        } else if (graph.world(level) == "Mars" && graph.height(level) == 85) {
            marsLevel = level;
        } else if (graph.world(level) == "Lava" && graph.height(level) == 86) {
            lavaLevel = level;
        } else {
            return {};
        }
    }
    if (greenLevel < 0 || marsLevel < 0 || lavaLevel < 0) {
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
    const Land lands[3] = {{lavaLevel, "Lava"}, {greenLevel, "Grass"}, {marsLevel, "Mars"}};
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
        const char* suffix = fill.level == lavaLevel ? "-Mars" : "";
        return std::string("Blend-Tri-") + fill.name + "-" + side + "-" + named.name + "-" + where + suffix;
    }
    return {};
}

// Lava, Ice and Lush in one cell. Ice filling a side names Lush
// (Blend-Tri-Ice-T-Lush-L). Lush filling a side names Ice
// (Blend-Tri-Lush-T-Ice-L). Lava filling a side names Ice and ends in
// -Lush (Blend-Tri-Lava-T-Ice-L-Lush), so it does not match the Ice Grass
// piece, the Ice Desert piece, or the Lush GreenWorld piece. Every name has
// Ice and Lush in it.
inline std::string triIceLushName(const BrushGraph& graph, const std::array<int, 4>& corners) {
    int iceLevel = -1;
    int lushLevel = -1;
    int lavaLevel = -1;
    for (const int level : corners) {
        if (graph.world(level) == "Ice" && graph.height(level) == 85) {
            iceLevel = level;
        } else if (graph.world(level) == "Lush" && graph.height(level) == 77) {
            lushLevel = level;
        } else if (graph.world(level) == "Lava" && graph.height(level) == 86) {
            lavaLevel = level;
        } else {
            return {};
        }
    }
    if (iceLevel < 0 || lushLevel < 0 || lavaLevel < 0) {
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
    const Land lands[3] = {{lavaLevel, "Lava"}, {iceLevel, "Ice"}, {lushLevel, "Lush"}};
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
        const char* suffix = fill.level == lavaLevel ? "-Lush" : "";
        return std::string("Blend-Tri-") + fill.name + "-" + side + "-" + named.name + "-" + where + suffix;
    }
    return {};
}

// Lava, Ice and Crystal in one cell. Ice filling a side names Crystal
// (Blend-Tri-Ice-T-Crystal-L). Crystal filling a side names Ice
// (Blend-Tri-Crystal-T-Ice-L). Lava filling a side names Ice and ends in
// -Crystal (Blend-Tri-Lava-T-Ice-L-Crystal), so it does not match the Ice Lush
// lava piece, the Ice Desert lava piece, or the Crystal Archipelago lava piece.
// Every name has Ice and Crystal in it.
inline std::string triIceCrystalName(const BrushGraph& graph, const std::array<int, 4>& corners) {
    int iceLevel = -1;
    int crystalLevel = -1;
    int lavaLevel = -1;
    for (const int level : corners) {
        if (graph.world(level) == "Ice" && graph.height(level) == 85) {
            iceLevel = level;
        } else if (graph.world(level) == "Crystal" && graph.height(level) == 85) {
            crystalLevel = level;
        } else if (graph.world(level) == "Lava" && graph.height(level) == 86) {
            lavaLevel = level;
        } else {
            return {};
        }
    }
    if (iceLevel < 0 || crystalLevel < 0 || lavaLevel < 0) {
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
    const Land lands[3] = {{lavaLevel, "Lava"}, {iceLevel, "Ice"}, {crystalLevel, "Crystal"}};
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
        const char* suffix = fill.level == lavaLevel ? "-Crystal" : "";
        return std::string("Blend-Tri-") + fill.name + "-" + side + "-" + named.name + "-" + where + suffix;
    }
    return {};
}

// Lava, Ice and Acid in one cell. Ice filling a side names Acid
// (Blend-Tri-Ice-T-Acid-L). Acid filling a side names Ice
// (Blend-Tri-Acid-T-Ice-L). Lava filling a side names Ice and ends in
// -Acid (Blend-Tri-Lava-T-Ice-L-Acid), so it does not match the Ice Crystal
// lava piece, the Ice Lush lava piece, or the Acid Crystal lava piece. Every
// name has Ice and Acid in it.
inline std::string triIceAcidName(const BrushGraph& graph, const std::array<int, 4>& corners) {
    int iceLevel = -1;
    int acidLevel = -1;
    int lavaLevel = -1;
    for (const int level : corners) {
        if (graph.world(level) == "Ice" && graph.height(level) == 85) {
            iceLevel = level;
        } else if (graph.world(level) == "Acid" && graph.height(level) == 85) {
            acidLevel = level;
        } else if (graph.world(level) == "Lava" && graph.height(level) == 86) {
            lavaLevel = level;
        } else {
            return {};
        }
    }
    if (iceLevel < 0 || acidLevel < 0 || lavaLevel < 0) {
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
    const Land lands[3] = {{lavaLevel, "Lava"}, {iceLevel, "Ice"}, {acidLevel, "Acid"}};
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
        const char* suffix = fill.level == lavaLevel ? "-Acid" : "";
        return std::string("Blend-Tri-") + fill.name + "-" + side + "-" + named.name + "-" + where + suffix;
    }
    return {};
}

// Lava, Desert and Lush in one cell. Lush filling a side names Desert
// (Blend-Tri-Lush-T-Desert-L). Desert filling a side names Lush
// (Blend-Tri-Desert-T-Lush-L). Lava filling a side names Lush and ends in
// -Desert (Blend-Tri-Lava-T-Lush-L-Desert), so it does not match the Lush
// GreenWorld piece, the Lush Mars piece, or the Green Desert piece. Every
// name has Lush and Desert in it.
inline std::string triDesertLushName(const BrushGraph& graph, const std::array<int, 4>& corners) {
    int desertLevel = -1;
    int lushLevel = -1;
    int lavaLevel = -1;
    for (const int level : corners) {
        if (graph.world(level) == "Desert" && graph.height(level) == 80) {
            desertLevel = level;
        } else if (graph.world(level) == "Lush" && graph.height(level) == 77) {
            lushLevel = level;
        } else if (graph.world(level) == "Lava" && graph.height(level) == 86) {
            lavaLevel = level;
        } else {
            return {};
        }
    }
    if (desertLevel < 0 || lushLevel < 0 || lavaLevel < 0) {
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
    const Land lands[3] = {{lavaLevel, "Lava"}, {lushLevel, "Lush"}, {desertLevel, "Desert"}};
    for (const Land& fill : lands) {
        const char* side = sideOf(maskOf(fill.level));
        if (side == nullptr) {
            continue;
        }
        const Land& named = fill.level == lushLevel ? lands[2] : lands[1];
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

// Lava, GreenWorld and Crystal in one cell. Grass filling a side names Crystal
// (Blend-Tri-Grass-T-Crystal-L). Crystal filling a side names Grass
// (Blend-Tri-Crystal-T-Grass-L). Lava filling a side names Grass and ends in
// -Crystal (Blend-Tri-Lava-T-Grass-L-Crystal), so it does not match the Ice
// Grass piece, the Green Mars piece, or the Crystal Archipelago piece. Every
// name has Grass and Crystal in it.
inline std::string triGreenCrystalName(const BrushGraph& graph, const std::array<int, 4>& corners) {
    int greenLevel = -1;
    int crystalLevel = -1;
    int lavaLevel = -1;
    for (const int level : corners) {
        if (graph.world(level) == "Green" && graph.height(level) == 85) {
            greenLevel = level;
        } else if (graph.world(level) == "Crystal" && graph.height(level) == 85) {
            crystalLevel = level;
        } else if (graph.world(level) == "Lava" && graph.height(level) == 86) {
            lavaLevel = level;
        } else {
            return {};
        }
    }
    if (greenLevel < 0 || crystalLevel < 0 || lavaLevel < 0) {
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
    const Land lands[3] = {{lavaLevel, "Lava"}, {greenLevel, "Grass"}, {crystalLevel, "Crystal"}};
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
        const char* suffix = fill.level == lavaLevel ? "-Crystal" : "";
        return std::string("Blend-Tri-") + fill.name + "-" + side + "-" + named.name + "-" + where + suffix;
    }
    return {};
}

// Lava, Archipelago and GreenWorld in one cell. Grass filling a side names Archipelago
// (Blend-Tri-Grass-T-Archi-L). Archipelago filling a side names Grass
// (Blend-Tri-Archi-T-Grass-L). Lava filling a side names Grass and ends in
// -Archi (Blend-Tri-Lava-T-Grass-L-Archi), so it does not match the Ice Grass
// piece, the Ice Archipelago piece, or the Archipelago Desert piece. Every
// name has Grass and Archi in it.
inline std::string triArchiGreenName(const BrushGraph& graph, const std::array<int, 4>& corners) {
    int archiLevel = -1;
    int greenLevel = -1;
    int lavaLevel = -1;
    for (const int level : corners) {
        if (graph.world(level) == "Archi" && graph.height(level) == 78) {
            archiLevel = level;
        } else if (graph.world(level) == "Green" && graph.height(level) == 85) {
            greenLevel = level;
        } else if (graph.world(level) == "Lava" && graph.height(level) == 86) {
            lavaLevel = level;
        } else {
            return {};
        }
    }
    if (archiLevel < 0 || greenLevel < 0 || lavaLevel < 0) {
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
    const Land lands[3] = {{lavaLevel, "Lava"}, {greenLevel, "Grass"}, {archiLevel, "Archi"}};
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
        const char* suffix = fill.level == lavaLevel ? "-Archi" : "";
        return std::string("Blend-Tri-") + fill.name + "-" + side + "-" + named.name + "-" + where + suffix;
    }
    return {};
}

// Lava, Archipelago and Lush in one cell. Lush filling a side names Archipelago
// (Blend-Tri-Lush-T-Archi-L). Archipelago filling a side names Lush
// (Blend-Tri-Archi-T-Lush-L). Lava filling a side names Lush and ends in
// -Archi (Blend-Tri-Lava-T-Lush-L-Archi), so it does not match the Lush
// GreenWorld piece, the Lush Mars piece, or the Archipelago GreenWorld piece.
// Every name has Archi and Lush in it.
inline std::string triArchiLushName(const BrushGraph& graph, const std::array<int, 4>& corners) {
    int archiLevel = -1;
    int lushLevel = -1;
    int lavaLevel = -1;
    for (const int level : corners) {
        if (graph.world(level) == "Archi" && graph.height(level) == 78) {
            archiLevel = level;
        } else if (graph.world(level) == "Lush" && graph.height(level) == 77) {
            lushLevel = level;
        } else if (graph.world(level) == "Lava" && graph.height(level) == 86) {
            lavaLevel = level;
        } else {
            return {};
        }
    }
    if (archiLevel < 0 || lushLevel < 0 || lavaLevel < 0) {
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
    const Land lands[3] = {{lavaLevel, "Lava"}, {lushLevel, "Lush"}, {archiLevel, "Archi"}};
    for (const Land& fill : lands) {
        const char* side = sideOf(maskOf(fill.level));
        if (side == nullptr) {
            continue;
        }
        const Land& named = fill.level == lushLevel ? lands[2] : lands[1];
        const int namedMask = maskOf(named.level);
        if (cornerCount(namedMask) != 1) {
            return {};
        }
        const char* where = side[0] == 'T' || side[0] == 'B'
            ? ((namedMask & (kCornerTopRight | kCornerBottomRight)) != 0 ? "R" : "L")
            : ((namedMask & (kCornerBottomLeft | kCornerBottomRight)) != 0 ? "B" : "T");
        const char* suffix = fill.level == lavaLevel ? "-Archi" : "";
        return std::string("Blend-Tri-") + fill.name + "-" + side + "-" + named.name + "-" + where + suffix;
    }
    return {};
}

// Lava, Crystal and Lush in one cell. Lush filling a side names Crystal
// (Blend-Tri-Lush-T-Crystal-L). Crystal filling a side names Lush
// (Blend-Tri-Crystal-T-Lush-L). Lava filling a side names Lush and ends in
// -Crystal (Blend-Tri-Lava-T-Lush-L-Crystal), so it does not match the Lush
// GreenWorld piece, the Lush Mars piece, the Lush Archipelago piece, or the
// Crystal Archipelago lava piece. Every name has Crystal and Lush in it.
inline std::string triLushCrystalName(const BrushGraph& graph, const std::array<int, 4>& corners) {
    int crystalLevel = -1;
    int lushLevel = -1;
    int lavaLevel = -1;
    for (const int level : corners) {
        if (graph.world(level) == "Crystal" && graph.height(level) == 85) {
            crystalLevel = level;
        } else if (graph.world(level) == "Lush" && graph.height(level) == 77) {
            lushLevel = level;
        } else if (graph.world(level) == "Lava" && graph.height(level) == 86) {
            lavaLevel = level;
        } else {
            return {};
        }
    }
    if (crystalLevel < 0 || lushLevel < 0 || lavaLevel < 0) {
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
    const Land lands[3] = {{lavaLevel, "Lava"}, {lushLevel, "Lush"}, {crystalLevel, "Crystal"}};
    for (const Land& fill : lands) {
        const char* side = sideOf(maskOf(fill.level));
        if (side == nullptr) {
            continue;
        }
        const Land& named = fill.level == lushLevel ? lands[2] : lands[1];
        const int namedMask = maskOf(named.level);
        if (cornerCount(namedMask) != 1) {
            return {};
        }
        const char* where = side[0] == 'T' || side[0] == 'B'
            ? ((namedMask & (kCornerTopRight | kCornerBottomRight)) != 0 ? "R" : "L")
            : ((namedMask & (kCornerBottomLeft | kCornerBottomRight)) != 0 ? "B" : "T");
        const char* suffix = fill.level == lavaLevel ? "-Crystal" : "";
        return std::string("Blend-Tri-") + fill.name + "-" + side + "-" + named.name + "-" + where + suffix;
    }
    return {};
}

// Lava, Desert and Mars in one cell. Desert filling a side names Mars
// (Blend-Tri-Desert-T-Mars-L). Mars filling a side names Desert
// (Blend-Tri-Mars-T-Desert-L). Lava filling a side names Desert and ends in
// -Mars (Blend-Tri-Lava-T-Desert-L-Mars), so it does not match the Acid Desert
// lava piece, the Ice Mars piece, or the Green Mars piece. Every name has
// Desert and Mars in it.
inline std::string triDesertMarsName(const BrushGraph& graph, const std::array<int, 4>& corners) {
    int desertLevel = -1;
    int marsLevel = -1;
    int lavaLevel = -1;
    for (const int level : corners) {
        if (graph.world(level) == "Desert" && graph.height(level) == 80) {
            desertLevel = level;
        } else if (graph.world(level) == "Mars" && graph.height(level) == 85) {
            marsLevel = level;
        } else if (graph.world(level) == "Lava" && graph.height(level) == 86) {
            lavaLevel = level;
        } else {
            return {};
        }
    }
    if (desertLevel < 0 || marsLevel < 0 || lavaLevel < 0) {
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
    const Land lands[3] = {{lavaLevel, "Lava"}, {desertLevel, "Desert"}, {marsLevel, "Mars"}};
    for (const Land& fill : lands) {
        const char* side = sideOf(maskOf(fill.level));
        if (side == nullptr) {
            continue;
        }
        const Land& named = fill.level == desertLevel ? lands[2] : lands[1];
        const int namedMask = maskOf(named.level);
        if (cornerCount(namedMask) != 1) {
            return {};
        }
        const char* where = side[0] == 'T' || side[0] == 'B'
            ? ((namedMask & (kCornerTopRight | kCornerBottomRight)) != 0 ? "R" : "L")
            : ((namedMask & (kCornerBottomLeft | kCornerBottomRight)) != 0 ? "B" : "T");
        const char* suffix = fill.level == lavaLevel ? "-Mars" : "";
        return std::string("Blend-Tri-") + fill.name + "-" + side + "-" + named.name + "-" + where + suffix;
    }
    return {};
}

// Lava, Acid and Lush in one cell. Lush filling a side names Acid
// (Blend-Tri-Lush-T-Acid-L). Acid filling a side names Lush
// (Blend-Tri-Acid-T-Lush-L). Lava filling a side names Lush and ends in
// -Acid (Blend-Tri-Lava-T-Lush-L-Acid), so it does not match the Lush
// GreenWorld piece, the Lush Mars piece, the Acid Crystal lava piece, or the
// Ice Acid lava piece. Every name has Acid and Lush in it.
inline std::string triAcidLushName(const BrushGraph& graph, const std::array<int, 4>& corners) {
    int acidLevel = -1;
    int lushLevel = -1;
    int lavaLevel = -1;
    for (const int level : corners) {
        if (graph.world(level) == "Acid" && graph.height(level) == 85) {
            acidLevel = level;
        } else if (graph.world(level) == "Lush" && graph.height(level) == 77) {
            lushLevel = level;
        } else if (graph.world(level) == "Lava" && graph.height(level) == 86) {
            lavaLevel = level;
        } else {
            return {};
        }
    }
    if (acidLevel < 0 || lushLevel < 0 || lavaLevel < 0) {
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
    const Land lands[3] = {{lavaLevel, "Lava"}, {lushLevel, "Lush"}, {acidLevel, "Acid"}};
    for (const Land& fill : lands) {
        const char* side = sideOf(maskOf(fill.level));
        if (side == nullptr) {
            continue;
        }
        const Land& named = fill.level == lushLevel ? lands[2] : lands[1];
        const int namedMask = maskOf(named.level);
        if (cornerCount(namedMask) != 1) {
            return {};
        }
        const char* where = side[0] == 'T' || side[0] == 'B'
            ? ((namedMask & (kCornerTopRight | kCornerBottomRight)) != 0 ? "R" : "L")
            : ((namedMask & (kCornerBottomLeft | kCornerBottomRight)) != 0 ? "B" : "T");
        const char* suffix = fill.level == lavaLevel ? "-Acid" : "";
        return std::string("Blend-Tri-") + fill.name + "-" + side + "-" + named.name + "-" + where + suffix;
    }
    return {};
}

// Lava, Acid and Mars in one cell. Acid filling a side names Mars
// (Blend-Tri-Acid-T-Mars-L). Mars filling a side names Acid
// (Blend-Tri-Mars-T-Acid-L). Lava filling a side names Acid and ends in
// -Mars (Blend-Tri-Lava-T-Acid-L-Mars), so it does not match the Acid Crystal
// lava piece, the Ice Mars piece, or the Green Mars piece. Every name has
// Acid and Mars in it.
inline std::string triAcidMarsName(const BrushGraph& graph, const std::array<int, 4>& corners) {
    int acidLevel = -1;
    int marsLevel = -1;
    int lavaLevel = -1;
    for (const int level : corners) {
        if (graph.world(level) == "Acid" && graph.height(level) == 85) {
            acidLevel = level;
        } else if (graph.world(level) == "Mars" && graph.height(level) == 85) {
            marsLevel = level;
        } else if (graph.world(level) == "Lava" && graph.height(level) == 86) {
            lavaLevel = level;
        } else {
            return {};
        }
    }
    if (acidLevel < 0 || marsLevel < 0 || lavaLevel < 0) {
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
    const Land lands[3] = {{lavaLevel, "Lava"}, {acidLevel, "Acid"}, {marsLevel, "Mars"}};
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
        const char* suffix = fill.level == lavaLevel ? "-Mars" : "";
        return std::string("Blend-Tri-") + fill.name + "-" + side + "-" + named.name + "-" + where + suffix;
    }
    return {};
}

// Lava, Archipelago and Mars in one cell. Archipelago filling a side names Mars
// (Blend-Tri-Archi-T-Mars-L). Mars filling a side names Archipelago
// (Blend-Tri-Mars-T-Archi-L). Lava filling a side names Archipelago and ends in
// -Mars (Blend-Tri-Lava-T-Archi-L-Mars), so it does not match the Ice
// Archipelago lava piece, the Ice Mars piece, or the Green Mars piece. Every
// name has Archi and Mars in it.
inline std::string triArchiMarsName(const BrushGraph& graph, const std::array<int, 4>& corners) {
    int archiLevel = -1;
    int marsLevel = -1;
    int lavaLevel = -1;
    for (const int level : corners) {
        if (graph.world(level) == "Archi" && graph.height(level) == 78) {
            archiLevel = level;
        } else if (graph.world(level) == "Mars" && graph.height(level) == 85) {
            marsLevel = level;
        } else if (graph.world(level) == "Lava" && graph.height(level) == 86) {
            lavaLevel = level;
        } else {
            return {};
        }
    }
    if (archiLevel < 0 || marsLevel < 0 || lavaLevel < 0) {
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
    const Land lands[3] = {{lavaLevel, "Lava"}, {archiLevel, "Archi"}, {marsLevel, "Mars"}};
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
        const char* suffix = fill.level == lavaLevel ? "-Mars" : "";
        return std::string("Blend-Tri-") + fill.name + "-" + side + "-" + named.name + "-" + where + suffix;
    }
    return {};
}

// Lava, Crystal and Mars in one cell. Crystal filling a side names Mars
// (Blend-Tri-Crystal-T-Mars-L). Mars filling a side names Crystal
// (Blend-Tri-Mars-T-Crystal-L). Lava filling a side names Crystal and ends in
// -Mars (Blend-Tri-Lava-T-Crystal-L-Mars), so it does not match the Crystal
// Archipelago lava piece, the Ice Mars piece, or the Green Mars piece. Every
// name has Crystal and Mars in it.
inline std::string triCrystalMarsName(const BrushGraph& graph, const std::array<int, 4>& corners) {
    int crystalLevel = -1;
    int marsLevel = -1;
    int lavaLevel = -1;
    for (const int level : corners) {
        if (graph.world(level) == "Crystal" && graph.height(level) == 85) {
            crystalLevel = level;
        } else if (graph.world(level) == "Mars" && graph.height(level) == 85) {
            marsLevel = level;
        } else if (graph.world(level) == "Lava" && graph.height(level) == 86) {
            lavaLevel = level;
        } else {
            return {};
        }
    }
    if (crystalLevel < 0 || marsLevel < 0 || lavaLevel < 0) {
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
    const Land lands[3] = {{lavaLevel, "Lava"}, {crystalLevel, "Crystal"}, {marsLevel, "Mars"}};
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
        const char* suffix = fill.level == lavaLevel ? "-Mars" : "";
        return std::string("Blend-Tri-") + fill.name + "-" + side + "-" + named.name + "-" + where + suffix;
    }
    return {};
}

// Lava, GreenWorld and Metal in one cell. GreenWorld filling a side names Metal
// (Blend-Tri-Grass-T-Metal-L). Metal filling a side names Grass
// (Blend-Tri-Metal-T-Grass-L). Lava filling a side names Grass and ends in
// -Metal (Blend-Tri-Lava-T-Grass-L-Metal), so it does not match the Ice
// GreenWorld lava piece, the Green Mars piece, or the Green Crystal piece.
// Every name has Grass and Metal in it.
inline std::string triGreenMetalName(const BrushGraph& graph, const std::array<int, 4>& corners) {
    int greenLevel = -1;
    int metalLevel = -1;
    int lavaLevel = -1;
    for (const int level : corners) {
        if (graph.world(level) == "Green" && graph.height(level) == 85) {
            greenLevel = level;
        } else if (graph.world(level) == "Metal" && graph.height(level) == 85) {
            metalLevel = level;
        } else if (graph.world(level) == "Lava" && graph.height(level) == 86) {
            lavaLevel = level;
        } else {
            return {};
        }
    }
    if (greenLevel < 0 || metalLevel < 0 || lavaLevel < 0) {
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
    const Land lands[3] = {{lavaLevel, "Lava"}, {greenLevel, "Grass"}, {metalLevel, "Metal"}};
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
        const char* suffix = fill.level == lavaLevel ? "-Metal" : "";
        return std::string("Blend-Tri-") + fill.name + "-" + side + "-" + named.name + "-" + where + suffix;
    }
    return {};
}

// Lava, Desert and Metal in one cell. Desert filling a side names Metal
// (Blend-Tri-Desert-T-Metal-L). Metal filling a side names Desert
// (Blend-Tri-Metal-T-Desert-L). Lava filling a side names Desert and ends in
// -Metal (Blend-Tri-Lava-T-Desert-L-Metal), so it does not match the Acid
// Desert lava piece, the Green Metal lava piece, or the Desert Mars piece.
// Every name has Desert and Metal in it.
inline std::string triDesertMetalName(const BrushGraph& graph, const std::array<int, 4>& corners) {
    int desertLevel = -1;
    int metalLevel = -1;
    int lavaLevel = -1;
    for (const int level : corners) {
        if (graph.world(level) == "Desert" && graph.height(level) == 80) {
            desertLevel = level;
        } else if (graph.world(level) == "Metal" && graph.height(level) == 85) {
            metalLevel = level;
        } else if (graph.world(level) == "Lava" && graph.height(level) == 86) {
            lavaLevel = level;
        } else {
            return {};
        }
    }
    if (desertLevel < 0 || metalLevel < 0 || lavaLevel < 0) {
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
    const Land lands[3] = {{lavaLevel, "Lava"}, {desertLevel, "Desert"}, {metalLevel, "Metal"}};
    for (const Land& fill : lands) {
        const char* side = sideOf(maskOf(fill.level));
        if (side == nullptr) {
            continue;
        }
        const Land& named = fill.level == desertLevel ? lands[2] : lands[1];
        const int namedMask = maskOf(named.level);
        if (cornerCount(namedMask) != 1) {
            return {};
        }
        const char* where = side[0] == 'T' || side[0] == 'B'
            ? ((namedMask & (kCornerTopRight | kCornerBottomRight)) != 0 ? "R" : "L")
            : ((namedMask & (kCornerBottomLeft | kCornerBottomRight)) != 0 ? "B" : "T");
        const char* suffix = fill.level == lavaLevel ? "-Metal" : "";
        return std::string("Blend-Tri-") + fill.name + "-" + side + "-" + named.name + "-" + where + suffix;
    }
    return {};
}

// Lava, Archipelago and Metal in one cell. Archipelago filling a side names Metal
// (Blend-Tri-Archi-T-Metal-L). Metal filling a side names Archipelago
// (Blend-Tri-Metal-T-Archi-L). Lava filling a side names Archipelago and ends in
// -Metal (Blend-Tri-Lava-T-Archi-L-Metal), so it does not match the Ice
// Archipelago lava piece, the Green Metal lava piece, or the Archipelago Mars
// piece. Every name has Archi and Metal in it.
inline std::string triArchiMetalName(const BrushGraph& graph, const std::array<int, 4>& corners) {
    int archiLevel = -1;
    int metalLevel = -1;
    int lavaLevel = -1;
    for (const int level : corners) {
        if (graph.world(level) == "Archi" && graph.height(level) == 78) {
            archiLevel = level;
        } else if (graph.world(level) == "Metal" && graph.height(level) == 85) {
            metalLevel = level;
        } else if (graph.world(level) == "Lava" && graph.height(level) == 86) {
            lavaLevel = level;
        } else {
            return {};
        }
    }
    if (archiLevel < 0 || metalLevel < 0 || lavaLevel < 0) {
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
    const Land lands[3] = {{lavaLevel, "Lava"}, {archiLevel, "Archi"}, {metalLevel, "Metal"}};
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
        const char* suffix = fill.level == lavaLevel ? "-Metal" : "";
        return std::string("Blend-Tri-") + fill.name + "-" + side + "-" + named.name + "-" + where + suffix;
    }
    return {};
}

// Lava, Lush and Metal in one cell. Lush filling a side names Metal
// (Blend-Tri-Lush-T-Metal-L). Metal filling a side names Lush
// (Blend-Tri-Metal-T-Lush-L). Lava filling a side names Lush and ends in
// -Metal (Blend-Tri-Lava-T-Lush-L-Metal), so it does not match the Lush
// GreenWorld lava piece, the Lush Mars piece, or the Green Metal lava piece.
// Every name has Lush and Metal in it.
inline std::string triLushMetalName(const BrushGraph& graph, const std::array<int, 4>& corners) {
    int lushLevel = -1;
    int metalLevel = -1;
    int lavaLevel = -1;
    for (const int level : corners) {
        if (graph.world(level) == "Lush" && graph.height(level) == 77) {
            lushLevel = level;
        } else if (graph.world(level) == "Metal" && graph.height(level) == 85) {
            metalLevel = level;
        } else if (graph.world(level) == "Lava" && graph.height(level) == 86) {
            lavaLevel = level;
        } else {
            return {};
        }
    }
    if (lushLevel < 0 || metalLevel < 0 || lavaLevel < 0) {
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
    const Land lands[3] = {{lavaLevel, "Lava"}, {lushLevel, "Lush"}, {metalLevel, "Metal"}};
    for (const Land& fill : lands) {
        const char* side = sideOf(maskOf(fill.level));
        if (side == nullptr) {
            continue;
        }
        const Land& named = fill.level == lushLevel ? lands[2] : lands[1];
        const int namedMask = maskOf(named.level);
        if (cornerCount(namedMask) != 1) {
            return {};
        }
        const char* where = side[0] == 'T' || side[0] == 'B'
            ? ((namedMask & (kCornerTopRight | kCornerBottomRight)) != 0 ? "R" : "L")
            : ((namedMask & (kCornerBottomLeft | kCornerBottomRight)) != 0 ? "B" : "T");
        const char* suffix = fill.level == lavaLevel ? "-Metal" : "";
        return std::string("Blend-Tri-") + fill.name + "-" + side + "-" + named.name + "-" + where + suffix;
    }
    return {};
}

// Lava, Ice and Metal in one cell. Ice filling a side names Metal
// (Blend-Tri-Ice-T-Metal-L). Metal filling a side names Ice
// (Blend-Tri-Metal-T-Ice-L). Lava filling a side names Ice and ends in
// -Metal (Blend-Tri-Lava-T-Ice-L-Metal), so it does not match the Ice Lush
// lava piece, the Ice Crystal lava piece, or the Lush Metal lava piece.
// Every name has Ice and Metal in it.
inline std::string triIceMetalName(const BrushGraph& graph, const std::array<int, 4>& corners) {
    int iceLevel = -1;
    int metalLevel = -1;
    int lavaLevel = -1;
    for (const int level : corners) {
        if (graph.world(level) == "Ice" && graph.height(level) == 85) {
            iceLevel = level;
        } else if (graph.world(level) == "Metal" && graph.height(level) == 85) {
            metalLevel = level;
        } else if (graph.world(level) == "Lava" && graph.height(level) == 86) {
            lavaLevel = level;
        } else {
            return {};
        }
    }
    if (iceLevel < 0 || metalLevel < 0 || lavaLevel < 0) {
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
    const Land lands[3] = {{lavaLevel, "Lava"}, {iceLevel, "Ice"}, {metalLevel, "Metal"}};
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
        const char* suffix = fill.level == lavaLevel ? "-Metal" : "";
        return std::string("Blend-Tri-") + fill.name + "-" + side + "-" + named.name + "-" + where + suffix;
    }
    return {};
}

// Lava, Acid and Metal in one cell. Acid filling a side names Metal
// (Blend-Tri-Acid-T-Metal-L). Metal filling a side names Acid
// (Blend-Tri-Metal-T-Acid-L). Lava filling a side names Acid and ends in
// -Metal (Blend-Tri-Lava-T-Acid-L-Metal), so it does not match the Acid
// Crystal lava piece, the Acid Mars lava piece, or the Ice Metal lava piece.
// Every name has Acid and Metal in it.
inline std::string triAcidMetalName(const BrushGraph& graph, const std::array<int, 4>& corners) {
    int acidLevel = -1;
    int metalLevel = -1;
    int lavaLevel = -1;
    for (const int level : corners) {
        if (graph.world(level) == "Acid" && graph.height(level) == 85) {
            acidLevel = level;
        } else if (graph.world(level) == "Metal" && graph.height(level) == 85) {
            metalLevel = level;
        } else if (graph.world(level) == "Lava" && graph.height(level) == 86) {
            lavaLevel = level;
        } else {
            return {};
        }
    }
    if (acidLevel < 0 || metalLevel < 0 || lavaLevel < 0) {
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
    const Land lands[3] = {{lavaLevel, "Lava"}, {acidLevel, "Acid"}, {metalLevel, "Metal"}};
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
        const char* suffix = fill.level == lavaLevel ? "-Metal" : "";
        return std::string("Blend-Tri-") + fill.name + "-" + side + "-" + named.name + "-" + where + suffix;
    }
    return {};
}

// Lava, Crystal and Metal in one cell. Crystal filling a side names Metal
// (Blend-Tri-Crystal-T-Metal-L). Metal filling a side names Crystal
// (Blend-Tri-Metal-T-Crystal-L). Lava filling a side names Crystal and ends in
// -Metal (Blend-Tri-Lava-T-Crystal-L-Metal), so it does not match the Crystal
// Archipelago lava piece, the Crystal Mars lava piece, or the Acid Metal lava
// piece. Every name has Crystal and Metal in it.
inline std::string triCrystalMetalName(const BrushGraph& graph, const std::array<int, 4>& corners) {
    int crystalLevel = -1;
    int metalLevel = -1;
    int lavaLevel = -1;
    for (const int level : corners) {
        if (graph.world(level) == "Crystal" && graph.height(level) == 85) {
            crystalLevel = level;
        } else if (graph.world(level) == "Metal" && graph.height(level) == 85) {
            metalLevel = level;
        } else if (graph.world(level) == "Lava" && graph.height(level) == 86) {
            lavaLevel = level;
        } else {
            return {};
        }
    }
    if (crystalLevel < 0 || metalLevel < 0 || lavaLevel < 0) {
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
    const Land lands[3] = {{lavaLevel, "Lava"}, {crystalLevel, "Crystal"}, {metalLevel, "Metal"}};
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
        const char* suffix = fill.level == lavaLevel ? "-Metal" : "";
        return std::string("Blend-Tri-") + fill.name + "-" + side + "-" + named.name + "-" + where + suffix;
    }
    return {};
}

// Lava, Acid and Archipelago in one cell. Acid filling a side names Archipelago
// (Blend-Tri-Acid-T-Archi-L). Archipelago filling a side names Acid
// (Blend-Tri-Archi-T-Acid-L). Lava filling a side names Acid and ends in
// -Archi (Blend-Tri-Lava-T-Acid-L-Archi), so it does not match the Acid Crystal
// lava piece, the GreenWorld Acid piece, or the Ice Archipelago piece. Every
// name has Acid and Archi in it.
inline std::string triAcidArchiName(const BrushGraph& graph, const std::array<int, 4>& corners) {
    int acidLevel = -1;
    int archiLevel = -1;
    int lavaLevel = -1;
    for (const int level : corners) {
        if (graph.world(level) == "Acid" && graph.height(level) == 85) {
            acidLevel = level;
        } else if (graph.world(level) == "Archi" && graph.height(level) == 78) {
            archiLevel = level;
        } else if (graph.world(level) == "Lava" && graph.height(level) == 86) {
            lavaLevel = level;
        } else {
            return {};
        }
    }
    if (acidLevel < 0 || archiLevel < 0 || lavaLevel < 0) {
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
    const Land lands[3] = {{lavaLevel, "Lava"}, {acidLevel, "Acid"}, {archiLevel, "Archi"}};
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
        const char* suffix = fill.level == lavaLevel ? "-Archi" : "";
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
// GreenWorld, Ice and Lush in one cell, laid out the same way as triIceName. Every name ends in the
// land it does not otherwise give, so none matches a piece of another set:
//  - Ice filling a side names the grass: Blend-Tri-Ice-T-Grass-L-Lush has ice along the top, grass
//    bottom left and Lush bottom right.
//  - GreenWorld filling a side names the ice: Blend-Tri-Grass-T-Ice-L-Lush.
//  - Lush filling a side names the grass: Blend-Tri-Lush-T-Grass-L-Ice.
// The pivot land, GreenWorld unless given, fills or meets the other two: Archipelago, Desert and Lush use
// Lush as the pivot, named Lush, with Archipelago in Ice's place and Desert in Lush's:
// Blend-Tri-Archi-T-Lush-L-Desert has Archipelago along the top. Archipelago, Ice and Lush do the same
// with Ice in Desert's place: Blend-Tri-Archi-T-Lush-L-Ice.
// GreenWorld, Mars and Lush use the same names with Mars in place of Ice, and GreenWorld, Ice and
// Mars use them with Mars in place of Lush: Blend-Tri-Mars-T-Grass-L-Ice has Mars along the top.
// GreenWorld, Metal and Lush use Metal in place of Ice: Blend-Tri-Metal-T-Grass-L-Lush, and Crystal,
// GreenWorld and Lush use Crystal: Blend-Tri-Crystal-T-Grass-L-Lush.
// Archipelago, GreenWorld and Lush use Archipelago Base-78 in place of Ice: Blend-Tri-Archi-T-Grass-L-Lush.
// Archipelago, GreenWorld and Mars use Mars in place of Lush too: Blend-Tri-Mars-T-Grass-L-Archi.
// Desert, GreenWorld and Lush use Desert Base-80 in place of Ice: Blend-Tri-Desert-T-Grass-L-Lush.
// Archipelago, Desert and GreenWorld use Archipelago in place of Ice and Desert in place of Lush:
// Blend-Tri-Desert-T-Grass-L-Archi has Desert along the top. Acid, Archipelago and GreenWorld do the
// same with Acid in place of Desert: Blend-Tri-Acid-T-Grass-L-Archi, and Archipelago, Crystal and
// GreenWorld with Crystal: Blend-Tri-Crystal-T-Grass-L-Archi, and Archipelago, GreenWorld and Metal
// with Metal: Blend-Tri-Metal-T-Grass-L-Archi, and Archipelago, GreenWorld and Ice with Ice:
// Blend-Tri-Ice-T-Grass-L-Archi. GreenWorld, Mars and Metal use Mars in place of Ice and Metal in place
// of Lush: Blend-Tri-Metal-T-Grass-L-Mars has Metal along the top. GreenWorld, Ice and Metal use Metal
// in place of Lush: Blend-Tri-Metal-T-Grass-L-Ice.
inline std::string triGreenLushName(const BrushGraph& graph, const std::array<int, 4>& corners, const char* thirdWorld,
                                    const char* lastWorld = "Lush", int lastHeight = 77, int thirdHeight = 85,
                                    const char* pivotWorld = "Green", int pivotHeight = 85, const char* pivotName = "Grass") {
    int greenLevel = -1;
    int iceLevel = -1;
    int lushLevel = -1;
    for (const int level : corners) {
        if (graph.world(level) == pivotWorld && graph.height(level) == pivotHeight) {
            greenLevel = level;
        } else if (graph.world(level) == thirdWorld && graph.height(level) == thirdHeight) {
            iceLevel = level;
        } else if (graph.world(level) == lastWorld && graph.height(level) == lastHeight) {
            lushLevel = level;
        } else {
            return {};
        }
    }
    if (greenLevel < 0 || iceLevel < 0 || lushLevel < 0) {
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
    struct Land { int level; const char* name; int named; const char* suffix; };
    const Land lands[3] = {{iceLevel, thirdWorld, greenLevel, lastWorld}, {greenLevel, pivotName, iceLevel, lastWorld}, {lushLevel, lastWorld, greenLevel, thirdWorld}};
    for (const Land& fill : lands) {
        const char* side = sideOf(maskOf(fill.level));
        if (side == nullptr) {
            continue;
        }
        const int namedMask = maskOf(fill.named);
        if (cornerCount(namedMask) != 1) {
            return {};
        }
        const char* namedName = fill.named == greenLevel ? pivotName : thirdWorld;
        const char* where = side[0] == 'T' || side[0] == 'B'
            ? ((namedMask & (kCornerTopRight | kCornerBottomRight)) != 0 ? "R" : "L")
            : ((namedMask & (kCornerBottomLeft | kCornerBottomRight)) != 0 ? "B" : "T");
        return std::string("Blend-Tri-") + fill.name + "-" + side + "-" + namedName + "-" + where + "-" + fill.suffix;
    }
    return {};
}

// The land at each corner of a three-way piece named by triGreenLushName, read back from its name, such
// as blend-tri-acid-l-metal-b-archi-01: Acid fills the left side, Metal is at the bottom right and
// Archipelago at the top right. words are the three lands' lower-case name words; each corner is the
// index of its word there, in the order top left, top right, bottom left, bottom right, or -1 when the
// name is not one of these.
inline std::array<int, 4> threeWayCornerLands(std::string_view loweredName, const std::array<std::string_view, 3>& words) {
    std::array<int, 4> lands = {-1, -1, -1, -1};
    constexpr std::string_view prefix = "blend-tri-";
    if (loweredName.substr(0, prefix.size()) != prefix) {
        return lands;
    }
    std::vector<std::string_view> parts;
    std::string_view rest = loweredName.substr(prefix.size());
    while (!rest.empty()) {
        const std::size_t dash = rest.find('-');
        parts.push_back(rest.substr(0, dash));
        rest = dash == std::string_view::npos ? std::string_view() : rest.substr(dash + 1);
    }
    if (parts.size() < 5) {
        return lands;
    }
    const auto landOf = [&](std::string_view word) {
        for (int land = 0; land < 3; ++land) {
            if (words[static_cast<std::size_t>(land)] == word) {
                return land;
            }
        }
        return -1;
    };
    const int fill = landOf(parts[0]);
    const int named = landOf(parts[2]);
    const int other = landOf(parts[4]);
    const std::string_view side = parts[1];
    const std::string_view pos = parts[3];
    if (fill < 0 || named < 0 || other < 0 || fill == named || fill == other || named == other || side.size() != 1 || pos.size() != 1) {
        return lands;
    }
    for (int corner = 0; corner < 4; ++corner) {
        const bool right = (corner & 1) != 0;
        const bool bottom = (corner & 2) != 0;
        const bool across = side == "t" || side == "b";
        const bool inFill = side == "t" ? !bottom : side == "b" ? bottom : side == "l" ? !right : right;
        const bool atNamed = across ? (pos == "r") == right : (pos == "b") == bottom;
        lands[static_cast<std::size_t>(corner)] = inFill ? fill : atNamed ? named : other;
    }
    return lands;
}

inline std::string triBlendName(const BrushGraph& graph, const std::array<int, 4>& corners) {
    for (const char* third : {"Ice", "Mars", "Metal", "Crystal", "Acid"}) {
        if (std::string greenLush = triGreenLushName(graph, corners, third); !greenLush.empty()) {
            return greenLush;
        }
    }
    if (std::string greenIceMars = triGreenLushName(graph, corners, "Ice", "Mars", 85); !greenIceMars.empty()) {
        return greenIceMars;
    }
    if (std::string archiGreenLush = triGreenLushName(graph, corners, "Archi", "Lush", 77, 78); !archiGreenLush.empty()) {
        return archiGreenLush;
    }
    if (std::string archiGreenMars = triGreenLushName(graph, corners, "Archi", "Mars", 85, 78); !archiGreenMars.empty()) {
        return archiGreenMars;
    }
    if (std::string desertGreenLush = triGreenLushName(graph, corners, "Desert", "Lush", 77, 80); !desertGreenLush.empty()) {
        return desertGreenLush;
    }
    if (std::string archiDesertGreen = triGreenLushName(graph, corners, "Archi", "Desert", 80, 78); !archiDesertGreen.empty()) {
        return archiDesertGreen;
    }
    if (std::string acidArchiGreen = triGreenLushName(graph, corners, "Archi", "Acid", 85, 78); !acidArchiGreen.empty()) {
        return acidArchiGreen;
    }
    if (std::string archiCrystalGreen = triGreenLushName(graph, corners, "Archi", "Crystal", 85, 78); !archiCrystalGreen.empty()) {
        return archiCrystalGreen;
    }
    if (std::string archiGreenMetal = triGreenLushName(graph, corners, "Archi", "Metal", 85, 78); !archiGreenMetal.empty()) {
        return archiGreenMetal;
    }
    if (std::string archiGreenIce = triGreenLushName(graph, corners, "Archi", "Ice", 85, 78); !archiGreenIce.empty()) {
        return archiGreenIce;
    }
    if (std::string greenMarsMetal = triGreenLushName(graph, corners, "Mars", "Metal", 85); !greenMarsMetal.empty()) {
        return greenMarsMetal;
    }
    if (std::string greenIceMetal = triGreenLushName(graph, corners, "Ice", "Metal", 85); !greenIceMetal.empty()) {
        return greenIceMetal;
    }
    // More sets with GreenWorld, each named as triGreenLushName describes: the second land in Ice's place
    // and the last in Lush's. Acid, GreenWorld and Lush use the loop above.
    struct GreenTri { const char* second; int secondHeight; const char* last; int lastHeight; };
    static constexpr GreenTri kMoreGreenTris[] = {
        {"Desert", 80, "Ice", 85}, {"Desert", 80, "Mars", 85}, {"Desert", 80, "Metal", 85}, {"Ice", 85, "Crystal", 85},
        {"Mars", 85, "Crystal", 85}, {"Metal", 85, "Crystal", 85}, {"Desert", 80, "Crystal", 85}, {"Ice", 85, "Acid", 85},
        {"Mars", 85, "Acid", 85}, {"Metal", 85, "Acid", 85}, {"Crystal", 85, "Acid", 85},
        {"Desert", 80, "Acid", 85},
    };
    for (const GreenTri& tri : kMoreGreenTris) {
        if (std::string name = triGreenLushName(graph, corners, tri.second, tri.last, tri.lastHeight, tri.secondHeight); !name.empty()) {
            return name;
        }
    }
    // Sets without GreenWorld, with another land as the pivot in its place.
    struct PivotTri { const char* pivot; int pivotHeight; const char* second; int secondHeight; const char* last; int lastHeight; };
    static constexpr PivotTri kPivotTris[] = {
        {"Lush", 77, "Archi", 78, "Desert", 80}, {"Lush", 77, "Archi", 78, "Ice", 85}, {"Lush", 77, "Archi", 78, "Mars", 85},
        {"Lush", 77, "Archi", 78, "Metal", 85}, {"Ice", 85, "Archi", 78, "Metal", 85},
        {"Mars", 85, "Archi", 78, "Metal", 85},
        {"Crystal", 85, "Archi", 78, "Desert", 80},
        {"Crystal", 85, "Archi", 78, "Ice", 85},
        {"Lush", 77, "Archi", 78, "Crystal", 85},
        {"Crystal", 85, "Archi", 78, "Mars", 85},
        {"Crystal", 85, "Archi", 78, "Metal", 85},
        {"Desert", 80, "Archi", 78, "Ice", 85},
        {"Desert", 80, "Archi", 78, "Mars", 85},
        {"Desert", 80, "Archi", 78, "Metal", 85},
        {"Desert", 80, "Crystal", 85, "Ice", 85},
        {"Crystal", 85, "Archi", 78, "Acid", 85},
        {"Desert", 80, "Archi", 78, "Acid", 85},
        {"Ice", 85, "Archi", 78, "Acid", 85},
        {"Lush", 77, "Archi", 78, "Acid", 85},
        {"Mars", 85, "Archi", 78, "Acid", 85},
        {"Metal", 85, "Archi", 78, "Acid", 85},
        {"Ice", 85, "Crystal", 85, "Acid", 85},
        {"Ice", 85, "Desert", 80, "Acid", 85},
        {"Lush", 77, "Ice", 85, "Acid", 85},
        {"Ice", 85, "Mars", 85, "Acid", 85},
        {"Ice", 85, "Metal", 85, "Acid", 85},
        {"Lush", 77, "Ice", 85, "Crystal", 85},
        {"Ice", 85, "Mars", 85, "Crystal", 85},
        {"Ice", 85, "Metal", 85, "Crystal", 85},
        {"Lush", 77, "Desert", 80, "Ice", 85},
        {"Desert", 80, "Mars", 85, "Ice", 85},
        {"Desert", 80, "Metal", 85, "Ice", 85},
        {"Lush", 77, "Ice", 85, "Mars", 85},
        {"Lush", 77, "Ice", 85, "Metal", 85},
        {"Ice", 85, "Mars", 85, "Metal", 85},
        {"Desert", 80, "Mars", 85, "Metal", 85}, {"Lush", 77, "Desert", 80, "Metal", 85},
        {"Lush", 77, "Desert", 80, "Mars", 85},
        {"Crystal", 85, "Mars", 85, "Metal", 85},
        {"Lush", 77, "Crystal", 85, "Metal", 85},
        {"Lush", 77, "Crystal", 85, "Mars", 85},
        {"Desert", 80, "Crystal", 85, "Metal", 85},
        {"Desert", 80, "Crystal", 85, "Mars", 85},
        {"Lush", 77, "Desert", 80, "Crystal", 85},
        {"Desert", 80, "Crystal", 85, "Acid", 85},
        {"Lush", 77, "Desert", 80, "Acid", 85},
        {"Desert", 80, "Mars", 85, "Acid", 85},
        {"Desert", 80, "Metal", 85, "Acid", 85},
        {"Lush", 77, "Crystal", 85, "Acid", 85},
        {"Crystal", 85, "Mars", 85, "Acid", 85},
        {"Crystal", 85, "Metal", 85, "Acid", 85},
        {"Lush", 77, "Mars", 85, "Acid", 85},
        {"Lush", 77, "Metal", 85, "Acid", 85},
        {"Mars", 85, "Metal", 85, "Acid", 85},
        {"Lush", 77, "Mars", 85, "Metal", 85},
    };
    for (const PivotTri& tri : kPivotTris) {
        if (std::string name = triGreenLushName(graph, corners, tri.second, tri.last, tri.lastHeight, tri.secondHeight, tri.pivot, tri.pivotHeight, tri.pivot);
            !name.empty()) {
            return name;
        }
    }
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
    if (std::string greenMars = triGreenMarsName(graph, corners); !greenMars.empty()) {
        return greenMars;
    }
    if (std::string archiDesert = triArchiDesertName(graph, corners); !archiDesert.empty()) {
        return archiDesert;
    }
    if (std::string iceDesert = triIceDesertName(graph, corners); !iceDesert.empty()) {
        return iceDesert;
    }
    if (std::string iceLush = triIceLushName(graph, corners); !iceLush.empty()) {
        return iceLush;
    }
    if (std::string iceCrystal = triIceCrystalName(graph, corners); !iceCrystal.empty()) {
        return iceCrystal;
    }
    if (std::string iceAcid = triIceAcidName(graph, corners); !iceAcid.empty()) {
        return iceAcid;
    }
    if (std::string desertLush = triDesertLushName(graph, corners); !desertLush.empty()) {
        return desertLush;
    }
    if (std::string greenCrystal = triGreenCrystalName(graph, corners); !greenCrystal.empty()) {
        return greenCrystal;
    }
    if (std::string archiGreen = triArchiGreenName(graph, corners); !archiGreen.empty()) {
        return archiGreen;
    }
    if (std::string archiLush = triArchiLushName(graph, corners); !archiLush.empty()) {
        return archiLush;
    }
    if (std::string lushCrystal = triLushCrystalName(graph, corners); !lushCrystal.empty()) {
        return lushCrystal;
    }
    if (std::string desertMars = triDesertMarsName(graph, corners); !desertMars.empty()) {
        return desertMars;
    }
    if (std::string acidLush = triAcidLushName(graph, corners); !acidLush.empty()) {
        return acidLush;
    }
    if (std::string acidMars = triAcidMarsName(graph, corners); !acidMars.empty()) {
        return acidMars;
    }
    if (std::string archiMars = triArchiMarsName(graph, corners); !archiMars.empty()) {
        return archiMars;
    }
    if (std::string crystalMars = triCrystalMarsName(graph, corners); !crystalMars.empty()) {
        return crystalMars;
    }
    if (std::string greenMetal = triGreenMetalName(graph, corners); !greenMetal.empty()) {
        return greenMetal;
    }
    if (std::string desertMetal = triDesertMetalName(graph, corners); !desertMetal.empty()) {
        return desertMetal;
    }
    if (std::string archiMetal = triArchiMetalName(graph, corners); !archiMetal.empty()) {
        return archiMetal;
    }
    if (std::string lushMetal = triLushMetalName(graph, corners); !lushMetal.empty()) {
        return lushMetal;
    }
    if (std::string iceMetal = triIceMetalName(graph, corners); !iceMetal.empty()) {
        return iceMetal;
    }
    if (std::string acidMetal = triAcidMetalName(graph, corners); !acidMetal.empty()) {
        return acidMetal;
    }
    if (std::string crystalMetal = triCrystalMetalName(graph, corners); !crystalMetal.empty()) {
        return crystalMetal;
    }
    if (std::string acidArchi = triAcidArchiName(graph, corners); !acidArchi.empty()) {
        return acidArchi;
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
    const auto sideOf = [](int mask) -> const char* {
        if (mask == (kCornerTopLeft | kCornerTopRight)) return "T";
        if (mask == (kCornerBottomLeft | kCornerBottomRight)) return "B";
        if (mask == (kCornerTopLeft | kCornerBottomLeft)) return "L";
        if (mask == (kCornerTopRight | kCornerBottomRight)) return "R";
        return nullptr;
    };
    // Where a one-corner land sits along the side opposite the filled one.
    const auto cornerOf = [](const char* side, int mask) {
        return side[0] == 'T' || side[0] == 'B'
            ? ((mask & (kCornerTopRight | kCornerBottomRight)) != 0 ? "R" : "L")
            : ((mask & (kCornerBottomLeft | kCornerBottomRight)) != 0 ? "B" : "T");
    };
    if (acidSide != nullptr) {
        if (cornerCount(grassMask) != 1) {
            return {};
        }
        return std::string("Blend-Tri-Acid-") + acidSide + "-Grass-" + cornerOf(acidSide, grassMask);
    }
    // GreenWorld or Lava filling a side names where the acid is. Lava filling a side ends in -Grass,
    // since the Acid, Crystal and Lava set already has Blend-Tri-Lava-T-Acid-L.
    if (cornerCount(acidMask) != 1) {
        return {};
    }
    if (const char* grassSide = sideOf(grassMask)) {
        return std::string("Blend-Tri-Grass-") + grassSide + "-Acid-" + cornerOf(grassSide, acidMask);
    }
    if (const char* lavaSide = sideOf(maskOf(lavaLevel))) {
        return std::string("Blend-Tri-Lava-") + lavaSide + "-Acid-" + cornerOf(lavaSide, acidMask) + "-Grass";
    }
    return {};
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
        if (style == TransitionStyle::WorldBlend && graph.height(sorted[0]) < 68 && graph.height(sorted[1]) < 68
            && blueWaterWorld(graph.world(sorted[0])) != blueWaterWorld(graph.world(sorted[1]))) {
            // The mask marks the other water. Blend-WaterGreenMars-T has Mars water along the top, and
            // Blend-WaterGreenCrystal, -Desert and -Lush mark theirs. GreenWorld, Ice and Metal draw the
            // same blue water, so all three use these pieces, and Archipelago and Desert draw the same
            // water too, so both use Blend-WaterGreenDesert.
            marked = blueWaterWorld(graph.world(sorted[0])) ? sorted[1] : sorted[0];
            const std::string_view other = graph.world(marked);
            family = "WaterGreen" + std::string(other == "Archi" ? "Desert" : other);
        } else if (style == TransitionStyle::WorldBlend && graph.height(sorted[0]) < 68 && graph.height(sorted[1]) < 68
            && sandWaterWorld(graph.world(sorted[0])) != sandWaterWorld(graph.world(sorted[1]))) {
            // Archipelago and Desert water beside Acid, Lush, Crystal or Mars water. The mask marks the other
            // water: Blend-WaterDesertMars-T has Mars water along the top.
            marked = sandWaterWorld(graph.world(sorted[0])) ? sorted[1] : sorted[0];
            family = "WaterDesert" + std::string(graph.world(marked));
        } else if (style == TransitionStyle::WorldBlend && graph.height(sorted[0]) < 68 && graph.height(sorted[1]) < 68
            && (graph.world(sorted[0]) == "Mars") != (graph.world(sorted[1]) == "Mars")) {
            // Mars water beside Lush, Crystal or Acid water. The mask marks the other water:
            // Blend-WaterMarsAcid-T has Acid water along the top.
            marked = graph.world(sorted[0]) == "Mars" ? sorted[1] : sorted[0];
            family = "WaterMars" + std::string(graph.world(marked));
        } else if (style == TransitionStyle::WorldBlend && graph.height(sorted[0]) < 68 && graph.height(sorted[1]) < 68
            && (graph.world(sorted[0]) == "Lush") != (graph.world(sorted[1]) == "Lush")) {
            // Lush Base-27 water beside Crystal or Acid Base-0 water. The mask marks the lower water:
            // Blend-WaterLushAcid-T has Acid water along the top, and the ground falls from 27 to 0 toward it.
            marked = graph.world(sorted[0]) == "Lush" ? sorted[1] : sorted[0];
            family = "WaterLush" + std::string(graph.world(marked));
        } else if (style == TransitionStyle::WorldBlend && graph.height(sorted[0]) == 0 && graph.height(sorted[1]) == 0
            && ((graph.world(sorted[0]) == "Crystal" && graph.world(sorted[1]) == "Acid")
                || (graph.world(sorted[0]) == "Acid" && graph.world(sorted[1]) == "Crystal"))) {
            // Crystal water beside Acid water. The mask marks the Acid water: Blend-WaterCrystalAcid-T has
            // Acid water along the top.
            marked = graph.world(sorted[0]) == "Acid" ? sorted[0] : sorted[1];
            family = "WaterCrystalAcid";
        } else if (style == TransitionStyle::WorldBlend && graph.height(sorted[0]) == 78 && graph.height(sorted[1]) == 86) {
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
        } else if (style == TransitionStyle::WorldBlend
            && ((graph.height(sorted[0]) == 78 && graph.height(sorted[1]) == 85)
                || (graph.height(sorted[0]) == 85 && graph.height(sorted[1]) == 78))
            && ((graph.world(sorted[0]) == "Archi" && graph.world(sorted[1]) == "Mars")
                || (graph.world(sorted[0]) == "Mars" && graph.world(sorted[1]) == "Archi"))) {
            marked = graph.world(sorted[0]) == "Archi" ? sorted[0] : sorted[1];
            family = "ArchiMars";
        } else if (style == TransitionStyle::WorldBlend
            && ((graph.height(sorted[0]) == 78 && graph.height(sorted[1]) == 85)
                || (graph.height(sorted[0]) == 85 && graph.height(sorted[1]) == 78))
            && ((graph.world(sorted[0]) == "Archi" && graph.world(sorted[1]) == "Metal")
                || (graph.world(sorted[0]) == "Metal" && graph.world(sorted[1]) == "Archi"))) {
            marked = graph.world(sorted[0]) == "Archi" ? sorted[0] : sorted[1];
            family = "ArchiMetal";
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
        } else if (style == TransitionStyle::WorldBlend
            && ((graph.height(sorted[0]) == 80 && graph.height(sorted[1]) == 85)
                || (graph.height(sorted[0]) == 85 && graph.height(sorted[1]) == 80))
            && ((graph.world(sorted[0]) == "Mars" && graph.world(sorted[1]) == "Desert")
                || (graph.world(sorted[0]) == "Desert" && graph.world(sorted[1]) == "Mars"))) {
            marked = graph.world(sorted[0]) == "Desert" ? sorted[0] : sorted[1];
            family = "DesertMars";
        } else if (style == TransitionStyle::WorldBlend
            && ((graph.height(sorted[0]) == 80 && graph.height(sorted[1]) == 85)
                || (graph.height(sorted[0]) == 85 && graph.height(sorted[1]) == 80))
            && ((graph.world(sorted[0]) == "Metal" && graph.world(sorted[1]) == "Desert")
                || (graph.world(sorted[0]) == "Desert" && graph.world(sorted[1]) == "Metal"))) {
            marked = graph.world(sorted[0]) == "Desert" ? sorted[0] : sorted[1];
            family = "DesertMetal";
        } else if (style == TransitionStyle::WorldBlend
            && ((graph.world(sorted[0]) == "Metal" && graph.world(sorted[1]) == "Lava")
                || (graph.world(sorted[0]) == "Lava" && graph.world(sorted[1]) == "Metal"))) {
            marked = graph.world(sorted[0]) == "Metal" ? sorted[0] : sorted[1];
            family = "MetalLava";
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
        } else if (style == TransitionStyle::WorldBlend
            && ((graph.height(sorted[0]) == 77 && graph.height(sorted[1]) == 85)
                || (graph.height(sorted[0]) == 85 && graph.height(sorted[1]) == 77))
            && ((graph.world(sorted[0]) == "Lush" && graph.world(sorted[1]) == "Acid")
                || (graph.world(sorted[0]) == "Acid" && graph.world(sorted[1]) == "Lush"))) {
            marked = graph.world(sorted[0]) == "Acid" ? sorted[0] : sorted[1];
            family = "LushAcid";
        } else if (style == TransitionStyle::WorldBlend
            && ((graph.height(sorted[0]) == 77 && graph.height(sorted[1]) == 85)
                || (graph.height(sorted[0]) == 85 && graph.height(sorted[1]) == 77))
            && ((graph.world(sorted[0]) == "Lush" && graph.world(sorted[1]) == "Crystal")
                || (graph.world(sorted[0]) == "Crystal" && graph.world(sorted[1]) == "Lush"))) {
            marked = graph.world(sorted[0]) == "Crystal" ? sorted[0] : sorted[1];
            family = "LushCrystal";
        } else if (style == TransitionStyle::WorldBlend
            && ((graph.height(sorted[0]) == 77 && graph.height(sorted[1]) == 85)
                || (graph.height(sorted[0]) == 85 && graph.height(sorted[1]) == 77))
            && ((graph.world(sorted[0]) == "Lush" && graph.world(sorted[1]) == "Metal")
                || (graph.world(sorted[0]) == "Metal" && graph.world(sorted[1]) == "Lush"))) {
            marked = graph.world(sorted[0]) == "Metal" ? sorted[0] : sorted[1];
            family = "LushMetal";
        } else if (style == TransitionStyle::WorldBlend
            && ((graph.height(sorted[0]) == 77 && graph.height(sorted[1]) == 80)
                || (graph.height(sorted[0]) == 80 && graph.height(sorted[1]) == 77))
            && ((graph.world(sorted[0]) == "Lush" && graph.world(sorted[1]) == "Desert")
                || (graph.world(sorted[0]) == "Desert" && graph.world(sorted[1]) == "Lush"))) {
            marked = graph.world(sorted[0]) == "Desert" ? sorted[0] : sorted[1];
            family = "DesertLush";
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
        } else if (style == TransitionStyle::WorldBlend && graph.height(sorted[0]) == 85 && graph.height(sorted[1]) == 85
            && ((graph.world(sorted[0]) == "Green" && graph.world(sorted[1]) == "Crystal")
                || (graph.world(sorted[0]) == "Crystal" && graph.world(sorted[1]) == "Green"))) {
            marked = graph.world(sorted[0]) == "Crystal" ? sorted[0] : sorted[1];
            family = "GreenCrystal";
        } else if (style == TransitionStyle::WorldBlend && graph.height(sorted[0]) == 85 && graph.height(sorted[1]) == 85
            && ((graph.world(sorted[0]) == "Acid" && graph.world(sorted[1]) == "Mars")
                || (graph.world(sorted[0]) == "Mars" && graph.world(sorted[1]) == "Acid"))) {
            marked = graph.world(sorted[0]) == "Acid" ? sorted[0] : sorted[1];
            family = "AcidMars";
        } else if (style == TransitionStyle::WorldBlend && graph.height(sorted[0]) == 85 && graph.height(sorted[1]) == 85
            && ((graph.world(sorted[0]) == "Ice" && graph.world(sorted[1]) == "Acid")
                || (graph.world(sorted[0]) == "Acid" && graph.world(sorted[1]) == "Ice"))) {
            marked = graph.world(sorted[0]) == "Ice" ? sorted[0] : sorted[1];
            family = "IceAcid";
        } else if (style == TransitionStyle::WorldBlend && graph.height(sorted[0]) == 85 && graph.height(sorted[1]) == 85
            && ((graph.world(sorted[0]) == "Acid" && graph.world(sorted[1]) == "Metal")
                || (graph.world(sorted[0]) == "Metal" && graph.world(sorted[1]) == "Acid"))) {
            marked = graph.world(sorted[0]) == "Acid" ? sorted[0] : sorted[1];
            family = "AcidMetal";
        } else if (style == TransitionStyle::WorldBlend && graph.height(sorted[0]) == 85 && graph.height(sorted[1]) == 85
            && ((graph.world(sorted[0]) == "Crystal" && graph.world(sorted[1]) == "Mars")
                || (graph.world(sorted[0]) == "Mars" && graph.world(sorted[1]) == "Crystal"))) {
            marked = graph.world(sorted[0]) == "Crystal" ? sorted[0] : sorted[1];
            family = "CrystalMars";
        } else if (style == TransitionStyle::WorldBlend && graph.height(sorted[0]) == 85 && graph.height(sorted[1]) == 85
            && ((graph.world(sorted[0]) == "Mars" && graph.world(sorted[1]) == "Metal")
                || (graph.world(sorted[0]) == "Metal" && graph.world(sorted[1]) == "Mars"))) {
            marked = graph.world(sorted[0]) == "Metal" ? sorted[0] : sorted[1];
            family = "MarsMetal";
        } else if (style == TransitionStyle::WorldBlend && graph.height(sorted[0]) == 85 && graph.height(sorted[1]) == 85
            && ((graph.world(sorted[0]) == "Ice" && graph.world(sorted[1]) == "Crystal")
                || (graph.world(sorted[0]) == "Crystal" && graph.world(sorted[1]) == "Ice"))) {
            marked = graph.world(sorted[0]) == "Ice" ? sorted[0] : sorted[1];
            family = "IceCrystal";
        } else if (style == TransitionStyle::WorldBlend && graph.height(sorted[0]) == 85 && graph.height(sorted[1]) == 85
            && ((graph.world(sorted[0]) == "Ice" && graph.world(sorted[1]) == "Metal")
                || (graph.world(sorted[0]) == "Metal" && graph.world(sorted[1]) == "Ice"))) {
            marked = graph.world(sorted[0]) == "Ice" ? sorted[0] : sorted[1];
            family = "IceMetal";
        } else if (style == TransitionStyle::WorldBlend && graph.height(sorted[0]) == 85 && graph.height(sorted[1]) == 85
            && ((graph.world(sorted[0]) == "Crystal" && graph.world(sorted[1]) == "Metal")
                || (graph.world(sorted[0]) == "Metal" && graph.world(sorted[1]) == "Crystal"))) {
            marked = graph.world(sorted[0]) == "Crystal" ? sorted[0] : sorted[1];
            family = "CrystalMetal";
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
    // A cell whose corners did not change, redrawn only so its band matches the size of the
    // pieces beside it. It is left as it is when the tileset has no piece for it.
    bool matched = false;
};

struct BrushPlan {
    // Every corner the stroke sets, including corners moved to keep levels a step apart.
    std::vector<BrushNode> nodes;
    // Cells around the painted block that need a new picture. Cells whose
    // corners did not change are left alone.
    std::vector<BrushCell> cells;
};

// A brush smaller than a cell, 4 x 4 on the 8 tile cells, paints the point where four cells
// meet. The cursor snaps to the nearest such point, and a map large enough to have one keeps
// the snap off the outer edge so four cells always share it. nodeX and nodeY are that point, in
// the same coordinates planBrush uses. cellTiles is the cell size: 16, or 8 for the fine brushes.
inline bool brushCornerNode(int tileX, int tileY, int cellsX, int cellsY, int& nodeX, int& nodeY, int cellTiles = kBrushCellTiles) {
    if (tileX < 0 || tileY < 0 || cellsX < 0 || cellsY < 0 || cellTiles <= 0) {
        return false;
    }
    const int half = cellTiles / 2;
    nodeX = (tileX + half) / cellTiles;
    nodeY = (tileY + half) / cellTiles;
    if (cellsX > 1) {
        nodeX = std::clamp(nodeX, 1, cellsX - 1);
    }
    if (cellsY > 1) {
        nodeY = std::clamp(nodeY, 1, cellsY - 1);
    }
    return nodeX >= 0 && nodeY >= 0 && nodeX <= cellsX && nodeY <= cellsY;
}

// Top-left cell of a square brush centered on the pointer. A brush one cell wide, 16 x 16 on
// the 16 tile cells or 8 x 8 on the 8 tile cells, is the cell under the pointer. Larger brushes
// cover an even number of cells, so the center is the nearest point where four cells meet and
// the block reaches the same distance on every side of that point.
inline void brushBlockOrigin(int tileX, int tileY, int brushTiles, int cellsX, int cellsY, int& originX, int& originY, int cellTiles = kBrushCellTiles) {
    cellTiles = std::max(1, cellTiles);
    const int blockCells = std::max(1, brushTiles / cellTiles);
    if (blockCells <= 1) {
        originX = tileX / cellTiles;
        originY = tileY / cellTiles;
        return;
    }
    const int half = cellTiles / 2;
    int nodeX = (tileX + half) / cellTiles;
    int nodeY = (tileY + half) / cellTiles;
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

// The level whose height is nearest a tile's height, among the given candidate levels: the brush's own
// tileset when the tile's picture is unknown. Returns -1 on a tie when abstainOnTie is set.
inline int nearestOwnLevel(const std::vector<int>& levelHeights, const std::vector<std::size_t>& candidates, int height, bool abstainOnTie) {
    if (candidates.empty()) {
        return -1;
    }
    std::size_t nearest = candidates.front();
    bool tie = false;
    for (std::size_t k = 1; k < candidates.size(); ++k) {
        const std::size_t i = candidates[k];
        const int distance = std::abs(height - levelHeights[i]);
        const int best = std::abs(height - levelHeights[nearest]);
        if (distance < best) {
            nearest = i;
            tie = false;
        } else if (distance == best) {
            tie = true;
        }
    }
    if (tie && abstainOnTie) {
        return -1;
    }
    return static_cast<int>(nearest);
}

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

// True for a paint that meets a lower paint through cliff pieces, such as GreenWorld's Base-219
// or Lava's Base-190: a cliff top.
inline bool brushCliffLevel(const BrushGraph& graph, int level) {
    for (int other = 0; other < level && other < graph.size(); ++other) {
        const TransitionStyle style = graph.style(other, level);
        if (style == TransitionStyle::Cliff || style == TransitionStyle::CoastalCliffDeep || style == TransitionStyle::CoastalCliffShallow) {
            return true;
        }
    }
    return false;
}

// Reads corners on the 8 tile grid. A corner between the corners of the 16 tile grid lies inside a
// 16 x 16 cell, and cliffs only come in 16 x 16 pieces, so it is only a cliff top when a 16 tile
// corner around it is one. Otherwise it is the raised rim of a coast, whose grass Base-85 and
// Base-219 share, and it is read as the highest other land around it.
// True when the plan moves a corner of another land's own coast, such as GreenWorld Base-85 beside
// GreenWorld Base-0 under a Mars stroke. Water blends are made for islands: Mars land in its own
// water fading into the GreenWorld sea. A Mars coast meeting a GreenWorld coast has no pieces, so
// the Tile Brush refuses that stroke. A corner of another land counts when a cell around it holds
// a corner of the same land at another height before the stroke. GreenWorld, Ice and Metal draw
// the same blue water, so the brush may read an Ice shore's water as GreenWorld Base-0; that water
// counts as Ice's own unless it is the brush's own land. Archipelago and Desert water are the same.

inline bool brushPlanMovesForeignCoast(const BrushPlan& plan, const BrushGraph& graph, std::string_view brushWorld, int cellsX, int cellsY,
    const BrushNodeReader& readNode) {
    if (brushWorld.empty() || !readNode) {
        return false;
    }
    const auto before = [&](int x, int y) { return std::clamp(readNode(x, y), 0, graph.size() - 1); };
    for (const BrushNode& node : plan.nodes) {
        const int old = before(node.x, node.y);
        if (old == node.level) {
            continue;
        }
        const std::string_view world = graph.world(old);
        if (world.empty() || world == brushWorld) {
            continue;
        }
        for (int cy = node.y - 1; cy <= node.y; ++cy) {
            for (int cx = node.x - 1; cx <= node.x; ++cx) {
                if (cx < 0 || cy < 0 || cx >= cellsX || cy >= cellsY) {
                    continue;
                }
                for (const auto& [x, y] : {std::pair{cx, cy}, std::pair{cx + 1, cy}, std::pair{cx, cy + 1}, std::pair{cx + 1, cy + 1}}) {
                    const int level = before(x, y);
                    const bool water = graph.height(level) < 68 || graph.height(old) < 68;
                    const bool sameShore = graph.world(level) == world || (water && sameWaterFamily(graph.world(level), world));
                    if (level != old && graph.world(level) != brushWorld && sameShore && graph.height(level) != graph.height(old)) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

inline BrushNodeReader fineNodeReader(const BrushGraph& graph, BrushNodeReader read) {
    return [&graph, read](int x, int y) {
        const int level = read(x, y);
        if ((x % 2 == 0 && y % 2 == 0) || !brushCliffLevel(graph, level)) {
            return level;
        }
        int land = -1;
        for (const int cy : {y - y % 2, y + y % 2}) {
            for (const int cx : {x - x % 2, x + x % 2}) {
                const int around = read(cx, cy);
                if (around == level) {
                    return level;
                }
                if (!brushCliffLevel(graph, around)) {
                    land = std::max(land, around);
                }
            }
        }
        return land >= 0 ? land : level;
    };
}

// An 8 tile stroke redraws whole 16 x 16 transition cells. Where a stroke on the 8 tile cells
// changes a cell that lies in a 16 x 16 cell whose corners on the 16 tile grid differ, that 16 x 16
// cell holds a 16 x 16 piece, and the rest of it is redrawn too, as 8 x 8 pieces on the corners
// read from the map. Otherwise the part of the old piece left beside the new ones would end its
// band, such as a coast's shallows, in a straight line. blockX, blockY and blockCells are the
// painted block, which the stroke fills itself; blockCells is 0 for a corner stroke. Corners run
// on the 8 tile grid, as in planBrush.
inline void redrawWholeCoarseCells(BrushPlan& plan, int cellsX, int cellsY, int blockX, int blockY, int blockCells, const BrushNodeReader& readNode) {
    if (!readNode || cellsX <= 0 || cellsY <= 0) {
        return;
    }
    const auto keyOf = [&](int x, int y) {
        return static_cast<long long>(y) * (cellsX + 1) + x;
    };
    std::unordered_map<long long, int> after;
    for (const BrushNode& node : plan.nodes) {
        after[keyOf(node.x, node.y)] = node.level;
    }
    std::unordered_map<long long, int> before;
    const auto levelBefore = [&](int x, int y) {
        x = std::clamp(x, 0, cellsX);
        y = std::clamp(y, 0, cellsY);
        const long long key = keyOf(x, y);
        const auto found = before.find(key);
        if (found != before.end()) {
            return found->second;
        }
        const int level = readNode(x, y);
        before.emplace(key, level);
        return level;
    };
    const auto levelAfter = [&](int x, int y) {
        x = std::clamp(x, 0, cellsX);
        y = std::clamp(y, 0, cellsY);
        const auto found = after.find(keyOf(x, y));
        return found != after.end() ? found->second : levelBefore(x, y);
    };
    const auto inBlock = [&](int cx, int cy) {
        return blockCells > 0 && cx >= blockX && cx < blockX + blockCells && cy >= blockY && cy < blockY + blockCells;
    };
    std::unordered_map<long long, bool> planned;
    std::vector<std::pair<int, int>> touched;
    for (const BrushCell& cell : plan.cells) {
        planned[keyOf(cell.x, cell.y)] = true;
        touched.emplace_back(cell.x, cell.y);
    }
    for (int cy = blockY; blockCells > 0 && cy < blockY + blockCells; ++cy) {
        for (int cx = blockX; cx < blockX + blockCells; ++cx) {
            touched.emplace_back(cx, cy);
        }
    }
    std::unordered_map<long long, bool> parents;
    for (const auto& [cx, cy] : touched) {
        const int px = (cx >= 0 ? cx : cx - 1) / 2;
        const int py = (cy >= 0 ? cy : cy - 1) / 2;
        if (!parents.emplace(keyOf(px, py), true).second) {
            continue;
        }
        // The 16 x 16 cell's own corners, two 8 tile cells apart, before the stroke.
        const int a = levelBefore(px * 2, py * 2);
        const int b = levelBefore(px * 2 + 2, py * 2);
        const int c = levelBefore(px * 2, py * 2 + 2);
        const int d = levelBefore(px * 2 + 2, py * 2 + 2);
        if (a == b && a == c && a == d) {
            continue;
        }
        for (int sy = py * 2; sy < py * 2 + 2; ++sy) {
            for (int sx = px * 2; sx < px * 2 + 2; ++sx) {
                if (sx < 0 || sy < 0 || sx >= cellsX || sy >= cellsY || inBlock(sx, sy) || planned.count(keyOf(sx, sy)) != 0) {
                    continue;
                }
                BrushCell cell;
                cell.x = sx;
                cell.y = sy;
                cell.corners = {levelAfter(sx, sy), levelAfter(sx + 1, sy), levelAfter(sx, sy + 1), levelAfter(sx + 1, sy + 1)};
                cell.level = *std::min_element(cell.corners.begin(), cell.corners.end());
                cell.upper = *std::max_element(cell.corners.begin(), cell.corners.end());
                cell.mask = 0;
                if (cell.upper > cell.level) {
                    for (int corner = 0; corner < 4; ++corner) {
                        cell.mask |= cell.corners[static_cast<std::size_t>(corner)] == cell.upper ? (1 << corner) : 0;
                    }
                }
                planned[keyOf(sx, sy)] = true;
                plan.cells.push_back(cell);
            }
        }
    }
}

// Which sizes of brush piece a 16 x 16 cell holds: kCoarseBand for a 16 x 16 piece's band,
// kFineBand for an 8 x 8 piece's, kFixedPiece for a piece that only comes in 16 x 16, any of them,
// or 0 for flat ground.
inline constexpr int kCoarseBand = 1;
inline constexpr int kFineBand = 2;
// A piece that only comes in 16 x 16, such as a cliff or a river. An 8 x 8 stroke stays off it.
inline constexpr int kFixedPiece = 4;
using BrushPieceSizes = std::function<int(int cellX, int cellY)>;

// Keeps every coast, shallows and blend band one size along its whole length. A band that changes
// size at a cell edge jumps there, because a 16 x 16 piece's band crosses a cell side at its middle
// and an 8 x 8 piece's a quarter of the way along, and that jump is a hard crease.
//
// After a stroke on the 8 tile cells, the 16 x 16 cells it touches are followed along every band
// that crosses their sides. Each 16 x 16 cell that a band runs into and that holds a 16 x 16 piece
// is redrawn as four 8 x 8 cells on the corners read from the map, and its bands are followed in
// turn. The walk stops at flat ground, at 8 x 8 pieces, and at cells the brush did not lay, such as
// cliffs or a map's own coast. A touched 16 x 16 cell that holds a 16 x 16 piece is redrawn whole,
// so no part of that piece is left beside the new ones. Corners run on the 8 tile grid, as in
// planBrush, and pieceSizes takes 16 x 16 cells. Returns how many 16 x 16 cells were redrawn beyond
// the stroke's own cells. At most limit cells are followed.
inline int matchBandSizes(BrushPlan& plan, int cellsX, int cellsY, int blockX, int blockY, int blockCells, const BrushNodeReader& readNode,
                          const BrushPieceSizes& pieceSizes, int limit = 4096) {
    if (!readNode || !pieceSizes || cellsX <= 0 || cellsY <= 0) {
        return 0;
    }
    const auto keyOf = [&](int x, int y) {
        return static_cast<long long>(y) * (cellsX + 2) + x;
    };
    std::unordered_map<long long, int> after;
    for (const BrushNode& node : plan.nodes) {
        after[keyOf(node.x, node.y)] = node.level;
    }
    std::unordered_map<long long, int> before;
    const auto levelAfter = [&](int x, int y) {
        x = std::clamp(x, 0, cellsX);
        y = std::clamp(y, 0, cellsY);
        const long long key = keyOf(x, y);
        if (const auto found = after.find(key); found != after.end()) {
            return found->second;
        }
        if (const auto found = before.find(key); found != before.end()) {
            return found->second;
        }
        const int level = readNode(x, y);
        before.emplace(key, level);
        return level;
    };
    const int parentsX = (cellsX + 1) / 2;
    const int parentsY = (cellsY + 1) / 2;
    std::unordered_map<long long, int> sizes;
    const auto sizesOf = [&](int px, int py) {
        const long long key = keyOf(px, py);
        if (const auto found = sizes.find(key); found != sizes.end()) {
            return found->second;
        }
        const int value = pieceSizes(px, py);
        sizes.emplace(key, value);
        return value;
    };
    const auto inBlock = [&](int cx, int cy) {
        return blockCells > 0 && cx >= blockX && cx < blockX + blockCells && cy >= blockY && cy < blockY + blockCells;
    };
    std::unordered_map<long long, bool> planned;
    std::vector<std::pair<int, int>> touched;
    for (const BrushCell& cell : plan.cells) {
        planned[keyOf(cell.x, cell.y)] = true;
        touched.emplace_back(cell.x, cell.y);
    }
    for (int cy = blockY; blockCells > 0 && cy < blockY + blockCells; ++cy) {
        for (int cx = blockX; cx < blockX + blockCells; ++cx) {
            if (cx >= 0 && cy >= 0 && cx < cellsX && cy < cellsY) {
                touched.emplace_back(cx, cy);
            }
        }
    }
    // Redraws the 8 x 8 cells of one 16 x 16 cell that the stroke left alone.
    const auto redraw = [&](int px, int py) {
        for (int sy = py * 2; sy < py * 2 + 2; ++sy) {
            for (int sx = px * 2; sx < px * 2 + 2; ++sx) {
                if (sx >= cellsX || sy >= cellsY || inBlock(sx, sy) || planned.count(keyOf(sx, sy)) != 0) {
                    continue;
                }
                BrushCell cell;
                cell.x = sx;
                cell.y = sy;
                cell.corners = {levelAfter(sx, sy), levelAfter(sx + 1, sy), levelAfter(sx, sy + 1), levelAfter(sx + 1, sy + 1)};
                cell.level = *std::min_element(cell.corners.begin(), cell.corners.end());
                cell.upper = *std::max_element(cell.corners.begin(), cell.corners.end());
                for (int corner = 0; corner < 4 && cell.upper > cell.level; ++corner) {
                    cell.mask |= cell.corners[static_cast<std::size_t>(corner)] == cell.upper ? (1 << corner) : 0;
                }
                cell.matched = true;
                planned[keyOf(sx, sy)] = true;
                plan.cells.push_back(cell);
            }
        }
    };
    std::unordered_map<long long, bool> visited;
    std::vector<std::pair<int, int>> queue;
    for (const auto& [cx, cy] : touched) {
        const int px = cx / 2;
        const int py = cy / 2;
        if (visited.emplace(keyOf(px, py), true).second) {
            queue.emplace_back(px, py);
            if ((sizesOf(px, py) & kCoarseBand) != 0) {
                redraw(px, py);
            }
        }
    }
    int converted = 0;
    for (std::size_t next = 0; next < queue.size(); ++next) {
        const auto [px, py] = queue[next];
        // The three corners along each side: a band crosses that side when they differ.
        constexpr int kSides[4][4] = {{0, 0, 1, 0}, {0, 2, 1, 0}, {0, 0, 0, 1}, {2, 0, 0, 1}};  // start x, start y, step x, step y
        constexpr int kAcross[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
        for (int side = 0; side < 4; ++side) {
            const int x0 = px * 2 + kSides[side][0];
            const int y0 = py * 2 + kSides[side][1];
            const int a = levelAfter(x0, y0);
            const int b = levelAfter(x0 + kSides[side][2], y0 + kSides[side][3]);
            const int c = levelAfter(x0 + 2 * kSides[side][2], y0 + 2 * kSides[side][3]);
            if (a == b && b == c) {
                continue;
            }
            const int qx = px + kAcross[side][0];
            const int qy = py + kAcross[side][1];
            if (qx < 0 || qy < 0 || qx >= parentsX || qy >= parentsY || visited.count(keyOf(qx, qy)) != 0) {
                continue;
            }
            visited.emplace(keyOf(qx, qy), true);
            if ((sizesOf(qx, qy) & kCoarseBand) == 0 || converted >= limit) {
                continue;
            }
            redraw(qx, qy);
            ++converted;
            queue.emplace_back(qx, qy);
        }
    }
    return converted;
}

// Reads corners on the 8 tile grid inside 16 x 16 pieces. A 16 x 16 piece only joins the lands at its
// own four corners, so a point between them is one of those lands: the pale water by a 16 x 16
// shore stands at shallow height, but it is the coast's water or land, not Base-68. A point on a
// side is one of that side's two corners, and the middle one of the four, chosen so no quarter of
// the cell has the same land on opposite corners, which no piece draws. A point beside an 8 x 8
// piece keeps its own reading, since that piece was drawn on it. levelHeights gives each level's
// height, for the nearest land when the reading is none of them; pieceSizes takes 16 x 16 cells.
inline BrushNodeReader coarseInteriorReader(const std::vector<int>& levelHeights, BrushNodeReader read, BrushPieceSizes pieceSizes) {
    struct Caches {
        std::unordered_map<long long, int> raw;
        std::unordered_map<long long, int> sizes;
        std::unordered_map<long long, int> settled;
    };
    auto caches = std::make_shared<Caches>();
    const auto keyOf = [](int x, int y) {
        return (static_cast<long long>(y) << 32) ^ static_cast<long long>(static_cast<unsigned int>(x));
    };
    const auto rawAt = [caches, read, keyOf](int x, int y) {
        const long long key = keyOf(x, y);
        if (const auto found = caches->raw.find(key); found != caches->raw.end()) {
            return found->second;
        }
        const int level = read(x, y);
        caches->raw.emplace(key, level);
        return level;
    };
    const auto sizesAt = [caches, pieceSizes, keyOf](int px, int py) {
        if (px < 0 || py < 0) {
            return 0;
        }
        const long long key = keyOf(px, py);
        if (const auto found = caches->sizes.find(key); found != caches->sizes.end()) {
            return found->second;
        }
        const int value = pieceSizes(px, py);
        caches->sizes.emplace(key, value);
        return value;
    };
    const auto coarseOnly = [sizesAt](int px, int py) {
        const int sizes = sizesAt(px, py);
        return (sizes & kCoarseBand) != 0 && (sizes & kFineBand) == 0;
    };
    const auto heightOf = [levelHeights](int level) {
        return level >= 0 && static_cast<std::size_t>(level) < levelHeights.size() ? levelHeights[static_cast<std::size_t>(level)] : 0;
    };
    // The reading, or the land among allowed nearest to it in height.
    const auto within = [heightOf](int reading, const std::vector<int>& allowed) {
        if (allowed.empty() || std::find(allowed.begin(), allowed.end(), reading) != allowed.end()) {
            return reading;
        }
        int best = allowed.front();
        for (const int level : allowed) {
            if (std::abs(heightOf(level) - heightOf(reading)) < std::abs(heightOf(best) - heightOf(reading))) {
                best = level;
            }
        }
        return best;
    };
    // A point on a side of the 16 tile grid, between corners (ax, ay) and (bx, by), with the 16 x 16
    // cells on either side of that side at (p1x, p1y) and (p2x, p2y).
    const auto sidePoint = [rawAt, sizesAt, coarseOnly, within](int x, int y, int ax, int ay, int bx, int by, int p1x, int p1y, int p2x, int p2y) {
        const int reading = rawAt(x, y);
        const bool besideFine = (sizesAt(p1x, p1y) & kFineBand) != 0 || (sizesAt(p2x, p2y) & kFineBand) != 0;
        if (besideFine || (!coarseOnly(p1x, p1y) && !coarseOnly(p2x, p2y))) {
            return reading;
        }
        return within(reading, {rawAt(ax, ay), rawAt(bx, by)});
    };
    return [rawAt, coarseOnly, within, sidePoint, heightOf, caches, keyOf](int x, int y) {
        if (x % 2 == 0 && y % 2 == 0) {
            return rawAt(x, y);
        }
        const long long key = keyOf(x, y);
        if (const auto found = caches->settled.find(key); found != caches->settled.end()) {
            return found->second;
        }
        int level = 0;
        if (y % 2 == 0) {
            // On a side running across, between the cells above and below.
            level = sidePoint(x, y, x - 1, y, x + 1, y, x / 2, y / 2 - 1, x / 2, y / 2);
        } else if (x % 2 == 0) {
            level = sidePoint(x, y, x, y - 1, x, y + 1, x / 2 - 1, y / 2, x / 2, y / 2);
        } else {
            // The middle of a 16 x 16 cell.
            const int reading = rawAt(x, y);
            const int px = x / 2;
            const int py = y / 2;
            if (!coarseOnly(px, py)) {
                level = reading;
            } else {
                const int a = rawAt(x - 1, y - 1);
                const int b = rawAt(x + 1, y - 1);
                const int c = rawAt(x - 1, y + 1);
                const int d = rawAt(x + 1, y + 1);
                const int top = sidePoint(x, y - 1, x - 1, y - 1, x + 1, y - 1, px, py - 1, px, py);
                const int bottom = sidePoint(x, y + 1, x - 1, y + 1, x + 1, y + 1, px, py, px, py + 1);
                const int left = sidePoint(x - 1, y, x - 1, y - 1, x - 1, y + 1, px - 1, py, px, py);
                const int right = sidePoint(x + 1, y, x + 1, y - 1, x + 1, y + 1, px, py, px + 1, py);
                // Opposite corners of a quarter on one land and the other two on another: no piece draws that.
                const auto awkward = [](int q0, int q1, int q2, int q3) {
                    const bool diagonal = q0 == q3 && q1 == q2 && q0 != q1;
                    int distinct = 1 + (q1 != q0 ? 1 : 0);
                    distinct += (q2 != q0 && q2 != q1) ? 1 : 0;
                    distinct += (q3 != q0 && q3 != q1 && q3 != q2) ? 1 : 0;
                    return (diagonal ? 4 : 0) + (distinct > 2 ? 1 : 0);
                };
                const std::vector<int> corners = {a, b, c, d};
                const int preferred = within(reading, corners);
                level = preferred;
                int bestScore = 1 << 20;
                for (const int candidate : {preferred, a, b, c, d}) {
                    const int score = awkward(a, top, left, candidate) + awkward(top, b, candidate, right)
                        + awkward(left, candidate, c, bottom) + awkward(candidate, right, bottom, d);
                    const bool closer = score == bestScore && std::abs(heightOf(candidate) - heightOf(reading)) < std::abs(heightOf(level) - heightOf(reading));
                    if (score < bestScore || closer) {
                        bestScore = score;
                        level = candidate;
                    }
                }
            }
        }
        caches->settled.emplace(key, level);
        return level;
    };
}

// True when a stroke on the 16 tile cells would leave a 16 x 16 piece beside an 8 x 8 piece: some
// 16 x 16 cell it redraws or fills sits beside one that holds an 8 x 8 piece. Such a stroke is
// painted on the 8 tile cells instead, so the band stays one size.
inline bool coarseStrokeMeetsFineBand(const BrushPlan& plan, int cellsX, int cellsY, int blockX, int blockY, int blockCells, const BrushPieceSizes& pieceSizes) {
    if (!pieceSizes) {
        return false;
    }
    std::vector<std::pair<int, int>> touched;
    for (const BrushCell& cell : plan.cells) {
        touched.emplace_back(cell.x, cell.y);
    }
    for (int cy = blockY; blockCells > 0 && cy < blockY + blockCells; ++cy) {
        for (int cx = blockX; cx < blockX + blockCells; ++cx) {
            touched.emplace_back(cx, cy);
        }
    }
    const auto isTouched = [&](int x, int y) {
        return std::find(touched.begin(), touched.end(), std::pair<int, int>{x, y}) != touched.end();
    };
    for (const auto& [cx, cy] : touched) {
        constexpr int kAround[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
        for (const auto& step : kAround) {
            const int qx = cx + step[0];
            const int qy = cy + step[1];
            if (qx >= 0 && qy >= 0 && qx < cellsX && qy < cellsY && !isTouched(qx, qy) && (pieceSizes(qx, qy) & kFineBand) != 0) {
                return true;
            }
        }
    }
    return false;
}

}  // namespace galaxy
