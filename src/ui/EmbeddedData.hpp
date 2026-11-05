#pragma once

namespace galaxy {

// Defined in a file CMake writes at configure time from the PNG files named in
// CMakeLists.txt. A size of 0 means the build had no such picture.
extern const unsigned char kSplashImageData[];
extern const unsigned long kSplashImageSize;

extern const unsigned char kWindowIcon256Data[];
extern const unsigned long kWindowIcon256Size;
extern const unsigned char kWindowIcon64Data[];
extern const unsigned long kWindowIcon64Size;
extern const unsigned char kWindowIcon32Data[];
extern const unsigned long kWindowIcon32Size;

extern const unsigned char kDockIconData[];
extern const unsigned long kDockIconSize;

// Subset fonts for the characters the translations use beyond the system UI font.
extern const unsigned char kGalaxyLatinData[];
extern const unsigned long kGalaxyLatinSize;
extern const unsigned char kGalaxyCjkData[];
extern const unsigned long kGalaxyCjkSize;

}  // namespace galaxy
