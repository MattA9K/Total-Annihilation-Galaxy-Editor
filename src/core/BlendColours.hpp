#pragma once

// Colour tests that tell the two lands of a blend tile apart when their heights are too close to.
// Each looks at one 32 x 32 tile and says whether it is the first land named in the function.
//
// kBlendColourPairs lists every pair of lands these tests separate in the editor's picture reading.
// A test that misreads one land's plain base tiles makes the brush see that land as the other one,
// and painting beside the blend then breaks it up. format_test checks every pair against both
// lands' base tiles, so when a new blend is read with one of these tests, add its pair here.

#include "format/TaPalette.hpp"

#include <cstdint>

namespace galaxy {

// Grass in the green-acid blend is much greener than the acid ground.
inline bool greenAcidTileIsGrass(const std::uint8_t* bytes) {
    int score = 0;
    for (int i = 0; i < 1024; ++i) {
        const std::uint8_t* color = kTaPalette[bytes[i]];
        score += static_cast<int>(color[1]) - static_cast<int>(color[0]);
    }
    return score >= 20 * 1024;
}

// Lush ground is green. Archipelago sand in the same blend is pale.
inline bool archiLushTileIsLush(const std::uint8_t* bytes) {
    int score = 0;
    for (int i = 0; i < 1024; ++i) {
        const std::uint8_t* color = kTaPalette[bytes[i]];
        score += static_cast<int>(color[1]) - static_cast<int>(color[0]);
    }
    return score >= 40 * 1024;
}

// Mars ground is red; Lush grass is green.
inline bool lushMarsTileIsMars(const std::uint8_t* bytes) {
    int score = 0;
    for (int i = 0; i < 1024; ++i) {
        const std::uint8_t* color = kTaPalette[bytes[i]];
        score += static_cast<int>(color[0]) - static_cast<int>(color[1]);
    }
    return score >= 0;
}

// Desert sand is pale; Lava dirt is dark. Desert Base-80 tiles average about 150 bright and
// Lava Base-86 tiles about 66.
inline bool desertLavaTileIsDesert(const std::uint8_t* bytes) {
    long brightness = 0;
    for (int i = 0; i < 1024; ++i) {
        const std::uint8_t* color = kTaPalette[bytes[i]];
        brightness += 30 * color[0] + 59 * color[1] + 11 * color[2];
    }
    return brightness >= 105L * 100 * 1024;
}

// Crystal ground is purple; Lava dirt is brown.
inline bool crystalLavaTileIsCrystal(const std::uint8_t* bytes) {
    int score = 0;
    for (int i = 0; i < 1024; ++i) {
        const std::uint8_t* color = kTaPalette[bytes[i]];
        score += static_cast<int>(color[2]) - static_cast<int>(color[1]);
    }
    // Crystal tiles average at least 35 more blue than green. Lava and Mars average about 15 less,
    // and Metal plating up to 2 more, so 12 keeps every plain Metal tile off Crystal.
    return score >= 12 * 1024;
}

// Acid ground is olive; Metal plating is a warm grey. Green over blue alone overlaps between the two,
// so green over red counts twice: Acid tiles score at least 18 a pixel and Metal tiles at most 4.
inline bool acidMetalTileIsAcid(const std::uint8_t* bytes) {
    int score = 0;
    for (int i = 0; i < 1024; ++i) {
        const int r = kTaPalette[bytes[i]][0];
        const int g = kTaPalette[bytes[i]][1];
        const int b = kTaPalette[bytes[i]][2];
        score += 2 * (g - r) + (g - b);
    }
    return score >= 11 * 1024;
}

// Red over green, averaged over a tile. Mars tiles average at least 47.
inline int redOverGreen(const std::uint8_t* bytes) {
    int score = 0;
    for (int i = 0; i < 1024; ++i) {
        score += static_cast<int>(kTaPalette[bytes[i]][0]) - static_cast<int>(kTaPalette[bytes[i]][1]);
    }
    return score / 1024;
}

// Mars ground is red; Metal plating is a warm grey that averages up to 25 more red than green.
inline bool marsMetalColourIsMars(const std::uint8_t* bytes) {
    return redOverGreen(bytes) >= 36;
}

// Mars ground is red; Acid ground is olive, with at most about 1 more red than green.
inline bool acidMarsColourIsMars(const std::uint8_t* bytes) {
    return redOverGreen(bytes) >= 24;
}

// Ice is bright white and blue, about 228 bright; Crystal ground is dark purple, at most about 86.
// Crystal has more blue than red too, so blue over red would call it ice.
inline bool iceCrystalColourIsIce(const std::uint8_t* bytes) {
    long brightness = 0;
    for (int i = 0; i < 1024; ++i) {
        brightness += 30 * kTaPalette[bytes[i]][0] + 59 * kTaPalette[bytes[i]][1] + 11 * kTaPalette[bytes[i]][2];
    }
    return brightness >= 155L * 100 * 1024;
}

// Lush grass in the lava blend is green. Lava ground is brown.
inline bool lushLavaTileIsLush(const std::uint8_t* bytes) {
    int score = 0;
    for (int i = 0; i < 1024; ++i) {
        const std::uint8_t* color = kTaPalette[bytes[i]];
        score += static_cast<int>(color[1]) - static_cast<int>(color[0]);
    }
    return score >= 40 * 1024;
}

// Lush grass is green. Ice in the same blend is pale, and both sides are blue
// enough that green-minus-red is what separates them.
inline bool lushIceTileIsLush(const std::uint8_t* bytes) {
    int score = 0;
    for (int i = 0; i < 1024; ++i) {
        const std::uint8_t* color = kTaPalette[bytes[i]];
        score += static_cast<int>(color[1]) - static_cast<int>(color[0]);
    }
    return score >= 40 * 1024;
}

// Ice in the green blend is pale blue. Grass is green.
inline bool iceGreenTileIsIce(const std::uint8_t* bytes) {
    int score = 0;
    for (int i = 0; i < 1024; ++i) {
        const std::uint8_t* color = kTaPalette[bytes[i]];
        score += static_cast<int>(color[2]) - static_cast<int>(color[0]);
    }
    return score >= 12 * 1024;
}

// Ice in the mars blend is pale blue. Mars ground is red.
// GreenWorld grass is yellow-green with some red; Lush grass is blue-green with almost none.
inline bool greenLushTileIsGreen(const std::uint8_t* bytes) {
    int score = 0;
    for (int i = 0; i < 1024; ++i) {
        const std::uint8_t* color = kTaPalette[bytes[i]];
        score += static_cast<int>(color[0]) - static_cast<int>(color[2]);
    }
    return score >= 0;
}

// GreenWorld grass is green; Metal plating is grey.
inline bool greenMetalTileIsGrass(const std::uint8_t* bytes) {
    int grass = 0;
    for (int i = 0; i < 1024; ++i) {
        const std::uint8_t* color = kTaPalette[bytes[i]];
        grass += color[1] > color[0] + 15 && color[1] > color[2] + 10;
    }
    return grass * 5 >= 1024;
}

// Metal plating is grey; Lava dirt is brown. Metal plating is a warm grey, and a quarter or more of
// a plain Metal tile can look brown pixel by pixel, so brown pixels alone call most Metal tiles Lava.
// Grey pixels, with blue within 12 of red, tell them apart instead: every plain Metal tile has at
// least a third of them and a plain Lava tile almost none. A Lava corner with Metal specks has
// about a quarter.
inline bool metalLavaTileIsMetal(const std::uint8_t* bytes) {
    int grey = 0;
    for (int i = 0; i < 1024; ++i) {
        grey += kTaPalette[bytes[i]][2] + 12 >= kTaPalette[bytes[i]][0];
    }
    return grey * 100 >= 28 * 1024;
}

// Crystal ground is purple; Acid ground is olive.
inline bool acidCrystalTileIsCrystal(const std::uint8_t* bytes) {
    int score = 0;
    for (int i = 0; i < 1024; ++i) {
        const std::uint8_t* color = kTaPalette[bytes[i]];
        score += static_cast<int>(color[2]) - static_cast<int>(color[1]);
    }
    return score >= 0;
}

// GreenWorld grass is green; Mars ground is red-brown.
inline bool greenMarsTileIsGrass(const std::uint8_t* bytes) {
    int score = 0;
    for (int i = 0; i < 1024; ++i) {
        const std::uint8_t* color = kTaPalette[bytes[i]];
        score += static_cast<int>(color[1]) - static_cast<int>(color[0]);
    }
    return score >= 0;
}

inline bool iceMarsTileIsIce(const std::uint8_t* bytes) {
    int score = 0;
    for (int i = 0; i < 1024; ++i) {
        const std::uint8_t* color = kTaPalette[bytes[i]];
        score += static_cast<int>(color[2]) - static_cast<int>(color[0]);
    }
    return score >= 12 * 1024;
}

// Archipelago sand in the acid blend is pale. Acid ground is much darker.
inline bool archiAcidTileIsSand(const std::uint8_t* bytes) {
    int red = 0;
    for (int i = 0; i < 1024; ++i) {
        red += static_cast<int>(kTaPalette[bytes[i]][0]);
    }
    return red >= 150 * 1024;
}

using BlendColourTest = bool (*)(const std::uint8_t*);

// One pair of lands told apart by a colour test: tiles of yesBase must pass and tiles of noBase must
// not. The bases are section paths under sections/BaseTiles in the favorites archive.
struct BlendColourPair {
    const char* blend;
    const char* yesBase;
    const char* noBase;
    BlendColourTest isYes;
};

inline constexpr BlendColourPair kBlendColourPairs[] = {
    {"GreenWorld / Acid", "GreenWorld/Base-85", "Acid/Base-85", greenAcidTileIsGrass},
    {"GreenWorld / Crystal", "GreenWorld/Base-85", "Crystal/Base-85", greenAcidTileIsGrass},
    {"Lush / Archipelago", "Lush/Base-77", "Archipelago/Base-78", archiLushTileIsLush},
    {"Lush / Acid", "Lush/Base-77", "Acid/Base-85", archiLushTileIsLush},
    {"Lush / Crystal", "Lush/Base-77", "Crystal/Base-85", archiLushTileIsLush},
    {"Lush / Mars", "Mars/Base-85", "Lush/Base-77", lushMarsTileIsMars},
    {"Mars / Metal", "Mars/Base-85", "Metal/Base-85", marsMetalColourIsMars},
    {"Acid / Mars", "Mars/Base-85", "Acid/Base-85", acidMarsColourIsMars},
    {"Desert / Lava", "Desert/Base-80", "Lava/Base-86", desertLavaTileIsDesert},
    {"Desert / Lush", "Desert/Base-80", "Lush/Base-77", desertLavaTileIsDesert},
    {"Desert / Metal", "Desert/Base-80", "Metal/Base-85", desertLavaTileIsDesert},
    {"Desert / Mars", "Desert/Base-80", "Mars/Base-85", desertLavaTileIsDesert},
    {"Crystal / Lava", "Crystal/Base-85", "Lava/Base-86", crystalLavaTileIsCrystal},
    {"Crystal / Mars", "Crystal/Base-85", "Mars/Base-85", crystalLavaTileIsCrystal},
    {"Crystal / Metal", "Crystal/Base-85", "Metal/Base-85", crystalLavaTileIsCrystal},
    {"Acid / Metal", "Acid/Base-85", "Metal/Base-85", acidMetalTileIsAcid},
    {"Lush / Lava", "Lush/Base-77", "Lava/Base-86", lushLavaTileIsLush},
    {"Lush / Ice", "Lush/Base-77", "Ice/Base-85", lushIceTileIsLush},
    {"Ice / GreenWorld", "Ice/Base-85", "GreenWorld/Base-85", iceGreenTileIsIce},
    {"GreenWorld / Lush", "GreenWorld/Base-85", "Lush/Base-77", greenLushTileIsGreen},
    {"GreenWorld / Metal", "GreenWorld/Base-85", "Metal/Base-85", greenMetalTileIsGrass},
    {"Lush / Metal", "Lush/Base-77", "Metal/Base-85", greenMetalTileIsGrass},
    {"Metal / Lava", "Metal/Base-85", "Lava/Base-86", metalLavaTileIsMetal},
    {"Crystal / Acid", "Crystal/Base-85", "Acid/Base-85", acidCrystalTileIsCrystal},
    {"GreenWorld / Mars", "GreenWorld/Base-85", "Mars/Base-85", greenMarsTileIsGrass},
    {"Ice / Mars", "Ice/Base-85", "Mars/Base-85", iceMarsTileIsIce},
    {"Ice / Lava", "Ice/Base-85", "Lava/Base-86", iceMarsTileIsIce},
    {"Ice / Archipelago", "Ice/Base-85", "Archipelago/Base-78", iceMarsTileIsIce},
    {"Ice / Desert", "Ice/Base-85", "Desert/Base-80", iceMarsTileIsIce},
    {"Ice / Acid", "Ice/Base-85", "Acid/Base-85", iceMarsTileIsIce},
    {"Ice / Metal", "Ice/Base-85", "Metal/Base-85", iceMarsTileIsIce},
    {"Ice / Crystal", "Ice/Base-85", "Crystal/Base-85", iceCrystalColourIsIce},
    {"Archipelago / Acid", "Archipelago/Base-78", "Acid/Base-85", archiAcidTileIsSand},
    {"Archipelago / Mars", "Archipelago/Base-78", "Mars/Base-85", archiAcidTileIsSand},
    {"Archipelago / Metal", "Archipelago/Base-78", "Metal/Base-85", archiAcidTileIsSand},
};

}  // namespace galaxy
